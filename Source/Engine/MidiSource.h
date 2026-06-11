#pragma once
#include "Processors.h"

namespace dg
{

class AudioEngine;

struct MidiNoteRT { juce::int64 on, off; juce::uint8 note, vel; };
struct MidiPlaylist { std::vector<MidiNoteRT> notes; };   // sorted by 'on', pre-clipped to clips

// MIDI-track source node: emits clip notes + live thru (when armed) into the
// instrument node downstream. All-notes-off on stop/seek/loop-wrap via engine flag.
class MidiSourceProcessor : public BasicProcessor
{
public:
    MidiSourceProcessor (AudioEngine& e, const String& trackUid);

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    void setPlaylist (std::shared_ptr<const MidiPlaylist> p);

    std::atomic<bool> armed { false };

private:
    AudioEngine& engine;
    String trackUid;

    juce::SpinLock lock;
    std::shared_ptr<const MidiPlaylist> pending, rt;
};

} // namespace dg
