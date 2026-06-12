#pragma once
#include "Processors.h"
#include "TempoMap.h"

namespace dg
{

class AudioEngine;

struct MidiNoteRT { juce::int64 on, off; juce::uint8 note, vel; };
struct MidiPlaylist
{
    std::vector<MidiNoteRT> notes;     // sorted by 'on', pre-clipped to clips
    juce::int64 maxLen = 0;            // longest note: bounds the lower_bound window in emitSpan
};

// Expands one timeline MIDI clip into RT notes appended to pl (unsorted -
// the caller sorts and sets maxLen). Content-looped clips repeat the first
// loopBeats of their notes every pass across the clip length. Header-inline
// so the headless suite drives the exact expansion the engine plays.
inline void appendClipNotes (MidiPlaylist& pl, const ValueTree& c, const TempoMap& map, double sr)
{
    const double startSec = (double) c[id::start];
    const double lenSec = (double) c[id::length];
    const double clipStartBeat = map.secondsToBeats (startSec);
    const auto clipStartSa = (juce::int64) std::llround (startSec * sr);
    const auto clipEndSa = (juce::int64) std::llround ((startSec + lenSec) * sr);

    const double loopBeats = (double) c.getProperty (id::loopBeats, 0.0);
    const bool looping = (bool) c.getProperty (id::loop, false) && loopBeats > 1.0e-6;
    const double clipEndBeat = map.secondsToBeats (startSec + lenSec);
    const int passes = looping
        ? juce::jlimit (1, 4096, (int) std::ceil ((clipEndBeat - clipStartBeat) / loopBeats))
        : 1;

    for (const auto& nt : c.getChildWithName (id::NOTES))
    {
        const double nb = (double) nt[id::beat];
        if (looping && nb >= loopBeats) continue;          // beyond the pass: silent
        for (int pass = 0; pass < passes; ++pass)
        {
            const double b = clipStartBeat + nb + pass * loopBeats;
            MidiNoteRT n;
            n.on  = map.beatsToSamples (b);
            n.off = map.beatsToSamples (b + (double) nt[id::len]);
            if (looping)        // a held note must not collide with its own repeat
                n.off = juce::jmin (n.off, map.beatsToSamples (clipStartBeat + (pass + 1) * loopBeats));
            if (n.on >= clipEndSa) break;
            if (n.off <= clipStartSa) continue;
            n.on  = juce::jmax (n.on, clipStartSa);
            n.off = juce::jmin (n.off, clipEndSa);
            n.note = (juce::uint8) (int) nt[id::pitch];
            n.vel  = (juce::uint8) juce::jlimit (1, 127, (int) nt[id::vel]);
            pl.notes.push_back (n);
        }
    }
}

// MIDI-track source node: emits clip notes + live thru (when armed) into the
// instrument node downstream. All-notes-off on stop/seek/loop-wrap via engine flag.
class MidiSourceProcessor : public BasicProcessor
{
public:
    MidiSourceProcessor (AudioEngine& e, const String& trackUid);

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    void setPlaylist (std::shared_ptr<const MidiPlaylist> p);
    void setSessionClip (std::shared_ptr<const MidiPlaylist> p);

    std::atomic<bool> armed { false };

    // session view: looping slot overrides the timeline
    std::atomic<bool> sessEngaged { false };
    std::atomic<juce::int64> sessStart { 0 }, sessLen { 0 };

private:
    void emitSpan (juce::MidiBuffer&, const MidiPlaylist&, juce::int64 localStart, int count, int bufOffset);

    AudioEngine& engine;
    String trackUid;

    juce::SpinLock lock;
    std::shared_ptr<const MidiPlaylist> pending, rt;
    std::shared_ptr<const MidiPlaylist> sessPending, sessRT;
};

} // namespace dg
