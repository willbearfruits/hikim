#include "MidiSource.h"
#include "AudioEngine.h"

namespace dg
{

MidiSourceProcessor::MidiSourceProcessor (AudioEngine& e, const String& uid)
    : BasicProcessor ("MIDI: " + uid, true, true), engine (e), trackUid (uid) {}

void MidiSourceProcessor::setPlaylist (std::shared_ptr<const MidiPlaylist> p)
{
    juce::SpinLock::ScopedLockType sl (lock);
    pending = std::move (p);
}

void MidiSourceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    const int n = buffer.getNumSamples();
    const juce::int64 pos = engine.segStartSample();
    const bool playing = engine.segIsPlaying();

    if (! armed.load())
        midi.clear();                      // live thru only on armed tracks

    if (engine.shouldFlushMidi())
        for (int ch = 1; ch <= 16; ++ch)
            midi.addEvent (juce::MidiMessage::allNotesOff (ch), 0);

    if (! playing)
        return;

    {
        juce::SpinLock::ScopedTryLockType tl (lock);
        if (tl.isLocked() && pending != rt)
            rt = pending;
    }
    if (rt == nullptr)
        return;

    for (const auto& note : rt->notes)     // sorted by 'on'
    {
        if (note.on >= pos + n) break;
        if (note.on >= pos)
            midi.addEvent (juce::MidiMessage::noteOn (1, note.note, note.vel), (int) (note.on - pos));
        if (note.off >= pos && note.off < pos + n)
            midi.addEvent (juce::MidiMessage::noteOff (1, note.note), (int) (note.off - pos));
    }
}

} // namespace dg
