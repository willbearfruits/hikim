#pragma once
#include "Session.h"
#include "../Engine/TempoMap.h"
#include <functional>
#include <set>
#include <vector>

namespace dg::clipops
{
    // All clip editing operations live here, UI-free, so the test suite can
    // drive them exactly as the timeline does. Every op opens its own undo
    // transaction. Returned arrays are the uids of newly created clips.

    juce::StringArray splitAt (SessionModel&, const TempoMap&,
                               const std::set<String>& selectedUids, double timeSec);

    void deleteClips (SessionModel&, const std::set<String>& uids);
    void rippleDelete (SessionModel&, const std::set<String>& uids);

    struct ClipboardItem { String trackUid; ValueTree clip; };
    std::vector<ClipboardItem> copyClips (SessionModel&, const std::set<String>& uids);
    juce::StringArray paste (SessionModel&, const std::vector<ClipboardItem>&, double atSec);
    juce::StringArray duplicate (SessionModel&, const std::set<String>& uids);

    // Clip loop + slip - the missing half of break-editing.
    // setLoop: content repeats every pass to fill the clip length. The first
    // enable seeds the pass length: audio takes contentLenSecFor (clip) when
    // provided (timeline seconds of source left after the offset) capped at
    // the clip length, else the clip length; midi takes whole beats.
    void setLoop (SessionModel&, const TempoMap&, const std::set<String>& uids, bool on,
                  const std::function<double (const ValueTree& clip)>& contentLenSecFor = {});

    // slip: move content inside the clip without moving its edges. +delta
    // drags content later. Audio slides the source window (offset clamps at
    // the file start); midi shifts note beats, wrapping modulo the loop pass
    // when looping. newTransaction=false lets a drag gesture coalesce into
    // the transaction its mouseDown opened.
    void slip (SessionModel&, const TempoMap&, const std::set<String>& uids,
               double deltaSec, bool newTransaction = true);

    // Crossfade handles. A partial tail/head overlap between two audible
    // audio clips is what the engine auto-crossfades (equal power); these
    // find and edit that overlap as a first-class object.
    struct Overlap
    {
        ValueTree left, right;            // invalid when none
        double start = 0, end = 0;        // overlap region in seconds
        bool isValid() const { return left.isValid() && right.isValid(); }
    };
    Overlap overlapAt (ValueTree track, double timeSec);        // overlap containing timeSec
    std::vector<Overlap> overlapsOf (ValueTree clip);           // every overlap this clip is part of

    // Roll the comp boundary: the left clip's end and the right clip's start
    // move together (overlap length preserved, right content stays anchored
    // in time). Returns the applied (clamped) delta.
    double rollBoundary (SessionModel&, ValueTree left, ValueTree right,
                         double deltaSec, bool newTransaction = true);

    // Symmetrically resize the overlap about its centre (alt-drag on the
    // handle). Returns the applied (clamped) overlap length.
    double resizeOverlap (SessionModel&, ValueTree left, ValueTree right,
                          double newOverlapSec, bool newTransaction = true);

    // ---- piano-roll note ops ----------------------------------------------
    // MIDI notes are NOTE children of the clip's NOTES tree, beats relative
    // to the clip start. Indices index into that tree (the roll's selection).
    struct NoteData { double beat = 0.0, len = 0.25; int pitch = 60, vel = 100; };

    // clipboard snapshot, normalised so the earliest note sits at beat 0
    // (relative positions/lengths/velocities preserved; clip-agnostic)
    std::vector<NoteData> copyNotes (const ValueTree& clip, const std::set<int>& noteIndices);

    // append the items anchored at atBeat (each note clamped to beat >= 0);
    // returns the new notes' indices so the UI can select them
    std::vector<int> pasteNotes (SessionModel&, ValueTree clip,
                                 const std::vector<NoteData>& items, double atBeat);

    // Bitwig-style duplicate: copies land one selection-span later
    // (span = earliest start -> latest end of the selected notes)
    std::vector<int> duplicateNotes (SessionModel&, ValueTree clip,
                                     const std::set<int>& noteIndices);

    void deleteNotes (SessionModel&, ValueTree clip, const std::set<int>& noteIndices);
}
