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

void MidiSourceProcessor::setSessionClip (std::shared_ptr<const MidiPlaylist> p)
{
    juce::SpinLock::ScopedLockType sl (lock);
    sessPending = std::move (p);
}

void MidiSourceProcessor::emitSpan (juce::MidiBuffer& midi, const MidiPlaylist& pl,
                                    juce::int64 localStart, int count, int bufOffset)
{
    // notes are sorted by 'on'; any note whose off can still land in this span
    // starts at or after localStart - maxLen, so skip the elapsed history
    // instead of rescanning it every block
    auto it = std::lower_bound (pl.notes.begin(), pl.notes.end(), localStart - pl.maxLen,
                                [] (const MidiNoteRT& n, juce::int64 t) { return n.on < t; });
    for (; it != pl.notes.end(); ++it)
    {
        const auto& note = *it;
        if (note.on >= localStart + count) break;
        if (note.on >= localStart)
            midi.addEvent (juce::MidiMessage::noteOn (1, note.note, note.vel),
                           bufOffset + (int) (note.on - localStart));
        if (note.off >= localStart && note.off < localStart + count)
            midi.addEvent (juce::MidiMessage::noteOff (1, note.note),
                           bufOffset + (int) (note.off - localStart));
    }
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
        if (tl.isLocked())
        {
            if (pending != rt) rt = pending;
            if (sessPending != sessRT) sessRT = sessPending;
        }
    }

    // session slot overrides the timeline notes on this track
    if (sessEngaged.load())
    {
        const juce::int64 len = sessLen.load();
        if (sessRT != nullptr && len > 0)
        {
            juce::int64 local = (pos - sessStart.load()) % len;
            if (local < 0) local += len;
            int done = 0;
            while (done < n)
            {
                const int chunkLen = (int) juce::jmin ((juce::int64) (n - done), len - local);
                emitSpan (midi, *sessRT, local, chunkLen, done);
                done += chunkLen;
                local += chunkLen;
                if (local >= len)
                {
                    local = 0;       // loop wrap: close hanging notes cleanly
                    for (int ch = 1; ch <= 16; ++ch)
                        midi.addEvent (juce::MidiMessage::allNotesOff (ch), juce::jmax (0, done - 1));
                }
            }
        }
        return;
    }

    if (rt == nullptr)
        return;

    emitSpan (midi, *rt, pos, n, 0);
}

} // namespace dg
