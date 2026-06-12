#include "PatcherProcessor.h"
#include "PatcherEditor.h"
#include "../Model/Session.h"

namespace dg
{

static const Identifier kNodeId ("NODE"), kCableId ("CABLE");
static const Identifier kArgs ("args"), kSrcPort ("srcPort"), kDstPort ("dstPort"), kDst ("dst");

const std::vector<PatcherProcessor::Spec>& PatcherProcessor::specs()
{
    static const std::vector<Spec> s = {
        { "adc~", oAdc, 0, 2, "", "track audio in (L R)", famSource, "", "ss" },
        { "dac~", oDac, 2, 0, "", "to the track output", famSource, "ss", "" },
        { "chan~", oChan, 0, 2, "1", "tap any channel (track#/name, 'pre')", famSource, "", "ss" },
        { "sample~", oSample, 3, 2, "", "buffer player (trig, rate, pos 0-1) - drop audio onto it", famSource, "esn", "ss" },
        { "osc~", oOsc, 1, 1, "220", "sine osc (freq)", famSource, "s", "s" },
        { "phasor~", oPhasor, 1, 1, "2", "ramp 0..1 (freq)", famSource, "s", "s" },
        { "noise~", oNoise, 0, 1, "", "white noise", famSource, "", "s" },
        { "lfo~", oLfo, 1, 1, "1 0", "lfo -1..1 (rate, shape 0-3)", famSource, "s", "s" },
        { "sig", oSig, 0, 1, "0.5", "constant value", famSource, "", "n" },
        { "lores~", oLores, 2, 1, "800 0.5", "resonant lowpass (cutoff, res)", famEffect, "ss", "s" },
        { "hipass~", oHipass, 1, 1, "120", "highpass (cutoff)", famEffect, "s", "s" },
        { "delay~", oDelay, 2, 1, "250 0.5", "delay (ms, feedback) - loops ok", famEffect, "ss", "s" },
        { "tanh~", oTanh, 1, 1, "4", "saturate (drive)", famEffect, "s", "s" },
        { "number", oNumber, 1, 1, "0", "value box: drag it (Ctrl-drag moves) - the modulation currency", famMath, "n", "n" },
        { "*~", oMul, 2, 1, "0.5", "multiply (in2 or arg)", famMath, "ss", "s" },
        { "+~", oAdd, 2, 1, "0", "add (in2 or arg)", famMath, "ss", "s" },
        { "scale", oScale, 1, 1, "0 1", "map 0..1 to (lo, hi)", famMath, "n", "n" },
        { "sah~", oSah, 2, 1, "", "sample & hold (sig, trig)", famMath, "se", "s" },
        { "env~", oEnv, 1, 1, "5 120", "envelope follower (atk, rel ms)", famMath, "s", "n" },
        { "param", oParam, 0, 1, "1", "host knob P1-8", famMath, "", "n" },
        { "metro", oMetro, 1, 1, "2", "pulse train (hz)", famTime, "n", "e" },
        { "random", oRandom, 1, 1, "", "random 0..1 on trigger", famTime, "e", "n" },
        { "clock", oClock, 0, 4, "", "transport: bpm, beat 0-1, bar 0-1, bar #", famTime, "", "nnnn" },
        { "oscin", oOscIn, 0, 1, "9000 /ruin", "OSC receive (port, /addr)", famRouting, "", "n" },
        { "oscout", oOscOut, 1, 0, "127.0.0.1 57120 /ruin/out", "OSC send (host, port, /addr)", famRouting, "n", "" },
        { "modout", oModOut, 1, 0, "", "signal -> mod source in the PATCH bay", famRouting, "s", "" },
        { "strip", oStrip, 3, 3, "1", "drive a channel: gain dB, pan, mute (track#/name)", famRouting, "nnn", "nnn" },
        { "master~", oMaster, 2, 0, "", "send straight to the master bus", famRouting, "ss", "" },
    };
    return s;
}

const PatcherProcessor::Spec* PatcherProcessor::specFor (const String& name)
{
    for (const auto& s : specs())
        if (name == s.name) return &s;
    return nullptr;
}

PatcherProcessor::Obj PatcherProcessor::parseType (const String& name)
{
    for (const auto& s : specs())
        if (name == s.name) return s.type;
    return oUnknown;
}

PatcherProcessor::PatcherProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("In", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Out", juce::AudioChannelSet::stereo(), true))
{
    for (int i = 0; i < 8; ++i)
        addParameter (hostParams[i] = new juce::AudioParameterFloat (
            { "p" + String (i + 1), 1 }, "P" + String (i + 1),
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

    // starter patch: adc~ wired straight to dac~ so inserting WIRES is
    // passthrough until you patch something in between
    auto adc = addNode ("adc~", 220, 70);
    auto dac = addNode ("dac~", 220, 300);
    addCable (adc[id::uid].toString(), 0, dac[id::uid].toString(), 0);
    addCable (adc[id::uid].toString(), 1, dac[id::uid].toString(), 1);

    patch.addListener (this);
    startTimerHz (30);
}

PatcherProcessor::~PatcherProcessor()
{
    stopTimer();
    patch.removeListener (this);
}

// ============================================================ patch model

ValueTree PatcherProcessor::addNode (const String& typedText, int x, int y)
{
    auto tokens = juce::StringArray::fromTokens (typedText.trim(), " ", "");
    if (tokens.isEmpty() || parseType (tokens[0]) == oUnknown)
        return {};
    ValueTree n (kNodeId);
    n.setProperty (id::uid, SessionModel::newUID(), nullptr);
    n.setProperty (id::type, tokens[0], nullptr);
    tokens.remove (0);
    n.setProperty (kArgs, tokens.joinIntoString (" "), nullptr);
    n.setProperty (id::x, x, nullptr);
    n.setProperty (id::y, y, nullptr);
    patch.appendChild (n, nullptr);
    return n;
}

void PatcherProcessor::removeNode (const ValueTree& node)
{
    const String uid = node[id::uid];
    for (int i = patch.getNumChildren(); --i >= 0;)
    {
        auto c = patch.getChild (i);
        if (c == node
            || (c.hasType (kCableId) && (c[id::src].toString() == uid || c[kDst].toString() == uid)))
            patch.removeChild (i, nullptr);
    }
}

void PatcherProcessor::addCable (const String& srcUid, int srcPort, const String& dstUid, int dstPort)
{
    // one cable per inlet: replace anything already there
    for (int i = patch.getNumChildren(); --i >= 0;)
    {
        auto c = patch.getChild (i);
        if (c.hasType (kCableId) && c[kDst].toString() == dstUid && (int) c[kDstPort] == dstPort)
            patch.removeChild (i, nullptr);
    }
    ValueTree c (kCableId);
    c.setProperty (id::src, srcUid, nullptr);
    c.setProperty (kSrcPort, srcPort, nullptr);
    c.setProperty (kDst, dstUid, nullptr);
    c.setProperty (kDstPort, dstPort, nullptr);
    patch.appendChild (c, nullptr);
}

// ============================================================ compile

void PatcherProcessor::compile()
{
    auto prog = std::make_shared<Program>();
    prog->sr = sampleRate;
    chanTaps.clear();
    sampleBufs.clear();
    bool injectsChanged = false;
    std::set<String> injectSeen;

    struct NodeInfo { ValueTree tree; int objIdx = -1; int outBuf[4] = { -1, -1, -1, -1 }; };
    std::map<String, NodeInfo> nodes;

    int bufCount = 0;
    for (const auto& n : patch)
    {
        if (! n.hasType (kNodeId)) continue;
        const Obj t = parseType (n[id::type].toString());
        if (t == oUnknown) continue;
        NodeInfo info;
        info.tree = n;
        const Spec* spec = nullptr;
        for (const auto& s : specs()) if (s.type == t) spec = &s;
        for (int o = 0; o < spec->outs; ++o)
            info.outBuf[o] = bufCount++;
        nodes[n[id::uid].toString()] = info;
    }

    // dependencies (edges into delay~ are weak: it reads its line, not its input)
    std::map<String, std::vector<String>> deps;
    for (const auto& c : patch)
    {
        if (! c.hasType (kCableId)) continue;
        const String src = c[id::src], dst = c[kDst];
        if (! nodes.count (src) || ! nodes.count (dst)) continue;
        if (parseType (nodes[dst].tree[id::type].toString()) != oDelay)
            deps[dst].push_back (src);
    }

    // Kahn with cycle tolerance: leftovers get appended (one-block latency)
    std::vector<String> order;
    std::set<String> done;
    bool progressed = true;
    while (progressed)
    {
        progressed = false;
        for (auto& [uid, info] : nodes)
        {
            if (done.count (uid)) continue;
            bool ready = true;
            for (const auto& d : deps[uid])
                if (! done.count (d)) ready = false;
            if (ready)
            {
                order.push_back (uid);
                done.insert (uid);
                progressed = true;
            }
        }
    }
    for (auto& [uid, info] : nodes)
        if (! done.count (uid))
            order.push_back (uid);

    int modOutSeen = 0;
    for (const auto& uid : order)
    {
        auto& info = nodes[uid];
        const auto& n = info.tree;
        PObj o;
        o.type = parseType (n[id::type].toString());
        auto args = juce::StringArray::fromTokens (n[kArgs].toString(), " ", "");
        o.a = args.size() > 0 ? args[0].getFloatValue() : 0.0f;
        o.b = args.size() > 1 ? args[1].getFloatValue() : 0.0f;
        o.c = args.size() > 2 ? args[2].getFloatValue() : 0.0f;
        o.out0 = info.outBuf[0];
        o.out1 = info.outBuf[1];
        o.out2 = info.outBuf[2];
        o.out3 = info.outBuf[3];

        for (const auto& cb : patch)
        {
            if (! cb.hasType (kCableId) || cb[kDst].toString() != uid) continue;
            const String src = cb[id::src];
            if (! nodes.count (src)) continue;
            const int buf = nodes[src].outBuf[juce::jlimit (0, 3, (int) cb[kSrcPort])];
            switch ((int) cb[kDstPort])
            {
                case 0:  o.in0 = buf; break;
                case 1:  o.in1 = buf; break;
                default: o.in2 = buf; break;
            }
        }

        switch (o.type)
        {
            case oDelay:
                o.line.assign ((size_t) juce::nextPowerOfTwo (juce::jmax (256, (int) (sampleRate * 2.0))), 0.0f);   // pow2: mask instead of % per sample
                break;
            case oParam:
                o.b = (float) (juce::jlimit (1, 8, (int) o.a) - 1);   // stash P index
                break;
            case oDac:
                prog->hasDac = true;
                break;
            case oOscIn:
            case oOscOut:
                o.ext = std::make_shared<std::atomic<float>> (0.0f);  // string args bind in rebuildOsc
                break;
            case oModOut:
                if (modOutSeen < kMaxModOuts)
                    o.modIdx = modOutSeen++;
                break;
            case oNumber:
                o.ext = numberValueFor (uid, o.a);   // shared with the editor face
                break;
            case oChan:
            {
                const String ref = args.size() > 0 ? args[0] : "1";
                const bool pre = args.size() > 1 && args[1].equalsIgnoreCase ("pre");
                o.tap = chanTapProvider != nullptr ? chanTapProvider (ref, pre) : nullptr;
                chanTaps[uid] = o.tap;               // editor meter face reads this
                break;
            }
            case oStrip:
                o.ctl = stripCtlProvider != nullptr
                            ? stripCtlProvider (args.size() > 0 ? args[0] : "1") : nullptr;
                break;
            case oMaster:
            {
                auto& ring = injectRings[uid];      // stable per node across recompiles
                if (ring == nullptr) { ring = std::make_shared<InjectRing>(); injectsChanged = true; }
                injectSeen.insert (uid);
                o.inj = ring;
                break;
            }
            case oSample:
            {
                const String path = n[id::file].toString();
                o.smp = (sampleProvider != nullptr && path.isNotEmpty()) ? sampleProvider (path) : nullptr;
                sampleBufs[uid] = o.smp;             // editor waveform face reads this
                o.sampleLoop = n[kArgs].toString().containsIgnoreCase ("loop");
                // loop free-runs; an unconnected trig inlet means play-on-load
                o.samplePlaying = o.smp != nullptr && (o.sampleLoop || o.in0 < 0);
                break;
            }
            default: break;
        }
        prog->objs.push_back (std::move (o));
    }

    for (auto it = injectRings.begin(); it != injectRings.end();)
    {
        if (! injectSeen.count (it->first)) { it = injectRings.erase (it); injectsChanged = true; }
        else ++it;
    }
    if (injectsChanged && onInjectsChanged != nullptr)
        onInjectsChanged();             // master~ set changed: engine re-gathers

    if (numModOuts.exchange (modOutSeen) != modOutSeen && onModOutsChanged != nullptr)
        onModOutsChanged();             // message thread: compile runs via AsyncUpdater

    prog->numBufs = juce::jmax (1, bufCount);
    prog->bufs.setSize (prog->numBufs, juce::jmax (64, blockSize));
    prog->bufs.clear();

    rebuildOsc (*prog);

    juce::SpinLock::ScopedLockType sl (progLock);
    graveyard.push_back (pendingProg);
    if (graveyard.size() > 8)
        graveyard.erase (graveyard.begin());
    pendingProg = prog;
}

void PatcherProcessor::rebuildOsc (Program& prog)
{
    receivers.clear();
    inBindings.clear();
    outBindings.clear();

    // bind osc objects by scanning patch nodes per type, in document order
    int oscInSeen = 0, oscOutSeen = 0;
    std::vector<ValueTree> oscInNodes, oscOutNodes;
    for (const auto& n : patch)
    {
        if (! n.hasType (kNodeId)) continue;
        const Obj t = parseType (n[id::type].toString());
        if (t == oOscIn)  oscInNodes.push_back (n);
        if (t == oOscOut) oscOutNodes.push_back (n);
    }

    for (auto& obj : prog.objs)
    {
        if (obj.type == oOscIn && oscInSeen < (int) oscInNodes.size())
        {
            auto n = oscInNodes[(size_t) oscInSeen++];
            auto args = juce::StringArray::fromTokens (n[kArgs].toString(), " ", "");
            const int port = args.size() > 0 ? args[0].getIntValue() : 9000;
            const String addr = args.size() > 1 ? args[1] : "/ruin";
            if (! receivers.count (port))
            {
                auto r = std::make_unique<juce::OSCReceiver>();
                if (r->connect (port))
                    receivers[port] = std::move (r);
            }
            if (receivers.count (port) && addr.startsWith ("/"))
            {
                // OSCAddress throws on malformed patterns - typed args are user input
                try
                {
                    auto binding = std::make_unique<OscInBinding>();
                    binding->val = obj.ext;
                    receivers[port]->addListener (binding.get(), juce::OSCAddress (addr));
                    inBindings.push_back (std::move (binding));
                }
                catch (const juce::OSCException&) {}
            }
        }
        else if (obj.type == oOscOut && oscOutSeen < (int) oscOutNodes.size())
        {
            auto n = oscOutNodes[(size_t) oscOutSeen++];
            auto args = juce::StringArray::fromTokens (n[kArgs].toString(), " ", "");
            const String host = args.size() > 0 ? args[0] : "127.0.0.1";
            const int port = args.size() > 1 ? args[1].getIntValue() : 9001;
            const String addr = args.size() > 2 ? args[2] : "/ruin/out";
            try
            {
                OscOutBinding ob;
                ob.sender = std::make_unique<juce::OSCSender>();
                if (ob.sender->connect (host, port) && addr.startsWith ("/"))
                {
                    juce::OSCAddressPattern probe (addr);   // validate now, not at send time
                    juce::ignoreUnused (probe);
                    ob.addr = addr;
                    ob.tap = obj.ext;
                    outBindings.push_back (std::move (ob));
                }
            }
            catch (const juce::OSCException&) {}
        }
    }
}

void PatcherProcessor::timerCallback()
{
    for (auto& ob : outBindings)
    {
        const float v = ob.tap->load();
        if (std::abs (v - ob.last) > 1.0e-5f)
        {
            ob.last = v;
            try { ob.sender->send (juce::OSCMessage (juce::OSCAddressPattern (ob.addr), v)); }
            catch (const juce::OSCException&) {}
        }
    }
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [] (const auto& p) { return p == nullptr || p.use_count() == 1; }),
                     graveyard.end());
}

// ============================================================ audio

void PatcherProcessor::prepareToPlay (double sr, int maxBlock)
{
    sampleRate = sr;
    blockSize = maxBlock;
    compile();
    juce::SpinLock::ScopedLockType sl (progLock);
    rtProg = pendingProg;
}

void PatcherProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    {
        juce::SpinLock::ScopedTryLockType tl (progLock);
        if (tl.isLocked() && pendingProg != rtProg)
            rtProg = pendingProg;
    }
    auto prog = rtProg;
    const int n = buffer.getNumSamples();
    if (prog == nullptr || prog->objs.empty() || n > prog->bufs.getNumSamples())
        return;                                             // empty patch: bit-transparent

    auto& bufs = prog->bufs;
    const float* hostIn[2] = { buffer.getReadPointer (0),
                               buffer.getReadPointer (juce::jmin (1, buffer.getNumChannels() - 1)) };
    const double sr = prog->sr;

    auto bp = [&bufs] (int idx) { return idx >= 0 ? bufs.getWritePointer (idx) : nullptr; };

    for (auto& o : prog->objs)
    {
        float* out0 = bp (o.out0);
        float* out1 = bp (o.out1);
        const float* i0 = o.in0 >= 0 ? bufs.getReadPointer (o.in0) : nullptr;
        const float* i1 = o.in1 >= 0 ? bufs.getReadPointer (o.in1) : nullptr;

        switch (o.type)
        {
            case oAdc:
                if (out0) juce::FloatVectorOperations::copy (out0, hostIn[0], n);
                if (out1) juce::FloatVectorOperations::copy (out1, hostIn[1], n);
                break;

            case oChan:     // most recent block from the tapped strip's ring
                if (o.tap != nullptr)
                    o.tap->readLast (out0, out1, n);
                else
                {
                    if (out0) juce::FloatVectorOperations::clear (out0, n);
                    if (out1) juce::FloatVectorOperations::clear (out1, n);
                }
                break;

            case oStrip:    // inlets seize gain/pan/mute; outlets report them
            {
                const float* i2 = o.in2 >= 0 ? bufs.getReadPointer (o.in2) : nullptr;
                float* out2 = bp (o.out2);
                if (o.ctl != nullptr)
                {
                    const int st = o.ctl->blockStamp.load();
                    if (i0) { o.ctl->gainDb.store (i0[n - 1]); o.ctl->gainStamp.store (st); }
                    if (i1) { o.ctl->pan.store    (i1[n - 1]); o.ctl->panStamp.store  (st); }
                    if (i2) { o.ctl->mute.store   (i2[n - 1]); o.ctl->muteStamp.store (st); }
                    if (out0) juce::FloatVectorOperations::fill (out0, o.ctl->curGainDb.load(), n);
                    if (out1) juce::FloatVectorOperations::fill (out1, o.ctl->curPan.load(), n);
                    if (out2) juce::FloatVectorOperations::fill (out2, o.ctl->curMute.load(), n);
                }
                else
                {
                    if (out0) juce::FloatVectorOperations::clear (out0, n);
                    if (out1) juce::FloatVectorOperations::clear (out1, n);
                    if (out2) juce::FloatVectorOperations::clear (out2, n);
                }
                break;
            }

            case oMaster:   // write into the ring the master strip consumes
                if (o.inj != nullptr && (i0 != nullptr || i1 != nullptr))
                    o.inj->write (i0 != nullptr ? i0 : i1, i1, n);
                break;

            case oSample:   // buffer player: trig restarts at pos, rate scales (negative = reverse)
            {
                if (out0 == nullptr && out1 == nullptr) break;
                if (o.smp == nullptr || o.smp->buf.getNumSamples() < 2)
                {
                    if (out0) juce::FloatVectorOperations::clear (out0, n);
                    if (out1) juce::FloatVectorOperations::clear (out1, n);
                    break;
                }
                const float* i2 = o.in2 >= 0 ? bufs.getReadPointer (o.in2) : nullptr;
                const auto& sbuf = o.smp->buf;
                const float* sl = sbuf.getReadPointer (0);
                const float* srr = sbuf.getReadPointer (sbuf.getNumChannels() > 1 ? 1 : 0);
                const double len = (double) sbuf.getNumSamples() - 1.0;
                const double srRatio = o.smp->sr / sr;
                for (int i = 0; i < n; ++i)
                {
                    const float trig = i0 ? i0[i] : 0.0f;
                    if (trig > 0.5f && o.lastTrig <= 0.5f)
                    {
                        const double pos = i2 ? juce::jlimit (0.0, 1.0, (double) i2[i]) : 0.0;
                        o.ph = pos * len;
                        o.samplePlaying = true;
                    }
                    o.lastTrig = trig;

                    float l = 0.0f, r = 0.0f;
                    if (o.samplePlaying)
                    {
                        const int idx = (int) o.ph;
                        const float frac = (float) (o.ph - idx);
                        l = sl[idx] + (sl[idx + 1] - sl[idx]) * frac;
                        r = srr[idx] + (srr[idx + 1] - srr[idx]) * frac;
                        const double rate = juce::jlimit (-8.0, 8.0, i1 ? (double) i1[i] : 1.0);
                        o.ph += rate * srRatio;
                        if (o.ph >= len) { if (o.sampleLoop) o.ph -= len; else o.samplePlaying = false; }
                        if (o.ph < 0.0)  { if (o.sampleLoop) o.ph += len; else o.samplePlaying = false; }
                    }
                    if (out0) out0[i] = l;
                    if (out1) out1[i] = r;
                }
                break;
            }

            case oClock:    // transport as numbers: bpm, beat phase, bar phase, bar #
            {
                float bpm = 0.0f, beatPh = 0.0f, barPh = 0.0f, barNum = 0.0f;
                if (auto* ph = getPlayHead())
                    if (auto posOpt = ph->getPosition())
                    {
                        bpm = (float) posOpt->getBpm().orFallback (0.0);
                        const double ppq = posOpt->getPpqPosition().orFallback (0.0);
                        beatPh = (float) (ppq - std::floor (ppq));
                        const auto ts = posOpt->getTimeSignature()
                                            .orFallback (juce::AudioPlayHead::TimeSignature());
                        const double beatsPerBar = ts.numerator * 4.0 / juce::jmax (1, ts.denominator);
                        const double barStart = posOpt->getPpqPositionOfLastBarStart().orFallback (0.0);
                        barPh = (float) juce::jlimit (0.0, 1.0, (ppq - barStart)
                                                                / juce::jmax (1.0e-9, beatsPerBar));
                        barNum = (float) posOpt->getBarCount().orFallback (0);
                    }
                float* out2 = bp (o.out2);
                float* out3 = bp (o.out3);
                if (out0) juce::FloatVectorOperations::fill (out0, bpm, n);
                if (out1) juce::FloatVectorOperations::fill (out1, beatPh, n);
                if (out2) juce::FloatVectorOperations::fill (out2, barPh, n);
                if (out3) juce::FloatVectorOperations::fill (out3, barNum, n);
                break;
            }

            case oOsc:
            case oPhasor:
                if (out0)
                    for (int i = 0; i < n; ++i)
                    {
                        const double f = i0 ? i0[i] : o.a;
                        o.ph += f / sr;
                        o.ph -= std::floor (o.ph);
                        out0[i] = o.type == oOsc
                                      ? (float) std::sin (o.ph * juce::MathConstants<double>::twoPi)
                                      : (float) o.ph;
                    }
                break;

            case oNoise:
                if (out0)
                    for (int i = 0; i < n; ++i)
                        out0[i] = o.rng.nextFloat() * 2.0f - 1.0f;
                break;

            case oLfo:
                if (out0)
                    for (int i = 0; i < n; ++i)
                    {
                        const double f = i0 ? i0[i] : o.a;
                        o.ph += f / sr;
                        if (o.ph >= 1.0) { o.ph -= 1.0; o.held = o.rng.nextFloat() * 2.0f - 1.0f; }
                        const int shape = (int) o.b;
                        out0[i] = shape == 1 ? (float) (2.0 * o.ph - 1.0)
                                : shape == 2 ? (o.ph < 0.5 ? 1.0f : -1.0f)
                                : shape == 3 ? o.held
                                : (float) std::sin (o.ph * juce::MathConstants<double>::twoPi);
                    }
                break;

            case oMul:
                if (out0)
                    for (int i = 0; i < n; ++i)
                        out0[i] = (i0 ? i0[i] : 0.0f) * (i1 ? i1[i] : o.a);
                break;

            case oAdd:
                if (out0)
                    for (int i = 0; i < n; ++i)
                        out0[i] = (i0 ? i0[i] : 0.0f) + (i1 ? i1[i] : o.a);
                break;

            case oLores:        // chamberlin SVF lowpass
                if (out0)
                {
                    const float res = juce::jlimit (0.0f, 0.95f, o.b);
                    const float q = 1.0f - res;
                    for (int i = 0; i < n; ++i)
                    {
                        const double fc = juce::jlimit (20.0, sr * 0.22, (double) (i1 ? i1[i] : o.a));
                        const float f = 2.0f * (float) std::sin (juce::MathConstants<double>::pi * fc / sr);
                        o.z1 += f * o.z2;                       // low
                        const float hpv = (i0 ? i0[i] : 0.0f) - o.z1 - q * o.z2;
                        o.z2 += f * hpv;                        // band
                        out0[i] = o.z1;
                    }
                }
                break;

            case oHipass:
                if (out0)
                {
                    const float k = juce::jlimit (0.001f, 0.99f,
                        (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi
                                                  * juce::jlimit (10.0f, 18000.0f, o.a) / sr)));
                    for (int i = 0; i < n; ++i)
                    {
                        o.z1 += k * ((i0 ? i0[i] : 0.0f) - o.z1);
                        out0[i] = (i0 ? i0[i] : 0.0f) - o.z1;
                    }
                }
                break;

            case oDelay:        // read pass; write happens after everyone
                if (out0)
                {
                    const int len = (int) o.line.size();
                    const int lm = len - 1;                     // line is pow2-sized
                    for (int i = 0; i < n; ++i)
                    {
                        const double ms = i1 ? juce::jmax (0.0f, i1[i]) : o.a;
                        int d = (int) (ms * 0.001 * sr);
                        d = juce::jlimit (n, len - 1, d);       // feedback-safe minimum
                        out0[i] = o.line[(size_t) ((o.wp + i - d + len) & lm)];
                    }
                }
                break;

            case oTanh:
                if (out0)
                {
                    const float drive = juce::jmax (0.1f, o.a == 0.0f ? 1.0f : o.a);
                    for (int i = 0; i < n; ++i)
                        out0[i] = std::tanh ((i0 ? i0[i] : 0.0f) * drive);
                }
                break;

            case oNumber:       // value box: inlet sets it, outlet emits it
                if (out0)
                {
                    float v = o.ext->load();
                    if (i0 != nullptr) { v = i0[n - 1]; o.ext->store (v); }
                    juce::FloatVectorOperations::fill (out0, v, n);
                }
                break;

            case oModOut:       // control-rate tap into the PATCH mod bay
                if (o.modIdx >= 0)
                {
                    float mean = 0.0f;
                    if (i0 != nullptr)
                    {
                        for (int i = 0; i < n; ++i) mean += i0[i];
                        mean /= (float) juce::jmax (1, n);
                    }
                    o.z1 += 0.35f * (mean - o.z1);              // light smoothing across blocks
                    modOutVals[(size_t) o.modIdx].store (juce::jlimit (-1.0f, 1.0f, o.z1));
                }
                break;

            case oSah:
                if (out0)
                    for (int i = 0; i < n; ++i)
                    {
                        const float trig = i1 ? i1[i] : 0.0f;
                        if (trig > 0.5f && o.lastTrig <= 0.5f)
                            o.held = i0 ? i0[i] : 0.0f;
                        o.lastTrig = trig;
                        out0[i] = o.held;
                    }
                break;

            case oEnv:
            {
                if (out0)
                {
                    const float atk = (float) std::exp (-1.0 / (juce::jmax (0.5f, o.a == 0 ? 5.0f : o.a) * 0.001 * sr));
                    const float rel = (float) std::exp (-1.0 / (juce::jmax (1.0f, o.b == 0 ? 120.0f : o.b) * 0.001 * sr));
                    for (int i = 0; i < n; ++i)
                    {
                        const float x = std::abs (i0 ? i0[i] : 0.0f);
                        o.z1 = x > o.z1 ? atk * o.z1 + (1 - atk) * x : rel * o.z1 + (1 - rel) * x;
                        out0[i] = o.z1;
                    }
                }
                break;
            }

            case oMetro:
                if (out0)
                    for (int i = 0; i < n; ++i)
                    {
                        const double hz = juce::jmax (0.01, (double) (i0 ? i0[i] : o.a));
                        o.ph += hz / sr;
                        float v = 0.0f;
                        if (o.ph >= 1.0) { o.ph -= 1.0; o.z1 = (float) (0.002 * sr); }
                        if (o.z1 > 0) { v = 1.0f; o.z1 -= 1.0f; }
                        out0[i] = v;
                    }
                break;

            case oRandom:
                if (out0)
                    for (int i = 0; i < n; ++i)
                    {
                        const float trig = i0 ? i0[i] : 0.0f;
                        if (trig > 0.5f && o.lastTrig <= 0.5f)
                            o.held = o.rng.nextFloat();
                        o.lastTrig = trig;
                        out0[i] = o.held;
                    }
                break;

            case oScale:
                if (out0)
                    for (int i = 0; i < n; ++i)
                        out0[i] = o.a + (i0 ? i0[i] : 0.0f) * (o.b - o.a);
                break;

            case oSig:
                if (out0)
                    juce::FloatVectorOperations::fill (out0, o.a, n);
                break;

            case oParam:
                if (out0)
                {
                    const int idx = juce::jlimit (0, 7, (int) o.b);
                    juce::FloatVectorOperations::fill (out0, hostParams[idx]->get(), n);
                }
                break;

            case oOscIn:
                if (out0 && o.ext != nullptr)
                    juce::FloatVectorOperations::fill (out0, o.ext->load(), n);
                break;

            case oOscOut:
                if (o.ext != nullptr && i0 != nullptr)
                    o.ext->store (i0[n - 1]);
                break;

            default:
                break;
        }
    }

    // pass 2: delay writes (this is what makes feedback loops legal)
    for (auto& o : prog->objs)
        if (o.type == oDelay)
        {
            const int len = (int) o.line.size();
            const int lm = len - 1;                             // line is pow2-sized
            const float* i0 = o.in0 >= 0 ? bufs.getReadPointer (o.in0) : nullptr;
            const float fb = juce::jlimit (0.0f, 0.95f, o.b);
            float* out0 = bp (o.out0);
            for (int i = 0; i < n; ++i)
                o.line[(size_t) ((o.wp + i) & lm)] =
                    (i0 ? i0[i] : 0.0f) + (out0 != nullptr ? out0[i] * fb : 0.0f);
            o.wp = (o.wp + n) & lm;
        }

    // dac~: replace host audio; patches without dac~ stay transparent
    if (prog->hasDac)
    {
        buffer.clear();
        for (auto& o : prog->objs)
            if (o.type == oDac)
            {
                if (o.in0 >= 0) buffer.addFrom (0, 0, bufs.getReadPointer (o.in0), n);
                const int r = o.in1 >= 0 ? o.in1 : o.in0;   // mono patches mirror to both sides
                if (r >= 0 && buffer.getNumChannels() > 1)
                    buffer.addFrom (1, 0, bufs.getReadPointer (r), n);
            }
    }
}

// ============================================================ state

void PatcherProcessor::getStateInformation (juce::MemoryBlock& mb)
{
    ValueTree root ("WIRESSTATE");
    root.appendChild (patch.createCopy(), nullptr);
    for (int i = 0; i < 8; ++i)
        root.setProperty ("p" + String (i + 1), hostParams[i]->get(), nullptr);
    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, mb);
}

void PatcherProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto root = ValueTree::fromXml (*xml);
        auto p = root.getChildWithName ("WIRESPATCH");
        if (p.isValid())
        {
            patch.removeListener (this);
            patch.copyPropertiesAndChildrenFrom (p, nullptr);
            patch.addListener (this);
        }
        for (int i = 0; i < 8; ++i)
            hostParams[i]->setValueNotifyingHost (
                (float) (double) root.getProperty ("p" + String (i + 1), 0.0));
        compile();
    }
}

juce::AudioProcessorEditor* PatcherProcessor::createEditor()
{
    return new PatcherEditor (*this);
}

} // namespace dg
