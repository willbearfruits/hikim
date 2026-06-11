#pragma once
#include "Ids.h"

namespace dg
{

// Owns the session ValueTree, the undo manager, and file persistence.
// All edits go through the tree (with the UndoManager) so undo/redo is global.
class SessionModel
{
public:
    SessionModel();

    void newSession();
    bool load (const File& f, String& error);
    bool save();                       // to projectFile (must be set)
    bool saveAs (const File& f);

    ValueTree root;                    // SESSION
    juce::UndoManager undo;
    File projectFile;

    ValueTree transport()  const { return root.getChildWithName (id::TRANSPORT); }
    ValueTree tempoMap()   const { return root.getChildWithName (id::TEMPOMAP); }
    ValueTree tracks()     const { return root.getChildWithName (id::TRACKS); }
    ValueTree markers()    const { return root.getChildWithName (id::MARKERS); }
    ValueTree video()      const { return root.getChildWithName (id::VIDEO); }
    ValueTree mods()       const { ValueTree r = root; return r.getOrCreateChildWithName (id::MODS, nullptr); }
    ValueTree scenes()     const { ValueTree r = root; return r.getOrCreateChildWithName (id::SCENES, nullptr); }

    // ---- session view (launch grid) ----
    ValueTree addScene (const String& name);
    static ValueTree slotsOf (ValueTree track) { return track.getOrCreateChildWithName (id::SLOTS, nullptr); }
    ValueTree getSlotClip (ValueTree track, const String& sceneUid) const;   // CLIP inside the SLOT (may be invalid)
    ValueTree setSlotClip (ValueTree track, const String& sceneUid, ValueTree clip);   // replaces existing
    ValueTree masterTrack() const;

    // type: "audio" | "midi" | "bus" | "video" | "master"
    ValueTree addTrack (const String& type, const String& name);
    void removeTrack (const ValueTree& track);

    // All timeline positions in the tree are double SECONDS (sample-rate independent).
    // MIDI note positions are beats relative to the clip start. Audio clip 'offset'
    // is in source-file samples.
    ValueTree addAudioClip (ValueTree track, const File& audioFile,
                            double startSec, double lengthSec,
                            double fileSampleRate, int lane = 0);
    ValueTree addMidiClip (ValueTree track, double startSec, double lengthSec, int lane = 0);

    ValueTree addInsert (ValueTree track, const String& type);  // "rack" | "plugin" | "instrument"
    ValueTree addMarker (double timeSec, const String& name);

    ValueTree findTrack (const String& uid) const;
    static ValueTree clipsOf (ValueTree track)   { return track.getOrCreateChildWithName (id::CLIPS, nullptr); }
    static ValueTree insertsOf (ValueTree track) { return track.getOrCreateChildWithName (id::INSERTS, nullptr); }
    static ValueTree autoOf (ValueTree track)    { return track.getOrCreateChildWithName (id::AUTO, nullptr); }

    File assetsDir() const;            // <project>_Assets next to the project file
    static String newUID();

    std::function<void()> onSessionReplaced;   // wholesale tree swap (new/load)

private:
    void buildDefaultSession();
};

} // namespace dg
