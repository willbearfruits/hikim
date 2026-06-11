#include "Session.h"

namespace dg
{

static const juce::StringArray kTrackColours {
    "ffcf3b3b", "ffd9803a", "ffd6c44a", "ff64b54e", "ff44b8a8",
    "ff4a7fd6", "ff7a55d1", "ffc457b8", "ff999999"
};

SessionModel::SessionModel()
{
    newSession();
}

String SessionModel::newUID()
{
    return juce::Uuid().toString().substring (0, 12);
}

void SessionModel::buildDefaultSession()
{
    root = ValueTree (id::SESSION);
    root.setProperty (id::version, 1, nullptr);

    ValueTree tr (id::TRANSPORT);
    tr.setProperty (id::loopStart, 0.0, nullptr);
    tr.setProperty (id::loopEnd,   0.0, nullptr);
    tr.setProperty (id::loopOn,    false, nullptr);
    tr.setProperty (id::punchIn,   0.0, nullptr);
    tr.setProperty (id::punchOut,  0.0, nullptr);
    tr.setProperty (id::punchOn,   false, nullptr);
    tr.setProperty (id::metro,     false, nullptr);
    root.appendChild (tr, nullptr);

    ValueTree tm (id::TEMPOMAP);
    ValueTree t0 (id::TEMPO);
    t0.setProperty (id::beat, 0.0, nullptr);
    t0.setProperty (id::bpm, 120.0, nullptr);
    tm.appendChild (t0, nullptr);
    ValueTree s0 (id::TIMESIG);
    s0.setProperty (id::beat, 0.0, nullptr);
    s0.setProperty (id::num, 4, nullptr);
    s0.setProperty (id::den, 4, nullptr);
    tm.appendChild (s0, nullptr);
    root.appendChild (tm, nullptr);

    root.appendChild (ValueTree (id::TRACKS), nullptr);
    root.appendChild (ValueTree (id::MARKERS), nullptr);

    ValueTree vid (id::VIDEO);
    vid.setProperty (id::file, "", nullptr);
    vid.setProperty (id::offset, 0, nullptr);
    vid.setProperty (id::fps, 25.0, nullptr);
    root.appendChild (vid, nullptr);

    ValueTree master (id::TRACK);
    master.setProperty (id::uid, "master", nullptr);
    master.setProperty (id::type, "master", nullptr);
    master.setProperty (id::name, "MASTER", nullptr);
    master.setProperty (id::colour, "ffffffff", nullptr);
    tracks().appendChild (master, nullptr);
}

void SessionModel::newSession()
{
    undo.clearUndoHistory();
    projectFile = File();
    buildDefaultSession();

    addTrack ("audio", "Audio 1");
    addTrack ("audio", "Audio 2");
    addTrack ("midi",  "Inst 1");

    if (onSessionReplaced) onSessionReplaced();
}

ValueTree SessionModel::masterTrack() const
{
    return tracks().getChildWithProperty (id::uid, "master");
}

ValueTree SessionModel::addTrack (const String& type, const String& name)
{
    ValueTree t (id::TRACK);
    t.setProperty (id::uid, newUID(), nullptr);
    t.setProperty (id::type, type, nullptr);
    t.setProperty (id::name, name, nullptr);
    const int n = tracks().getNumChildren();
    t.setProperty (id::colour, kTrackColours[n % kTrackColours.size()], nullptr);
    t.setProperty (id::gain, 0.0, nullptr);
    t.setProperty (id::pan, 0.0, nullptr);
    t.setProperty (id::mute, false, nullptr);
    t.setProperty (id::solo, false, nullptr);
    t.setProperty (id::armed, false, nullptr);
    t.setProperty (id::monitor, 0, nullptr);
    t.setProperty (id::inputChan, 0, nullptr);
    t.setProperty (id::inputStereo, false, nullptr);
    t.setProperty (id::outputBus, "master", nullptr);
    t.setProperty (id::sendABus, "", nullptr);
    t.setProperty (id::sendALevel, -60.0, nullptr);
    t.setProperty (id::sendBBus, "", nullptr);
    t.setProperty (id::sendBLevel, -60.0, nullptr);
    t.appendChild (ValueTree (id::CLIPS), nullptr);
    t.appendChild (ValueTree (id::INSERTS), nullptr);
    t.appendChild (ValueTree (id::AUTO), nullptr);

    // keep master last in the list
    auto master = masterTrack();
    auto tl = tracks();
    if (master.isValid())
        tl.addChild (t, tl.indexOf (master), &undo);
    else
        tl.appendChild (t, &undo);
    return t;
}

void SessionModel::removeTrack (const ValueTree& track)
{
    if (track[id::type].toString() == "master") return;
    auto tl = tracks();
    tl.removeChild (track, &undo);
}

ValueTree SessionModel::addAudioClip (ValueTree track, const File& f, double start,
                                      double length, double fileSR, int lane)
{
    ValueTree c (id::CLIP);
    c.setProperty (id::uid, newUID(), nullptr);
    c.setProperty (id::type, "audio", nullptr);
    c.setProperty (id::name, f.getFileNameWithoutExtension(), nullptr);
    c.setProperty (id::file, f.getFullPathName(), nullptr);
    c.setProperty (id::fileSR, fileSR, nullptr);
    c.setProperty (id::start, start, nullptr);
    c.setProperty (id::length, length, nullptr);
    c.setProperty (id::offset, 0, nullptr);
    c.setProperty (id::clipGain, 0.0, nullptr);
    c.setProperty (id::fadeIn, 0, nullptr);
    c.setProperty (id::fadeOut, 0, nullptr);
    c.setProperty (id::stretch, 1.0, nullptr);
    c.setProperty (id::lane, lane, nullptr);
    clipsOf (track).appendChild (c, &undo);
    return c;
}

ValueTree SessionModel::addMidiClip (ValueTree track, double start, double length, int lane)
{
    ValueTree c (id::CLIP);
    c.setProperty (id::uid, newUID(), nullptr);
    c.setProperty (id::type, "midi", nullptr);
    c.setProperty (id::name, "MIDI", nullptr);
    c.setProperty (id::start, start, nullptr);
    c.setProperty (id::length, length, nullptr);
    c.setProperty (id::lane, lane, nullptr);
    c.appendChild (ValueTree (id::NOTES), nullptr);
    clipsOf (track).appendChild (c, &undo);
    return c;
}

ValueTree SessionModel::addInsert (ValueTree track, const String& type)
{
    ValueTree i (id::INSERT);
    i.setProperty (id::uid, newUID(), nullptr);
    i.setProperty (id::type, type, nullptr);
    i.setProperty (id::bypass, false, nullptr);
    insertsOf (track).appendChild (i, &undo);
    return i;
}

ValueTree SessionModel::addMarker (double timeSec, const String& name)
{
    ValueTree m (id::MARKER);
    m.setProperty (id::t, timeSec, nullptr);
    m.setProperty (id::name, name, nullptr);
    markers().appendChild (m, &undo);
    return m;
}

ValueTree SessionModel::findTrack (const String& uid) const
{
    return tracks().getChildWithProperty (id::uid, uid);
}

File SessionModel::assetsDir() const
{
    if (projectFile == File())
        return File::getSpecialLocation (File::userDocumentsDirectory)
                   .getChildFile (String (names::appName) + "_Recordings");
    return projectFile.getSiblingFile (projectFile.getFileNameWithoutExtension() + "_Assets");
}

bool SessionModel::save()
{
    if (projectFile == File()) return false;
    return saveAs (projectFile);
}

bool SessionModel::saveAs (const File& f)
{
    projectFile = f;
    if (auto xml = root.createXml())
        return xml->writeTo (f);
    return false;
}

bool SessionModel::load (const File& f, String& error)
{
    auto xml = juce::parseXML (f);
    if (xml == nullptr) { error = "Not a valid project file."; return false; }
    auto t = ValueTree::fromXml (*xml);
    if (! t.hasType (id::SESSION)) { error = "Not a " + String (names::appName) + " session."; return false; }

    undo.clearUndoHistory();
    root = t;
    projectFile = f;

    // tolerate older / hand-edited files
    for (auto tag : { id::TRANSPORT, id::TEMPOMAP, id::TRACKS, id::MARKERS, id::VIDEO, id::MODS })
        root.getOrCreateChildWithName (tag, nullptr);

    if (onSessionReplaced) onSessionReplaced();
    return true;
}

} // namespace dg
