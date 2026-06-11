#include "AudioEngine.h"
#include "../Plugins/PluginHost.h"
#include "../Rack/RackProcessor.h"

namespace dg
{

namespace rebuild
{
    enum { graph = 1, tempo = 2, flags = 4, automation = 8, transport = 16, playlists = 32, mods = 64 };
}

AudioEngine::AudioEngine (SessionModel& s, PluginHost& ph, juce::PropertiesFile* props)
    : session (s), pluginHost (ph), appProps (props)
{
    formatManager.registerBasicFormats();
    stretchCache = std::make_unique<StretchCache> (formatManager);
    diskThread.startThread (juce::Thread::Priority::normal);
    recordThread.startThread (juce::Thread::Priority::normal);

    midiCollector.reset (48000.0);
    graph.setPlayHead (this);
    audioInNode  = graph.addNode (std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor> (
                       juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    audioOutNode = graph.addNode (std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor> (
                       juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    midiInNode   = graph.addNode (std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor> (
                       juce::AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));

    std::unique_ptr<juce::XmlElement> savedAudio;
    if (appProps != nullptr)
        savedAudio = appProps->getXmlValue ("audioDeviceState");
    deviceManager.initialise (2, 2, savedAudio.get(), true);
    deviceManager.addAudioCallback (this);
    deviceManager.addChangeListener (this);
    deviceManager.addMidiInputDeviceCallback ({}, this);
    previewPlayer.setSource (&previewTransport);
    deviceManager.addAudioCallback (&previewPlayer);

    startTimer (1000);
    sessionReplaced();
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    stopPreview();
    deviceManager.removeAudioCallback (&previewPlayer);
    previewPlayer.setSource (nullptr);
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeAudioCallback (this);
    deviceManager.removeChangeListener (this);
    detachAutomation();
    modListeners.clear();
    session.root.removeListener (this);
    graph.clear();
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (appProps != nullptr)
        if (auto xml = deviceManager.createStateXml())
            appProps->setValue ("audioDeviceState", xml.get());
}

// ============================================================ model -> engine

void AudioEngine::sessionReplaced()
{
    session.root.removeListener (this);
    session.root.addListener (this);
    transportPos = 0;
    rebuildTempoMap();
    rebuildGraph();
    updateTransportFromTree();
}

void AudioEngine::scheduleRebuild (int what)
{
    rebuildFlags |= what;
    triggerAsyncUpdate();
}

void AudioEngine::handleAsyncUpdate()
{
    const int f = std::exchange (rebuildFlags, 0);
    if (f & rebuild::tempo)      rebuildTempoMap();
    if (f & rebuild::graph)      rebuildGraph();          // includes playlists/flags/automation
    else
    {
        if (f & rebuild::playlists)
        {
            if (allPlaylistsDirty) updateAllPlaylists();
            else for (auto& uid : playlistDirtyTracks)
                     updateTrackPlaylists (session.findTrack (uid));
        }
        if (f & rebuild::flags)      updateTrackFlags();
        if (f & rebuild::automation) rebuildAutomation();
        if (f & rebuild::mods)       rebuildMods();
    }
    if (f & rebuild::transport)  updateTransportFromTree();
    allPlaylistsDirty = false;
    playlistDirtyTracks.clear();
}

static ValueTree findParentOfType (ValueTree v, const Identifier& type)
{
    while (v.isValid() && ! v.hasType (type))
        v = v.getParent();
    return v;
}

void AudioEngine::valueTreePropertyChanged (ValueTree& tree, const Identifier& prop)
{
    if (tree.hasType (id::TRANSPORT)) { scheduleRebuild (rebuild::transport); return; }
    if (tree.hasType (id::TEMPO) || tree.hasType (id::TIMESIG))
    {
        scheduleRebuild (rebuild::tempo | rebuild::playlists);
        allPlaylistsDirty = true;
        return;
    }
    if (tree.hasType (id::TRACK))
    {
        if (prop == id::mute || prop == id::solo || prop == id::armed
            || prop == id::monitor || prop == id::inputChan || prop == id::inputStereo)
        { scheduleRebuild (rebuild::flags); return; }

        if (prop == id::outputBus || prop == id::sendABus || prop == id::sendBBus)
        { scheduleRebuild (rebuild::graph); return; }

        const String uid = tree[id::uid];
        if (prop == id::gain)
            if (auto* st = getStrip (uid)) st->gainDb->setValueNotifyingHost (
                st->gainDb->convertTo0to1 ((float) (double) tree[id::gain]));
        if (prop == id::pan)
            if (auto* st = getStrip (uid)) st->pan->setValueNotifyingHost (
                st->pan->convertTo0to1 ((float) (double) tree[id::pan]));
        if (prop == id::sendALevel)
            if (auto* sp = getSend (uid, 0)) sp->levelDb->setValueNotifyingHost (
                sp->levelDb->convertTo0to1 ((float) (double) tree[id::sendALevel]));
        if (prop == id::sendBLevel)
            if (auto* sp = getSend (uid, 1)) sp->levelDb->setValueNotifyingHost (
                sp->levelDb->convertTo0to1 ((float) (double) tree[id::sendBLevel]));
        return;
    }
    if (tree.hasType (id::INSERT))
    {
        if (prop == id::bypass)
        {
            auto it = insertNodes.find (tree[id::uid].toString());
            if (it != insertNodes.end() && it->second != nullptr)
                it->second->setBypassed ((bool) tree[id::bypass]);
            return;
        }
        if (prop == id::ident || prop == id::type)
            scheduleRebuild (rebuild::graph);
        return;
    }
    if (tree.hasType (id::CLIP) || tree.hasType (id::NOTE))
    {
        auto track = findParentOfType (tree, id::TRACK);
        if (track.isValid())
        {
            playlistDirtyTracks.addIfNotAlreadyThere (track[id::uid].toString());
            scheduleRebuild (rebuild::playlists);
        }
        return;
    }
    if (tree.hasType (id::LANE) || tree.hasType (id::PT))
    { scheduleRebuild (rebuild::automation); return; }
    if (tree.hasType (id::MODS) || tree.hasType (id::MOD) || tree.hasType (id::MODTARGET))
        scheduleRebuild (rebuild::mods);
}

void AudioEngine::valueTreeChildAdded (ValueTree& parent, ValueTree& child)
{
    if (parent.hasType (id::TRACKS) || parent.hasType (id::INSERTS) || child.hasType (id::INSERT))
    { scheduleRebuild (rebuild::graph); return; }
    if (child.hasType (id::CLIP) || child.hasType (id::NOTE) || parent.hasType (id::CLIPS) || parent.hasType (id::NOTES))
    {
        auto track = findParentOfType (parent, id::TRACK);
        if (track.isValid())
        {
            playlistDirtyTracks.addIfNotAlreadyThere (track[id::uid].toString());
            scheduleRebuild (rebuild::playlists);
        }
        return;
    }
    if (child.hasType (id::LANE) || child.hasType (id::PT))
    { scheduleRebuild (rebuild::automation); return; }
    if (child.hasType (id::MOD) || child.hasType (id::MODTARGET))
    { scheduleRebuild (rebuild::mods); return; }
    if (child.hasType (id::TEMPO) || child.hasType (id::TIMESIG))
    { allPlaylistsDirty = true; scheduleRebuild (rebuild::tempo | rebuild::playlists); }
}

void AudioEngine::valueTreeChildRemoved (ValueTree& parent, ValueTree& child, int)
{
    valueTreeChildAdded (parent, child);
}

void AudioEngine::valueTreeChildOrderChanged (ValueTree& parent, int, int)
{
    if (parent.hasType (id::INSERTS) || parent.hasType (id::TRACKS))
        scheduleRebuild (rebuild::graph);
}

// ============================================================ tempo map

void AudioEngine::rebuildTempoMap()
{
    auto next = std::make_shared<const TempoMap> (session.tempoMap(), currentSR);
    {
        juce::SpinLock::ScopedLockType sl (mapLock);
        if (rtMap == nullptr) rtMap = next;     // first call, before audio runs
        mapGraveyard.push_back (pendingMap);
        pendingMap = next;
    }
}

std::shared_ptr<const TempoMap> AudioEngine::getTempoMap() const
{
    juce::SpinLock::ScopedLockType sl (const_cast<juce::SpinLock&> (mapLock));
    return pendingMap != nullptr ? pendingMap : rtMap;
}

// ============================================================ graph build

void AudioEngine::rebuildGraph()
{
    detachAutomation();
    modListeners.clear();           // listeners reference params on nodes we may remove

    auto trackList = session.tracks();

    // drop nodes for tracks/inserts that no longer exist
    juce::StringArray liveTracks, liveInserts;
    for (const auto& t : trackList)
    {
        liveTracks.add (t[id::uid]);
        for (const auto& ins : t.getChildWithName (id::INSERTS))
            liveInserts.add (ins[id::uid]);
    }
    for (auto it = insertNodes.begin(); it != insertNodes.end();)
    {
        if (! liveInserts.contains (it->first)) { graph.removeNode (it->second->nodeID); it = insertNodes.erase (it); }
        else ++it;
    }
    for (auto it = trackNodes.begin(); it != trackNodes.end();)
    {
        if (! liveTracks.contains (it->first))
        {
            auto& tn = it->second;
            for (auto& n : { tn.source, tn.instrument, tn.strip, tn.sendA, tn.sendB })
                if (n != nullptr) graph.removeNode (n->nodeID);
            it = trackNodes.erase (it);
        }
        else ++it;
    }

    // ensure nodes per track
    for (const auto& t : trackList)
    {
        const String uid = t[id::uid];
        const String type = t[id::type];
        auto& tn = trackNodes[uid];
        const bool isNewStrip = (tn.strip == nullptr && type != "video");

        if (type == "audio" && tn.source == nullptr)
            tn.source = graph.addNode (std::make_unique<ClipPlayerProcessor> (*this, uid));
        if (type == "midi" && tn.source == nullptr)
            tn.source = graph.addNode (std::make_unique<MidiSourceProcessor> (*this, uid));
        if (type != "video" && tn.strip == nullptr)
            tn.strip = graph.addNode (std::make_unique<ChannelStripProcessor> (t[id::name]));
        if ((type == "audio" || type == "midi") && tn.sendA == nullptr)
        {
            tn.sendA = graph.addNode (std::make_unique<SendProcessor> ("Send A"));
            tn.sendB = graph.addNode (std::make_unique<SendProcessor> ("Send B"));
        }

        if (isNewStrip && tn.strip != nullptr)     // restore persisted mixer state
        {
            auto* st = static_cast<ChannelStripProcessor*> (tn.strip->getProcessor());
            st->gainDb->setValueNotifyingHost (st->gainDb->convertTo0to1 ((float) (double) t.getProperty (id::gain, 0.0)));
            st->pan->setValueNotifyingHost (st->pan->convertTo0to1 ((float) (double) t.getProperty (id::pan, 0.0)));
            if (tn.sendA != nullptr)
            {
                auto* sa = static_cast<SendProcessor*> (tn.sendA->getProcessor());
                sa->levelDb->setValueNotifyingHost (sa->levelDb->convertTo0to1 ((float) (double) t.getProperty (id::sendALevel, -60.0)));
                auto* sb = static_cast<SendProcessor*> (tn.sendB->getProcessor());
                sb->levelDb->setValueNotifyingHost (sb->levelDb->convertTo0to1 ((float) (double) t.getProperty (id::sendBLevel, -60.0)));
            }
        }

        // instrument node for midi tracks: hosted INSERT(type=="instrument") or built-in synth
        if (type == "midi")
        {
            ValueTree instIns;
            for (const auto& ins : t.getChildWithName (id::INSERTS))
                if (ins[id::type].toString() == "instrument") { instIns = ins; break; }

            tn.instrument = nullptr;
            if (instIns.isValid())
            {
                const String iuid = instIns[id::uid];
                if (insertNodes.count (iuid) && insertIdents[iuid] != instIns[id::ident].toString())
                {
                    graph.removeNode (insertNodes[iuid]->nodeID);   // plugin swapped
                    insertNodes.erase (iuid);
                }
                if (! insertNodes.count (iuid))
                    instantiateInsert (instIns, currentSR, currentBlock);
                if (insertNodes.count (iuid))
                    tn.instrument = insertNodes[iuid];
            }
            if (tn.instrument == nullptr)
            {
                if (tn.builtinSynth == nullptr)
                    tn.builtinSynth = graph.addNode (std::make_unique<SimpleSynthProcessor>());
                tn.instrument = tn.builtinSynth;
            }
        }

        // fx/rack inserts
        tn.inserts.clear();
        for (const auto& ins : t.getChildWithName (id::INSERTS))
        {
            const String itype = ins[id::type];
            if (itype == "instrument") continue;
            const String iuid = ins[id::uid];
            if (insertNodes.count (iuid) && insertIdents[iuid] != ins[id::ident].toString())
            {
                graph.removeNode (insertNodes[iuid]->nodeID);
                insertNodes.erase (iuid);
            }
            if (! insertNodes.count (iuid))
                instantiateInsert (ins, currentSR, currentBlock);
            if (insertNodes.count (iuid))
            {
                auto node = insertNodes[iuid];
                node->setBypassed ((bool) ins[id::bypass]);
                tn.inserts.push_back (node);
            }
        }
    }

    // wipe all connections, rewire from the model
    for (const auto& c : graph.getConnections())
        graph.removeConnection (c);

    const int midiCh = juce::AudioProcessorGraph::midiChannelIndex;
    auto stereo = [this] (juce::AudioProcessorGraph::Node::Ptr a, juce::AudioProcessorGraph::Node::Ptr b)
    {
        if (a == nullptr || b == nullptr) return;
        for (int ch = 0; ch < 2; ++ch)
            graph.addConnection ({ { a->nodeID, ch }, { b->nodeID, ch } });
    };
    auto midi = [&] (juce::AudioProcessorGraph::Node::Ptr a, juce::AudioProcessorGraph::Node::Ptr b)
    {
        if (a == nullptr || b == nullptr) return;
        graph.addConnection ({ { a->nodeID, midiCh }, { b->nodeID, midiCh } });
    };

    const int numIns = juce::jmax (0, graph.getTotalNumInputChannels());

    for (const auto& t : trackList)
    {
        const String uid = t[id::uid];
        const String type = t[id::type];
        if (type == "video") continue;
        auto& tn = trackNodes[uid];

        std::vector<juce::AudioProcessorGraph::Node::Ptr> chain;
        if (type == "midi" && tn.instrument != nullptr) chain.push_back (tn.instrument);
        for (auto& n : tn.inserts) chain.push_back (n);
        chain.push_back (tn.strip);

        // head of this track's processing for incoming audio
        auto head = chain.front();

        if (type == "audio" && tn.source != nullptr)
        {
            // device input -> source (for monitoring / recording)
            const int ic = (int) t[id::inputChan];
            const bool st = (bool) t[id::inputStereo];
            if (ic >= 0 && ic < numIns)
            {
                graph.addConnection ({ { audioInNode->nodeID, ic }, { tn.source->nodeID, 0 } });
                const int ic2 = (st && ic + 1 < numIns) ? ic + 1 : ic;
                graph.addConnection ({ { audioInNode->nodeID, ic2 }, { tn.source->nodeID, 1 } });
            }
            stereo (tn.source, head);
            midi (midiInNode, tn.source);
        }
        else if (type == "midi" && tn.source != nullptr)
        {
            midi (midiInNode, tn.source);
            midi (tn.source, tn.instrument);
        }

        // chain links
        for (size_t i = 0; i + 1 < chain.size(); ++i)
            stereo (chain[i], chain[i + 1]);

        // midi fan-out to every rack insert so corruption is MIDI-playable
        for (auto& n : tn.inserts)
            if (dynamic_cast<RackProcessor*> (n->getProcessor()) != nullptr)
                midi (midiInNode, n);
    }

    // routing: strip -> destination, sends, master -> out
    for (const auto& t : trackList)
    {
        const String uid = t[id::uid];
        const String type = t[id::type];
        auto& tn = trackNodes[uid];
        if (type == "video" || tn.strip == nullptr) continue;

        if (type == "master")
        {
            stereo (tn.strip, audioOutNode);
            continue;
        }

        auto dest = getBusHead (type == "bus" ? "master" : t[id::outputBus].toString());
        stereo (tn.strip, dest);

        if (tn.sendA != nullptr)
        {
            if (auto bh = getBusHead (t[id::sendABus].toString()); bh != nullptr && t[id::sendABus].toString().isNotEmpty())
            { stereo (tn.strip, tn.sendA); stereo (tn.sendA, bh); }
            if (auto bh = getBusHead (t[id::sendBBus].toString()); bh != nullptr && t[id::sendBBus].toString().isNotEmpty())
            { stereo (tn.strip, tn.sendB); stereo (tn.sendB, bh); }
        }
    }

    updateTrackFlags();
    updateAllPlaylists();
    rebuildAutomation();
    rebuildMods();
}

juce::AudioProcessorGraph::Node::Ptr AudioEngine::getBusHead (const String& busUid)
{
    auto t = session.findTrack (busUid.isEmpty() ? "master" : busUid);
    if (! t.isValid()) t = session.masterTrack();
    const String uid = t[id::uid];
    if (! trackNodes.count (uid)) return nullptr;
    auto& tn = trackNodes[uid];
    if (! tn.inserts.empty()) return tn.inserts.front();
    return tn.strip;
}

void AudioEngine::instantiateInsert (const ValueTree& insert, double sr, int blockSize)
{
    const String iuid = insert[id::uid];
    const String type = insert[id::type];
    std::unique_ptr<juce::AudioProcessor> proc;

    if (type == "rack")
    {
        auto rack = std::make_unique<RackProcessor>();
        if (insert.hasProperty (id::state))
        {
            juce::MemoryBlock mb;
            if (mb.fromBase64Encoding (insert[id::state].toString()))
                rack->setStateInformation (mb.getData(), (int) mb.getSize());
        }
        proc = std::move (rack);
    }
    else // built-in instrument, hosted plugin, or hosted instrument
    {
        const String ident = insert[id::ident];
        if (ident.startsWith ("builtin:"))
        {
            proc = BuiltinInstrument::create (ident.fromFirstOccurrenceOf ("builtin:", false, false));
            if (proc != nullptr && insert.hasProperty (id::state))
            {
                juce::MemoryBlock mb;
                if (mb.fromBase64Encoding (insert[id::state].toString()))
                    proc->setStateInformation (mb.getData(), (int) mb.getSize());
            }
        }
        String error;
        std::unique_ptr<juce::AudioPluginInstance> inst;
        if (proc == nullptr)
            if (auto desc = pluginHost.findByIdentifier (ident))
                inst = pluginHost.createInstance (*desc, sr, blockSize, error);

        if (proc != nullptr)
        {
            // built-in handled above
        }
        else if (inst != nullptr)
        {
            inst->enableAllBuses();
            if (insert.hasProperty (id::state))
            {
                juce::MemoryBlock mb;
                if (mb.fromBase64Encoding (insert[id::state].toString()))
                    inst->setStateInformation (mb.getData(), (int) mb.getSize());
            }
            proc = std::move (inst);
        }
        else
        {
            juce::MemoryBlock mb;
            mb.fromBase64Encoding (insert[id::state].toString());
            proc = std::make_unique<MissingPluginProcessor> (insert[id::name].toString(), std::move (mb));
        }
    }

    proc->setPlayConfigDetails (2, 2, sr, blockSize);
    if (auto node = graph.addNode (std::move (proc)))
    {
        insertNodes[iuid] = node;
        insertIdents[iuid] = insert[id::ident].toString();
    }
}

void AudioEngine::syncToTree()
{
    // persist modulation centres (knob positions the cables wiggle around)
    {
        auto modsTree = session.mods();
        std::shared_ptr<const ModRTState> mods;
        {
            juce::SpinLock::ScopedLockType sl (modLock);
            mods = pendingMods;
        }
        if (mods != nullptr)
            for (const auto& c : mods->conns)
            {
                auto m = modsTree.getChildWithProperty (id::uid, c.uid);
                if (m.isValid())
                    m.setProperty (id::base, (double) c.base->load(), nullptr);
            }
    }

    for (auto t : session.tracks())
    {
        const String uid = t[id::uid];
        if (! trackNodes.count (uid)) continue;
        auto& tn = trackNodes[uid];
        if (tn.strip != nullptr)
        {
            auto* st = static_cast<ChannelStripProcessor*> (tn.strip->getProcessor());
            t.setProperty (id::gain, (double) st->gainDb->get(), nullptr);
            t.setProperty (id::pan,  (double) st->pan->get(),    nullptr);
        }
        if (tn.sendA != nullptr)
        {
            t.setProperty (id::sendALevel, (double) static_cast<SendProcessor*> (tn.sendA->getProcessor())->levelDb->get(), nullptr);
            t.setProperty (id::sendBLevel, (double) static_cast<SendProcessor*> (tn.sendB->getProcessor())->levelDb->get(), nullptr);
        }
        for (auto ins : t.getChildWithName (id::INSERTS))
        {
            const String iuid = ins[id::uid];
            if (insertNodes.count (iuid))
            {
                juce::MemoryBlock mb;
                insertNodes[iuid]->getProcessor()->getStateInformation (mb);
                if (mb.getSize() > 0)
                    ins.setProperty (id::state, mb.toBase64Encoding(), nullptr);
            }
        }
    }
}

// ============================================================ playlists

void AudioEngine::updateAllPlaylists()
{
    for (const auto& t : session.tracks())
        updateTrackPlaylists (t);
}

void AudioEngine::updateTrackPlaylists (const ValueTree& t)
{
    if (! t.isValid()) return;
    const String uid = t[id::uid];
    if (! trackNodes.count (uid)) return;
    auto& tn = trackNodes[uid];
    if (tn.source == nullptr) return;

    auto map = getTempoMap();
    const String type = t[id::type];

    if (type == "audio")
    {
        auto pl = std::make_shared<AudioPlaylist>();
        for (const auto& c : t.getChildWithName (id::CLIPS))
        {
            if ((int) c.getProperty (id::lane, 0) != 0) continue;   // lanes > 0 are muted takes
            const String cuid = c[id::uid];
            const double fileSR = (double) c.getProperty (id::fileSR, currentSR);
            const double stretchRate = juce::jlimit (0.1, 10.0, (double) c.getProperty (id::stretch, 1.0));

            // pitch-locked stretch: swap to the RubberBand render when it's cached;
            // play varispeed until then (the background render triggers a re-snapshot)
            File f = mediaFileFor (File ((c[id::file]).toString()));
            double offset = (double) c[id::offset];
            double playRatio = (fileSR / currentSR) / stretchRate;
            if ((int) c.getProperty (id::stretchMode, 0) == 1
                && std::abs (stretchRate - 1.0) > 1.0e-3 && stretchCache != nullptr)
            {
                const File stretched = stretchCache->get (f, stretchRate,
                    [safe = juce::WeakReference<AudioEngine> (this), uid]
                    {
                        if (auto* e = safe.get())
                        {
                            e->playlistDirtyTracks.addIfNotAlreadyThere (uid);
                            e->scheduleRebuild (rebuild::playlists);
                        }
                    });
                if (stretched != File())
                {
                    f = stretched;
                    offset *= stretchRate;          // positions scale into the stretched render
                    playRatio = fileSR / currentSR; // duration already baked in
                }
            }

            std::shared_ptr<juce::AudioFormatReader> reader;
            auto cached = readerCache.find (cuid);
            if (cached != readerCache.end() && cached->second.first == f.getFullPathName())
                reader = cached->second.second;
            else if (auto* raw = formatManager.createReaderFor (f))
            {
                auto* buffered = new juce::BufferingAudioReader (raw, diskThread, 1 << 18);
                buffered->setReadTimeout (0);
                reader = std::shared_ptr<juce::AudioFormatReader> (buffered);
                readerCache[cuid] = { f.getFullPathName(), reader };
            }
            if (reader == nullptr) continue;

            AudioClipRT rc;
            rc.start   = secToSamples ((double) c[id::start]);
            rc.length  = secToSamples ((double) c[id::length]);
            rc.offset  = offset;
            rc.gain    = juce::Decibels::decibelsToGain ((float) (double) c.getProperty (id::clipGain, 0.0));
            rc.fadeIn  = secToSamples ((double) c.getProperty (id::fadeIn, 0.0));
            rc.fadeOut = secToSamples ((double) c.getProperty (id::fadeOut, 0.0));
            rc.ratio = playRatio;
            rc.reader = reader;
            rc.numFileChannels = (int) reader->numChannels;
            rc.fileLength = reader->lengthInSamples;
            pl->clips.push_back (rc);
        }

        applyCompCrossfades (pl->clips);
        auto* src = static_cast<ClipPlayerProcessor*> (tn.source->getProcessor());
        // keep the old snapshot alive message-side so the audio thread never
        // holds the last reference (readers must not be destroyed on the audio thread)
        playlistGraveyard.push_back (lastPlaylists[uid]);
        lastPlaylists[uid] = pl;
        src->setPlaylist (pl);
    }
    else if (type == "midi")
    {
        auto pl = std::make_shared<MidiPlaylist>();
        for (const auto& c : t.getChildWithName (id::CLIPS))
        {
            if ((int) c.getProperty (id::lane, 0) != 0) continue;
            const double startSec = (double) c[id::start];
            const double lenSec = (double) c[id::length];
            const double clipStartBeat = map->secondsToBeats (startSec);
            const juce::int64 clipStartSa = secToSamples (startSec);
            const juce::int64 clipEndSa = secToSamples (startSec + lenSec);

            for (const auto& nt : c.getChildWithName (id::NOTES))
            {
                MidiNoteRT n;
                n.on  = map->beatsToSamples (clipStartBeat + (double) nt[id::beat]);
                n.off = map->beatsToSamples (clipStartBeat + (double) nt[id::beat] + (double) nt[id::len]);
                if (n.on >= clipEndSa || n.off <= clipStartSa) continue;
                n.on  = juce::jmax (n.on, clipStartSa);
                n.off = juce::jmin (n.off, clipEndSa);
                n.note = (juce::uint8) (int) nt[id::pitch];
                n.vel  = (juce::uint8) juce::jlimit (1, 127, (int) nt[id::vel]);
                pl->notes.push_back (n);
            }
        }
        std::sort (pl->notes.begin(), pl->notes.end(), [] (auto& a, auto& b) { return a.on < b.on; });
        midiGraveyard.push_back (lastMidiPlaylists[uid]);
        lastMidiPlaylists[uid] = pl;
        static_cast<MidiSourceProcessor*> (tn.source->getProcessor())->setPlaylist (pl);
    }
}

void AudioEngine::updateTrackFlags()
{
    bool anySolo = false;
    for (const auto& t : session.tracks())
        if ((bool) t[id::solo]) anySolo = true;

    std::vector<DirectMonitor> dms;

    for (const auto& t : session.tracks())
    {
        const String uid = t[id::uid];
        const String type = t[id::type];
        if (! trackNodes.count (uid)) continue;
        auto& tn = trackNodes[uid];

        if (tn.strip != nullptr)
        {
            auto* st = static_cast<ChannelStripProcessor*> (tn.strip->getProcessor());
            st->soloMuted = anySolo && ! (bool) t[id::solo] && (type == "audio" || type == "midi");
            const bool m = (bool) t[id::mute];
            if (st->mute->get() != m)
                st->mute->setValueNotifyingHost (m ? 1.0f : 0.0f);
        }
        if (type == "audio" && tn.source != nullptr)
        {
            auto* src = static_cast<ClipPlayerProcessor*> (tn.source->getProcessor());
            src->armed = (bool) t[id::armed];
            src->monitorMode = (int) t[id::monitor];
            if ((bool) t[id::armed] && (int) t[id::monitor] == 1)
                dms.push_back ({ (int) t[id::inputChan], (bool) t[id::inputStereo] });
        }
        if (type == "midi" && tn.source != nullptr)
            static_cast<MidiSourceProcessor*> (tn.source->getProcessor())->armed = (bool) t[id::armed];
    }

    juce::SpinLock::ScopedLockType sl (dmLock);
    dmPending = std::move (dms);
}

void AudioEngine::updateTransportFromTree()
{
    auto tr = session.transport();
    loopStartS = secToSamples ((double) tr[id::loopStart]);
    loopEndS   = secToSamples ((double) tr[id::loopEnd]);
    loopOn     = (bool) tr[id::loopOn];
    punchInS   = secToSamples ((double) tr[id::punchIn]);
    punchOutS  = secToSamples ((double) tr[id::punchOut]);
    punchOn    = (bool) tr[id::punchOn];
    metroOn    = (bool) tr[id::metro];
}

// ============================================================ lookups

ChannelStripProcessor* AudioEngine::getStrip (const String& uid) const
{
    auto it = trackNodes.find (uid);
    if (it == trackNodes.end() || it->second.strip == nullptr) return nullptr;
    return static_cast<ChannelStripProcessor*> (it->second.strip->getProcessor());
}

SendProcessor* AudioEngine::getSend (const String& uid, int which) const
{
    auto it = trackNodes.find (uid);
    if (it == trackNodes.end()) return nullptr;
    auto n = which == 0 ? it->second.sendA : it->second.sendB;
    return n == nullptr ? nullptr : static_cast<SendProcessor*> (n->getProcessor());
}

juce::AudioProcessor* AudioEngine::getInsertProcessor (const String& insertUid) const
{
    auto it = insertNodes.find (insertUid);
    return it == insertNodes.end() ? nullptr : it->second->getProcessor();
}

juce::AudioProcessor* AudioEngine::getInstrumentFor (const String& trackUid) const
{
    auto it = trackNodes.find (trackUid);
    if (it == trackNodes.end() || it->second.instrument == nullptr) return nullptr;
    return it->second.instrument->getProcessor();
}

int AudioEngine::getTotalLatencySamples() const
{
    return graph.getLatencySamples() + ioLatency.load();
}

// ============================================================ automation / modulation

juce::AudioProcessorParameter* AudioEngine::resolveParamTarget (const String& trackUid, const String& target) const
{
    auto it = trackNodes.find (trackUid);
    if (it == trackNodes.end()) return nullptr;
    const auto& tn = it->second;

    if (target.startsWith ("strip:") && tn.strip != nullptr)
    {
        auto* st = static_cast<ChannelStripProcessor*> (tn.strip->getProcessor());
        if (target == "strip:gain") return st->gainDb;
        if (target == "strip:pan")  return st->pan;
        if (target == "strip:mute") return st->mute;
        return nullptr;
    }
    if (target == "send:A" && tn.sendA != nullptr)
        return static_cast<SendProcessor*> (tn.sendA->getProcessor())->levelDb;
    if (target == "send:B" && tn.sendB != nullptr)
        return static_cast<SendProcessor*> (tn.sendB->getProcessor())->levelDb;
    if (target.startsWith ("ins:"))
    {
        const String rest = target.fromFirstOccurrenceOf ("ins:", false, false);
        const String iuid = rest.upToFirstOccurrenceOf (":", false, false);
        const int pidx = rest.fromFirstOccurrenceOf (":", false, false).getIntValue();
        if (auto* proc = getInsertProcessor (iuid))
        {
            const auto& params = proc->getParameters();
            if (pidx >= 0 && pidx < params.size())
                return params[pidx];
        }
    }
    return nullptr;
}

void AudioEngine::rebuildMods()
{
    modListeners.clear();
    auto modsTree = session.mods();
    auto next = std::make_shared<ModRTState>();

    for (int i = 0; i < 4; ++i)
    {
        next->lfoRate[i] = (float) (double) modsTree.getProperty ("lfo" + String (i + 1) + "rate",
                                                                  next->lfoRate[i]);
        next->lfoShape[i] = (int) modsTree.getProperty ("lfo" + String (i + 1) + "shape", 0);
    }
    next->chaosRate = (float) (double) modsTree.getProperty ("chaosRate", 1.0);
    next->follower = getStrip (modsTree.getProperty ("followerTrack", "").toString());

    for (const auto& m : modsTree)
    {
        if (! m.hasType (id::MOD)) continue;
        auto targetNode = modsTree.getChildWithProperty (id::uid, m[id::target]);
        if (! targetNode.isValid()) continue;
        auto* param = resolveParamTarget (targetNode[id::track].toString(),
                                          targetNode[id::param].toString());
        if (param == nullptr) continue;

        ModConn c;
        c.param = param;
        const String src = m[id::src].toString();
        c.src = src == "lfo1" ? 0 : src == "lfo2" ? 1 : src == "lfo3" ? 2 : src == "lfo4" ? 3
              : src == "chaos" ? 4 : 5;
        c.amount = (float) (double) m.getProperty (id::amount, 0.5);
        c.uid = m[id::uid].toString();
        const float baseVal = m.hasProperty (id::base) ? (float) (double) m[id::base]
                                                       : param->getValue();
        c.base = std::make_shared<std::atomic<float>> (baseVal);
        modListeners.push_back (std::make_unique<ModListener> (*this, param, c.base));
        next->conns.push_back (std::move (c));
    }

    juce::SpinLock::ScopedLockType sl (modLock);
    modGraveyard.push_back (pendingMods);
    pendingMods = next;
    if (rtMods == nullptr) rtMods = next;
}

void AudioEngine::applyMods (juce::int64 pos, int numSamples)
{
    {
        juce::SpinLock::ScopedTryLockType tl (modLock);
        if (tl.isLocked() && pendingMods != rtMods)
            rtMods = pendingMods;
    }
    auto mods = rtMods;
    if (mods == nullptr) return;

    const double t = (double) pos / currentSR;
    float vals[kNumModSources] {};

    for (int i = 0; i < 4; ++i)
    {
        const double ph = t * mods->lfoRate[i];
        const double frac = ph - std::floor (ph);
        switch (mods->lfoShape[i])
        {
            case 1:  vals[i] = (float) (2.0 * frac - 1.0); break;                       // saw
            case 2:  vals[i] = frac < 0.5 ? 1.0f : -1.0f; break;                        // square
            case 3:  { const double h = std::sin (std::floor (ph) * 12.9898) * 43758.5453;   // s&h random
                       vals[i] = (float) ((h - std::floor (h)) * 2.0 - 1.0); break; }
            default: vals[i] = (float) std::sin (ph * juce::MathConstants<double>::twoPi); break;
        }
    }

    // Lorenz x, the attractor as a mod source
    {
        const double dt = juce::jlimit (1.0e-5, 0.02, (double) numSamples / currentSR * mods->chaosRate * 4.0);
        for (int step = 0; step < 4; ++step)
        {
            const double h = dt * 0.25;
            const double dx = 10.0 * (lorenz[1] - lorenz[0]);
            const double dy = lorenz[0] * (28.0 - lorenz[2]) - lorenz[1];
            const double dz = lorenz[0] * lorenz[1] - (8.0 / 3.0) * lorenz[2];
            lorenz[0] += dx * h; lorenz[1] += dy * h; lorenz[2] += dz * h;
        }
        vals[4] = juce::jlimit (-1.0f, 1.0f, (float) (lorenz[0] / 20.0));
    }

    if (mods->follower != nullptr)
    {
        const float pk = juce::jmax (mods->follower->peakL.load(), mods->follower->peakR.load());
        followEnv += (pk > followEnv ? 0.5f : 0.02f) * (pk - followEnv);
        vals[5] = juce::jlimit (0.0f, 1.0f, followEnv);
    }

    for (int i = 0; i < kNumModSources; ++i)
        modSrcValues[(size_t) i].store (vals[i]);

    if (mods->conns.empty()) return;
    applyingAutomation = true;
    for (const auto& c : mods->conns)
    {
        const float nv = juce::jlimit (0.0f, 1.0f, c.base->load() + c.amount * vals[c.src]);
        if (std::abs (c.param->getValue() - nv) > 0.0008f)
            c.param->setValueNotifyingHost (nv);
    }
    applyingAutomation = false;
}

void AudioEngine::detachAutomation()
{
    laneListeners.clear();
}

void AudioEngine::rebuildAutomation()
{
    detachAutomation();
    laneTrees.clear();

    auto lanes = std::make_shared<std::vector<LaneRT>>();

    for (const auto& t : session.tracks())
    {
        const String uid = t[id::uid];
        if (! trackNodes.count (uid)) continue;

        for (const auto& lane : t.getChildWithName (id::AUTO))
        {
            auto* param = resolveParamTarget (uid, lane[id::param].toString());
            if (param == nullptr) continue;

            LaneRT rt;
            rt.param = param;
            rt.mode = (int) lane.getProperty (id::mode, 1);
            rt.touching = std::make_shared<std::atomic<bool>> (false);
            for (const auto& pt : lane)
                if (pt.hasType (id::PT))
                    rt.pts.emplace_back (secToSamples ((double) pt[id::t]),
                                         juce::jlimit (0.0f, 1.0f, (float) (double) pt[id::v]));
            std::sort (rt.pts.begin(), rt.pts.end());

            const int laneIdx = (int) lanes->size();
            lanes->push_back (std::move (rt));
            laneTrees.push_back (lane);
            laneListeners.push_back (std::make_unique<LaneListener> (*this, laneIdx, param, lanes->back().touching));
        }
    }

    bool anyActive = false;
    for (const auto& l : *lanes)
        if ((l.mode == 1 || l.mode == 2) && ! l.pts.empty())
            anyActive = true;
    automationActive = anyActive;

    juce::SpinLock::ScopedLockType sl (laneLock);
    laneGraveyard.push_back (pendingLanes);
    pendingLanes = lanes;
    if (rtLanes == nullptr) rtLanes = lanes;
}

void AudioEngine::applyAutomation (juce::int64 pos)
{
    {
        juce::SpinLock::ScopedTryLockType tl (laneLock);
        if (tl.isLocked() && pendingLanes != rtLanes)
            rtLanes = pendingLanes;
    }
    if (rtLanes == nullptr) return;

    applyingAutomation = true;
    for (const auto& lane : *rtLanes)
    {
        if (lane.pts.empty() || lane.param == nullptr) continue;
        const bool apply = lane.mode == 1 || (lane.mode == 2 && ! lane.touching->load());
        if (! apply) continue;

        float v;
        auto it = std::upper_bound (lane.pts.begin(), lane.pts.end(),
                                    std::make_pair (pos, std::numeric_limits<float>::max()));
        if (it == lane.pts.begin()) v = it->second;
        else if (it == lane.pts.end()) v = std::prev (it)->second;
        else
        {
            auto p0 = std::prev (it);
            const double span = (double) (it->first - p0->first);
            const double a = span > 0 ? (double) (pos - p0->first) / span : 0.0;
            v = (float) (p0->second + a * (it->second - p0->second));
        }
        if (std::abs (lane.param->getValue() - v) > 0.0005f)
            lane.param->setValueNotifyingHost (v);
    }
    applyingAutomation = false;
}

void AudioEngine::automationParamChanged (int laneIdx, float v)
{
    if (applyingAutomation.load() || ! transportPlaying.load()) return;
    std::shared_ptr<const std::vector<LaneRT>> lanes;
    {
        juce::SpinLock::ScopedLockType sl (laneLock);
        lanes = pendingLanes;
    }
    if (lanes == nullptr || laneIdx >= (int) lanes->size()) return;
    const auto& lane = (*lanes)[laneIdx];
    if (lane.mode == 3 || (lane.mode == 2 && lane.touching->load()))
    {
        const juce::ScopedLock sl (writeLock);
        if (writeEvents.size() < 1 << 16)
            writeEvents.push_back ({ laneIdx, transportPos.load(), v });
    }
}

std::vector<AudioEngine::AutoWrite> AudioEngine::drainAutomationWrites()
{
    std::vector<WriteEvt> evts;
    {
        const juce::ScopedLock sl (writeLock);
        evts.swap (writeEvents);
    }
    std::vector<AutoWrite> out;
    for (const auto& e : evts)
        if (e.laneIdx < (int) laneTrees.size())
            out.push_back ({ laneTrees[(size_t) e.laneIdx], samplesToSec (e.t), e.v });
    return out;
}

// ============================================================ transport

void AudioEngine::play()
{
    if (transportPlaying.load()) return;
    if (recordPending.load() || transportRecording.load())
    {
        createRecordSessions();
        transportRecording = true;
        recordPending = false;
    }
    flushMidiPending = true;
    transportPlaying = true;
    if (onTransportStateChanged) onTransportStateChanged();
}

void AudioEngine::stop()
{
    const bool wasRecording = transportRecording.exchange (false);
    transportPlaying = false;
    recordPending = false;
    flushMidiPending = true;
    if (wasRecording)
        finalizeRecordings();
    if (onTransportStateChanged) onTransportStateChanged();
}

void AudioEngine::togglePlayStop()
{
    if (transportPlaying.load()) stop();
    else play();
}

void AudioEngine::toggleRecord()
{
    if (transportPlaying.load())
    {
        if (transportRecording.load())
        {
            transportRecording = false;
            finalizeRecordings();
        }
        else
        {
            createRecordSessions();
            transportRecording = true;
        }
    }
    else
        recordPending = ! recordPending.load();
    if (onTransportStateChanged) onTransportStateChanged();
}

void AudioEngine::seekSeconds (double sec)
{
    transportPos = juce::jmax ((juce::int64) 0, secToSamples (sec));
    flushMidiPending = true;
}

// ============================================================ recording

void AudioEngine::createRecordSessions()
{
    auto dir = session.assetsDir();
    dir.createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%y%m%d-%H%M%S");
    recStartSec = getPositionSeconds();
    anyArmedMidiAtRecord = false;
    {
        juce::SpinLock::ScopedLockType sl (midiRecLock);
        midiRecEvents.clear();
        midiRecEvents.reserve (1 << 14);
    }

    for (const auto& t : session.tracks())
    {
        const String uid = t[id::uid];
        const String type = t[id::type];
        if (! (bool) t[id::armed] || ! trackNodes.count (uid)) continue;
        if (type == "midi") { anyArmedMidiAtRecord = true; continue; }
        if (type != "audio") continue;

        auto& tn = trackNodes[uid];
        if (tn.source == nullptr) continue;

        auto sess = std::make_unique<RecordSession>();
        sess->trackUid = uid;
        sess->sampleRate = currentSR;
        sess->file = dir.getChildFile (t[id::name].toString().replaceCharacters (" /\\:", "----")
                                       + "-" + stamp + ".wav").getNonexistentSibling();
        sess->passes.reserve (512);
        sess->peakCapacity = 1 << 19;                  // ~3h of live waveform at 48k
        sess->peaks.calloc ((size_t) sess->peakCapacity);

        if (auto out = sess->file.createOutputStream())
        {
            juce::WavAudioFormat wav;
            if (auto* writer = wav.createWriterFor (out.get(), currentSR, 2, 32, {}, 0))
            {
                out.release();
                sess->writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer, recordThread, 1 << 17);
            }
        }
        if (sess->writer == nullptr) continue;

        static_cast<ClipPlayerProcessor*> (tn.source->getProcessor())->rec.store (sess.get());
        recordSessions.push_back (std::move (sess));
    }
}

void AudioEngine::finalizeRecordings()
{
    for (auto& [uid, tn] : trackNodes)
        if (tn.source != nullptr)
            if (auto* cp = dynamic_cast<ClipPlayerProcessor*> (tn.source->getProcessor()))
                cp->rec.store (nullptr);

    // let in-flight audio blocks finish before tearing writers down
    juce::Timer::callAfterDelay (80, [this, sessions = std::make_shared<std::vector<std::unique_ptr<RecordSession>>>
                                            (std::move (recordSessions))]
    {
        session.undo.beginNewTransaction ("record");
        for (auto& s : *sessions)
            finalizeAudioSession (*s);
        if (anyArmedMidiAtRecord)
            finalizeMidiRecording();
        if (onRecordingFinished) onRecordingFinished();
    });
    recordSessions.clear();
}

void AudioEngine::finalizeAudioSession (RecordSession& s)
{
    s.writer.reset();                       // flush + close
    const juce::int64 total = s.written.load();
    if (total <= 0) { s.file.deleteFile(); return; }

    auto track = session.findTrack (s.trackUid);
    if (! track.isValid()) return;

    const double latComp = (double) getTotalLatencySamples() / s.sampleRate;
    const int passCount = (int) s.passes.size();
    const double regionStart = juce::jmax (0.0, s.passes.front().timelineStartSec - latComp);
    const double regionEnd = s.passes.back().timelineStartSec
                             + (double) (total - s.passes.back().fileOffset) / s.sampleRate;

    // newest pass takes lane 0 (audible); older overlapping material becomes takes.
    // EXTEND: crossfade comping between take lanes.
    for (auto c : SessionModel::clipsOf (track))
        if ((double) c[id::start] < regionEnd && (double) c[id::start] + (double) c[id::length] > regionStart)
            c.setProperty (id::lane, (int) c.getProperty (id::lane, 0) + passCount, &session.undo);

    for (size_t k = 0; k < s.passes.size(); ++k)
    {
        const juce::int64 off = s.passes[k].fileOffset;
        const juce::int64 end = (k + 1 < s.passes.size()) ? s.passes[k + 1].fileOffset : total;
        if (end <= off) continue;

        const double startSec = juce::jmax (0.0, s.passes[k].timelineStartSec - latComp);
        const double lenSec = (double) (end - off) / s.sampleRate;
        const int lane = passCount - 1 - (int) k;

        auto c = session.addAudioClip (track, s.file, startSec, lenSec, s.sampleRate, lane);
        c.setProperty (id::offset, (double) off, &session.undo);
        c.setProperty (id::name, s.file.getFileNameWithoutExtension() + (passCount > 1 ? " take " + String ((int) k + 1) : ""), &session.undo);
    }
}

void AudioEngine::finalizeMidiRecording()
{
    std::vector<TimedMidi> evts;
    {
        juce::SpinLock::ScopedLockType sl (midiRecLock);
        evts.swap (midiRecEvents);
    }
    if (evts.empty()) return;

    auto map = getTempoMap();
    const double recEndSec = getPositionSeconds();

    for (auto track : session.tracks())
    {
        if (track[id::type].toString() != "midi" || ! (bool) track[id::armed]) continue;

        // find/merge an overlapping lane-0 clip when overdubbing, else create one
        ValueTree clip;
        if (overdubMidi.load())
            for (auto c : SessionModel::clipsOf (track))
                if ((int) c.getProperty (id::lane, 0) == 0
                    && (double) c[id::start] < recEndSec
                    && (double) c[id::start] + (double) c[id::length] > recStartSec)
                { clip = c; break; }

        if (! clip.isValid())
            clip = session.addMidiClip (track, recStartSec, juce::jmax (0.5, recEndSec - recStartSec));

        const double clipStartBeat = map->secondsToBeats ((double) clip[id::start]);
        auto notes = clip.getOrCreateChildWithName (id::NOTES, &session.undo);

        std::map<int, std::pair<double, int>> open;     // note -> (beat, vel)
        auto addNote = [&] (int note, double onBeat, double offBeat, int vel)
        {
            ValueTree n (id::NOTE);
            n.setProperty (id::pitch, note, nullptr);
            n.setProperty (id::beat, juce::jmax (0.0, onBeat - clipStartBeat), nullptr);
            n.setProperty (id::len, juce::jmax (0.05, offBeat - onBeat), nullptr);
            n.setProperty (id::vel, vel, nullptr);
            notes.appendChild (n, &session.undo);
        };

        for (const auto& e : evts)
        {
            const double beat = map->samplesToBeats (e.pos);
            if (e.msg.isNoteOn())
                open[e.msg.getNoteNumber()] = { beat, e.msg.getVelocity() };
            else if (e.msg.isNoteOff())
            {
                auto it = open.find (e.msg.getNoteNumber());
                if (it != open.end())
                {
                    addNote (e.msg.getNoteNumber(), it->second.first, beat, it->second.second);
                    open.erase (it);
                }
            }
        }
        const double endBeat = map->secondsToBeats (recEndSec);
        for (auto& [note, ob] : open)
            addNote (note, ob.first, endBeat, ob.second);

        // grow the clip to cover everything recorded
        const double needLen = recEndSec - (double) clip[id::start];
        if ((double) clip[id::length] < needLen)
            clip.setProperty (id::length, needLen, &session.undo);
    }
}

// ============================================================ device callback

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    currentSR = device->getCurrentSampleRate();
    currentBlock = device->getCurrentBufferSizeSamples();
    ioLatency = device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();

    const int numIn  = device->getActiveInputChannels().countNumberOfSetBits();
    const int numOut = device->getActiveOutputChannels().countNumberOfSetBits();
    const int chans = juce::jmax (2, numIn, numOut);

    scratch.setSize (chans, currentBlock);
    segPtrs.malloc ((size_t) chans);
    midiCollector.reset (currentSR);

    graph.setPlayConfigDetails (chans, chans, currentSR, currentBlock);
    graph.prepareToPlay (currentSR, currentBlock);

    // sample-rate-dependent RT data must be rebuilt on the message thread
    juce::MessageManager::callAsync ([safe = juce::WeakReference<AudioEngine> (this)]
    {
        if (auto* e = safe.get())
        {
            e->rebuildTempoMap();
            e->updateAllPlaylists();
            e->rebuildAutomation();
            e->updateTransportFromTree();
        }
    });
}

void AudioEngine::audioDeviceStopped()
{
    graph.releaseResources();
}

void AudioEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    midiCollector.addMessageToQueue (m);
    juce::SpinLock::ScopedTryLockType tl (uiMidiLock);
    if (tl.isLocked() && uiMidi.size() < 256)
        uiMidi.push_back (m);
}

std::vector<juce::MidiMessage> AudioEngine::drainUiMidi()
{
    std::vector<juce::MidiMessage> out;
    juce::SpinLock::ScopedLockType sl (uiMidiLock);
    out.swap (uiMidi);
    return out;
}

void AudioEngine::setPlayheadAtomics (juce::int64 pos, bool playing)
{
    segPos = pos;
    segPlaying = playing;
    // punch-out at or before punch-in means "open-ended": record from punch-in onward
    const juce::int64 pIn = punchInS.load(), pOut = punchOutS.load();
    segRecEnabled = playing && transportRecording.load()
                    && (! punchOn.load() || (pos >= pIn && (pOut <= pIn || pos < pOut)));
}

const RecordSession* AudioEngine::getLiveRecording (const String& trackUid) const
{
    for (const auto& s : recordSessions)
        if (s->trackUid == trackUid)
            return s.get();
    return nullptr;
}

juce::Optional<juce::AudioPlayHead::PositionInfo> AudioEngine::getPosition() const
{
    auto map = rtMap;
    if (map == nullptr) return {};

    juce::AudioPlayHead::PositionInfo info;
    const juce::int64 pos = segPos.load();
    const double ppq = map->samplesToBeats (pos);
    auto bb = map->barBeatAt (ppq);

    info.setTimeInSamples (pos);
    info.setTimeInSeconds ((double) pos / currentSR);
    info.setBpm (map->bpmAtBeat (ppq));
    info.setPpqPosition (ppq);
    info.setPpqPositionOfLastBarStart (bb.barStartBeat);
    info.setBarCount (bb.bar);
    info.setTimeSignature (juce::AudioPlayHead::TimeSignature { bb.num, bb.den });
    info.setIsPlaying (segPlaying.load());
    info.setIsRecording (transportRecording.load());
    info.setIsLooping (loopOn.load());
    info.setLoopPoints (juce::AudioPlayHead::LoopPoints {
        map->samplesToBeats (loopStartS.load()), map->samplesToBeats (loopEndS.load()) });
    return info;
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* input, int numIn,
                                                    float* const* output, int numOut, int n,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;

    {
        juce::SpinLock::ScopedTryLockType tl (mapLock);
        if (tl.isLocked() && pendingMap != nullptr && pendingMap != rtMap)
            rtMap = pendingMap;
    }
    auto map = rtMap;

    const int chans = scratch.getNumChannels();
    if (map == nullptr || n > scratch.getNumSamples() || chans < juce::jmax (numOut, 2))
    {
        for (int ch = 0; ch < numOut; ++ch)
            juce::FloatVectorOperations::clear (output[ch], n);
        return;
    }

    for (int ch = 0; ch < chans; ++ch)
    {
        if (ch < numIn) scratch.copyFrom (ch, 0, input[ch], n);
        else scratch.clear (ch, 0, n);
    }

    midiIn.clear();
    midiCollector.removeNextBlockOfMessages (midiIn, n);
    flushMidiCurrent = flushMidiPending.exchange (false);

    {
        juce::SpinLock::ScopedTryLockType tl (dmLock);
        if (tl.isLocked()) dmRT = dmPending;
    }

    juce::int64 pos = transportPos.load();
    bool playing = transportPlaying.load();
    const bool recording = transportRecording.load();
    const juce::int64 le = loopEndS.load(), ls = loopStartS.load();
    const bool looping = loopOn.load() && le > ls;

    int done = 0, remaining = n;
    while (remaining > 0)
    {
        int len = remaining;
        if (playing)
        {
            if (looping && pos < le)
                len = (int) juce::jmin ((juce::int64) len, le - pos);
            if (recording && punchOn.load())
                for (juce::int64 p : { punchInS.load(), punchOutS.load() })
                    if (pos < p && p < pos + len)
                        len = (int) (p - pos);
            if (automationActive.load())
                len = juce::jmin (len, 256);    // <=5ms automation resolution at 48k
            // EXTEND: per-sample automation ramps inside the parameters themselves

            // sample-accurate session launches: split at scheduled action times
            {
                juce::SpinLock::ScopedTryLockType tl (sessActLock);
                if (tl.isLocked())
                    for (const auto& s : sessActions)
                        if (pos < s.when && s.when < pos + len)
                            len = (int) (s.when - pos);
            }
        }

        setPlayheadAtomics (pos, playing);
        if (playing)
            applySessionActions (pos);
        applyAutomation (pos);
        applyMods (pos, len);

        for (int ch = 0; ch < chans; ++ch)
            segPtrs[ch] = scratch.getWritePointer (ch) + done;
        juce::AudioBuffer<float> sub (segPtrs.getData(), chans, len);

        midiSeg.clear();
        for (const auto meta : midiIn)
            if (meta.samplePosition >= done && meta.samplePosition < done + len)
                midiSeg.addEvent (meta.getMessage(), meta.samplePosition - done);

        if (recording && anyArmedMidiAtRecord && playing)
        {
            juce::SpinLock::ScopedTryLockType tl (midiRecLock);
            if (tl.isLocked())
                for (const auto meta : midiSeg)
                    if (midiRecEvents.size() < midiRecEvents.capacity())
                        midiRecEvents.push_back ({ pos + meta.samplePosition, meta.getMessage() });
        }

        graph.processBlock (sub, midiSeg);

        if (playing && metroOn.load())
            renderMetronome (sub, *map, pos);

        if (playing)
        {
            pos += len;
            if (looping && pos >= le)
            {
                pos = ls;
                flushMidiPending = true;
                for (auto& s : recordSessions)
                    s->needPassMark = true;
            }
        }
        done += len;
        remaining -= len;
        flushMidiCurrent = false;       // only the first segment flushes
    }
    transportPos = pos;

    // direct (dry) input monitoring, post-graph
    for (const auto& dm : dmRT)
    {
        if (dm.chan < 0 || dm.chan >= numIn) continue;
        scratch.addFrom (0, 0, input[dm.chan], n);
        const int c2 = (dm.stereo && dm.chan + 1 < numIn) ? dm.chan + 1 : dm.chan;
        if (chans > 1) scratch.addFrom (1, 0, input[c2], n);
    }

    if (auto* cap = masterCapture.load())
    {
        const float* ptrs[2] = { scratch.getReadPointer (0), scratch.getReadPointer (chans > 1 ? 1 : 0) };
        cap->write (ptrs, n);
    }

    for (int ch = 0; ch < numOut; ++ch)
        juce::FloatVectorOperations::copy (output[ch], scratch.getReadPointer (juce::jmin (ch, chans - 1)), n);
}

void AudioEngine::renderMetronome (juce::AudioBuffer<float>& buf, const TempoMap& map, juce::int64 pos)
{
    const int n = buf.getNumSamples();
    const double beatUnit = 4.0 / map.sigAtBeat (map.samplesToBeats (pos)).den;
    double b = std::ceil (map.samplesToBeats (pos) / beatUnit - 1.0e-6) * beatUnit;

    for (int guard = 0; guard < 32; ++guard, b += beatUnit)
    {
        const juce::int64 bs = map.beatsToSamples (b);
        if (bs >= pos + n) break;
        if (bs < pos) continue;
        auto& c = clicks[nextClick];
        nextClick = (nextClick + 1) % 4;
        const bool downbeat = map.barBeatAt (b).beatInBar < 1.0e-6;
        c.phase = 0;
        c.inc = (downbeat ? 1568.0 : 1046.5) / currentSR;
        c.env = 0.5f;
        c.active = (int) (bs - pos) + 1;     // offset+1; rendered below
    }

    for (auto& c : clicks)
    {
        if (c.active <= 0) continue;
        int i = c.active - 1;
        c.active = 0;
        for (; i < n && c.env > 0.0005f; ++i)
        {
            const float s = (float) std::sin (c.phase * juce::MathConstants<double>::twoPi) * c.env;
            c.phase += c.inc;
            c.env *= 0.9988f;
            for (int ch = 0; ch < juce::jmin (2, buf.getNumChannels()); ++ch)
                buf.addSample (ch, i, s);
        }
        // EXTEND: carry click tails across blocks (currently truncated at block end)
    }
}

// ============================================================ session view

juce::int64 AudioEngine::nextLaunchBoundary()
{
    auto map = getTempoMap();
    const juce::int64 pos = transportPos.load();
    if (! transportPlaying.load()) return pos;
    const double q = launchQuantizeBeats.load();
    if (q <= 0.001) return pos;
    const double ppq = map->samplesToBeats (pos);
    const double next = std::ceil (ppq / q - 1.0e-6) * q;
    juce::int64 b = map->beatsToSamples (next);
    if (b < pos) b = map->beatsToSamples (next + q);
    return b;
}

void AudioEngine::scheduleSessionAction (SessionAction act)
{
    juce::SpinLock::ScopedLockType sl (sessActLock);
    sessActions.erase (std::remove_if (sessActions.begin(), sessActions.end(),
        [&] (const SessionAction& s) { return s.a == act.a && s.m == act.m; }), sessActions.end());
    if (sessActions.size() < 64)
        sessActions.push_back (act);
}

void AudioEngine::launchSlot (ValueTree track, ValueTree clip)
{
    const String uid = track[id::uid];
    auto it = trackNodes.find (uid);
    if (it == trackNodes.end() || it->second.source == nullptr || ! clip.isValid()) return;
    auto& tn = it->second;
    auto map = getTempoMap();
    const String type = track[id::type];

    SessionAction act;
    act.when = nextLaunchBoundary();

    if (type == "audio")
    {
        auto* src = dynamic_cast<ClipPlayerProcessor*> (tn.source->getProcessor());
        if (src == nullptr) return;

        const File f = mediaFileFor (File (clip[id::file].toString()));
        const String cuid = clip[id::uid];
        std::shared_ptr<juce::AudioFormatReader> reader;
        auto cached = readerCache.find (cuid);
        if (cached != readerCache.end() && cached->second.first == f.getFullPathName())
            reader = cached->second.second;
        else if (auto* raw = formatManager.createReaderFor (f))
        {
            auto* buffered = new juce::BufferingAudioReader (raw, diskThread, 1 << 18);
            buffered->setReadTimeout (0);
            reader = std::shared_ptr<juce::AudioFormatReader> (buffered);
            readerCache[cuid] = { f.getFullPathName(), reader };
        }
        if (reader == nullptr) return;

        auto pl = std::make_shared<AudioPlaylist>();
        AudioClipRT rc;
        rc.start = 0;
        rc.length = juce::jmax ((juce::int64) 64, secToSamples ((double) clip[id::length]));
        rc.offset = (double) clip[id::offset];
        rc.gain = juce::Decibels::decibelsToGain ((float) (double) clip.getProperty (id::clipGain, 0.0));
        const double fileSR = (double) clip.getProperty (id::fileSR, currentSR);
        const double stretchRate = juce::jlimit (0.1, 10.0, (double) clip.getProperty (id::stretch, 1.0));
        rc.ratio = (fileSR / currentSR) / stretchRate;   // EXTEND: pitch-locked session loops via StretchCache
        rc.reader = reader;
        rc.numFileChannels = (int) reader->numChannels;
        rc.fileLength = reader->lengthInSamples;
        pl->clips.push_back (rc);

        act.loopLen = rc.length;
        playlistGraveyard.push_back (lastPlaylists["sess:" + uid]);
        lastPlaylists["sess:" + uid] = pl;
        src->setSessionClip (pl);
        act.a = src;
    }
    else if (type == "midi")
    {
        auto* src = dynamic_cast<MidiSourceProcessor*> (tn.source->getProcessor());
        if (src == nullptr) return;

        const double launchBeat = map->samplesToBeats (act.when);
        const double spb = 60.0 / map->bpmAtBeat (launchBeat) * currentSR;
        const double loopBeats = juce::jmax (1.0, (double) clip.getProperty (id::loopBeats, 4.0));
        act.loopLen = (juce::int64) std::llround (loopBeats * spb);

        auto pl = std::make_shared<MidiPlaylist>();
        for (const auto& nt : clip.getChildWithName (id::NOTES))
        {
            MidiNoteRT n;
            n.on  = (juce::int64) std::llround ((double) nt[id::beat] * spb);
            n.off = (juce::int64) std::llround (((double) nt[id::beat] + (double) nt[id::len]) * spb);
            if (n.on >= act.loopLen) continue;
            n.off = juce::jmin (n.off, act.loopLen);
            n.note = (juce::uint8) (int) nt[id::pitch];
            n.vel  = (juce::uint8) juce::jlimit (1, 127, (int) nt[id::vel]);
            pl->notes.push_back (n);
        }
        std::sort (pl->notes.begin(), pl->notes.end(), [] (auto& x, auto& y) { return x.on < y.on; });

        midiGraveyard.push_back (lastMidiPlaylists["sess:" + uid]);
        lastMidiPlaylists["sess:" + uid] = pl;
        src->setSessionClip (pl);
        act.m = src;
    }
    else
        return;

    scheduleSessionAction (act);
    auto& st = sessUI[uid];
    st.pending = clip[id::uid].toString();
    st.pendingStop = false;
    st.when = act.when;
    if (! transportPlaying.load()) play();
}

void AudioEngine::stopTrackSession (const String& uid)
{
    auto it = trackNodes.find (uid);
    if (it == trackNodes.end() || it->second.source == nullptr) return;

    SessionAction act;
    act.stop = true;
    act.when = nextLaunchBoundary();
    act.a = dynamic_cast<ClipPlayerProcessor*> (it->second.source->getProcessor());
    act.m = dynamic_cast<MidiSourceProcessor*> (it->second.source->getProcessor());
    if (act.a == nullptr && act.m == nullptr) return;

    scheduleSessionAction (act);
    auto& st = sessUI[uid];
    st.pending.clear();
    st.pendingStop = true;
    st.when = act.when;
}

void AudioEngine::stopAllSession()
{
    for (const auto& t : session.tracks())
    {
        const String type = t[id::type];
        if (type == "audio" || type == "midi")
            stopTrackSession (t[id::uid].toString());
    }
}

double AudioEngine::getSessionLoopPhase (const String& uid) const
{
    auto it = trackNodes.find (uid);
    if (it == trackNodes.end() || it->second.source == nullptr) return -1.0;
    auto* proc = it->second.source->getProcessor();

    if (auto* a = dynamic_cast<ClipPlayerProcessor*> (proc))
    {
        if (! a->sessEngaged.load() || a->sessLen.load() <= 0) return -1.0;
        juce::int64 local = (transportPos.load() - a->sessStart.load()) % a->sessLen.load();
        if (local < 0) local += a->sessLen.load();
        return (double) local / (double) a->sessLen.load();
    }
    if (auto* m = dynamic_cast<MidiSourceProcessor*> (proc))
    {
        if (! m->sessEngaged.load() || m->sessLen.load() <= 0) return -1.0;
        juce::int64 local = (transportPos.load() - m->sessStart.load()) % m->sessLen.load();
        if (local < 0) local += m->sessLen.load();
        return (double) local / (double) m->sessLen.load();
    }
    return -1.0;
}

AudioEngine::SlotState AudioEngine::getSessionState (const String& uid)
{
    auto& st = sessUI[uid];
    if ((st.pending.isNotEmpty() || st.pendingStop) && st.when > 0 && transportPos.load() >= st.when)
    {
        if (st.pendingStop) st.playing.clear();
        else st.playing = st.pending;
        st.pending.clear();
        st.pendingStop = false;
        st.when = 0;
    }
    return { st.playing, st.pending };
}

void AudioEngine::applySessionActions (juce::int64 pos)
{
    juce::SpinLock::ScopedTryLockType tl (sessActLock);
    if (! tl.isLocked()) return;
    for (int i = (int) sessActions.size(); --i >= 0;)
    {
        auto& s = sessActions[(size_t) i];
        if (pos < s.when) continue;
        if (s.a != nullptr)
        {
            if (s.stop) s.a->sessEngaged = false;
            else { s.a->sessStart = s.when; s.a->sessLen = s.loopLen; s.a->sessEngaged = true; }
        }
        if (s.m != nullptr)
        {
            if (s.stop) s.m->sessEngaged = false;
            else { s.m->sessStart = s.when; s.m->sessLen = s.loopLen; s.m->sessEngaged = true; }
        }
        sessActions.erase (sessActions.begin() + i);
    }
}

// ============================================================ media bridge

File AudioEngine::mediaFileFor (const File& src)
{
    if (! src.existsAsFile())
        return src;

    const String key = src.getFullPathName() + "|"
                     + String (src.getLastModificationTime().toMilliseconds());
    auto hit = bridgeCache.find (key);
    if (hit != bridgeCache.end())
        return File (hit->second);

    {
        std::unique_ptr<juce::AudioFormatReader> probe (formatManager.createReaderFor (src));
        if (probe != nullptr)
        {
            bridgeCache[key] = src.getFullPathName();
            return src;
        }
    }

    // ffmpeg fallback: transcode once into a temp cache, reuse forever.
    // EXTEND: background transcode with a placeholder clip for very long files.
    auto dir = File::getSpecialLocation (File::tempDirectory)
                   .getChildFile (String (names::appName) + "-transcode");
    dir.createDirectory();
    const File cached = dir.getChildFile (String::toHexString (key.hashCode64()) + ".wav");

    if (! cached.existsAsFile())
    {
        juce::ChildProcess proc;
        const juce::StringArray args { "ffmpeg", "-y", "-loglevel", "error",
                                       "-i", src.getFullPathName(),
                                       "-vn", "-acodec", "pcm_f32le",
                                       cached.getFullPathName() };
        const bool ok = proc.start (args)
                        && proc.waitForProcessToFinish (180000)
                        && proc.getExitCode() == 0
                        && cached.getSize() > 64;
        if (! ok)
        {
            cached.deleteFile();
            return src;          // caller's reader open fails and reports
        }
    }
    bridgeCache[key] = cached.getFullPathName();
    return cached;
}

std::unique_ptr<juce::AudioFormatReader> AudioEngine::createAnyReader (const File& src)
{
    return std::unique_ptr<juce::AudioFormatReader> (formatManager.createReaderFor (mediaFileFor (src)));
}

// ============================================================ preview

void AudioEngine::startPreview (const File& f)
{
    stopPreview();
    if (auto* reader = formatManager.createReaderFor (mediaFileFor (f)))
    {
        previewSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
        previewTransport.setSource (previewSource.get(), 1 << 15, &diskThread, reader->sampleRate);
        previewTransport.setPosition (0.0);
        previewTransport.start();
        previewFile = f;
    }
}

void AudioEngine::stopPreview()
{
    previewTransport.stop();
    previewTransport.setSource (nullptr);
    previewSource.reset();
    previewFile = File();
}

// ============================================================ capture & offline

bool AudioEngine::startMasterCapture (const File& f)
{
    stopMasterCapture();
    if (auto out = f.createOutputStream())
    {
        juce::WavAudioFormat wav;
        if (auto* writer = wav.createWriterFor (out.get(), currentSR, 2, 32, {}, 0))
        {
            out.release();
            masterCaptureOwned = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer, recordThread, 1 << 17);
            masterCapture.store (masterCaptureOwned.get());
            return true;
        }
    }
    return false;
}

void AudioEngine::stopMasterCapture()
{
    if (masterCapture.exchange (nullptr) != nullptr)
    {
        auto owned = std::move (masterCaptureOwned);
        juce::Timer::callAfterDelay (80, [w = std::shared_ptr<juce::AudioFormatWriter::ThreadedWriter> (std::move (owned))] () mutable
        { w.reset(); });
    }
}

void AudioEngine::beginOffline (double sr, int blockSize)
{
    deviceManager.removeAudioCallback (this);
    offline = true;
    currentSR = sr;
    currentBlock = blockSize;
    rebuildTempoMap();
    updateAllPlaylists();
    rebuildAutomation();
    updateTransportFromTree();
    {
        juce::SpinLock::ScopedLockType sl (mapLock);
        rtMap = pendingMap;
    }
    for (auto* node : graph.getNodes())
        node->getProcessor()->setNonRealtime (true);
    graph.setPlayConfigDetails (2, 2, sr, blockSize);
    graph.prepareToPlay (sr, blockSize);
}

void AudioEngine::processOffline (juce::AudioBuffer<float>& buf, juce::int64 posSamples)
{
    buf.clear();
    const int n = buf.getNumSamples();
    const int chans = buf.getNumChannels();
    const int step = automationActive.load() ? 256 : n;

    for (int done = 0; done < n; done += step)
    {
        const int len = juce::jmin (step, n - done);
        setPlayheadAtomics (posSamples + done, true);
        applyAutomation (posSamples + done);
        applyMods (posSamples + done, len);
        float* ptrs[8];
        for (int ch = 0; ch < juce::jmin (8, chans); ++ch)
            ptrs[ch] = buf.getWritePointer (ch) + done;
        juce::AudioBuffer<float> sub (ptrs, juce::jmin (8, chans), len);
        juce::MidiBuffer mb;
        graph.processBlock (sub, mb);
    }
}

void AudioEngine::endOffline()
{
    for (auto* node : graph.getNodes())
        node->getProcessor()->setNonRealtime (false);
    setStemSolo ({});
    offline = false;
    deviceManager.addAudioCallback (this);   // re-prepares at device rates
}

void AudioEngine::setStemSolo (const String& uid)
{
    for (const auto& t : session.tracks())
    {
        const String tuid = t[id::uid];
        const String type = t[id::type];
        if (! trackNodes.count (tuid) || trackNodes[tuid].strip == nullptr) continue;
        auto* st = static_cast<ChannelStripProcessor*> (trackNodes[tuid].strip->getProcessor());
        st->forceMute = uid.isNotEmpty() && tuid != uid && (type == "audio" || type == "midi");
    }
}

// ============================================================ housekeeping

void AudioEngine::timerCallback()
{
    // keep all MIDI inputs live
    for (const auto& dev : juce::MidiInput::getAvailableDevices())
        if (! deviceManager.isMidiInputDeviceEnabled (dev.identifier))
            deviceManager.setMidiInputDeviceEnabled (dev.identifier, true);

    auto purge = [] (auto& graveyard)
    {
        graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                         [] (const auto& p) { return p == nullptr || p.use_count() == 1; }),
                         graveyard.end());
    };
    purge (playlistGraveyard);
    purge (midiGraveyard);
    purge (mapGraveyard);
    purge (laneGraveyard);
    purge (modGraveyard);
}

} // namespace dg
