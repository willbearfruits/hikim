#include "ClipPlayer.h"
#include "AudioEngine.h"

namespace dg
{

ClipPlayerProcessor::ClipPlayerProcessor (AudioEngine& e, const String& uid)
    : BasicProcessor ("Source: " + uid, true, true), engine (e), trackUid (uid) {}

void ClipPlayerProcessor::prepareToPlay (double, int maxBlock)
{
    fileTemp.setSize (2, juce::jmax (256, maxBlock * 8 + 8));
    inputCopy.setSize (2, juce::jmax (256, maxBlock));
}

void ClipPlayerProcessor::setPlaylist (std::shared_ptr<const AudioPlaylist> p)
{
    juce::SpinLock::ScopedLockType sl (lock);
    pending = std::move (p);
}

void ClipPlayerProcessor::setSessionClip (std::shared_ptr<const AudioPlaylist> p)
{
    juce::SpinLock::ScopedLockType sl (lock);
    sessPending = std::move (p);
}

void ClipPlayerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (2, buffer.getNumChannels());
    const juce::int64 pos = engine.segStartSample();
    const bool playing = engine.segIsPlaying();

    // The buffer holds whatever input is routed to this track. Grab it first.
    const bool wantsInput = armed.load() && (monitorMode.load() == 2 || rec.load() != nullptr);
    if (wantsInput)
        for (int ch = 0; ch < chans; ++ch)
            inputCopy.copyFrom (ch, 0, buffer, ch, 0, n);

    if (auto* rs = rec.load(); rs != nullptr && armed.load()
                           && (engine.segRecordEnabled() || (rs->slotMode && engine.segIsPlaying())))
    {
        if (rs->needPassMark.exchange (false))
            if (rs->passes.size() < rs->passes.capacity())
                rs->passes.push_back ({ rs->written.load(), (double) pos / engine.getSampleRate() });

        const float* ptrs[2] = { inputCopy.getReadPointer (0),
                                 inputCopy.getReadPointer (chans > 1 ? 1 : 0) };
        if (rs->writer != nullptr)
            rs->writer->write (ptrs, n);
        rs->written += n;

        // live waveform peaks for the timeline
        for (int i = 0; i < n; ++i)
        {
            rs->peakAccum = juce::jmax (rs->peakAccum, std::abs (ptrs[0][i]), std::abs (ptrs[1][i]));
            if (++rs->peakAccumCount >= rs->samplesPerPeak)
            {
                const int c = rs->peakCount.load();
                if (c < rs->peakCapacity)
                {
                    rs->peaks[c] = rs->peakAccum;
                    rs->peakCount.store (c + 1);
                }
                rs->peakAccum = 0.0f;
                rs->peakAccumCount = 0;
            }
        }
    }

    buffer.clear();

    if (armed.load() && monitorMode.load() == 2)
        for (int ch = 0; ch < chans; ++ch)
            buffer.addFrom (ch, 0, inputCopy, ch, 0, n);

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

    // session slot overrides the timeline on this track, looping in sync
    if (sessEngaged.load())
    {
        const juce::int64 len = sessLen.load();
        if (sessRT != nullptr && ! sessRT->clips.empty() && len > 0)
        {
            const auto& c = sessRT->clips[0];
            juce::int64 local = (pos - sessStart.load()) % len;
            if (local < 0) local += len;
            int done = 0;
            float* ptrs[2];
            while (done < n)
            {
                const int chunkLen = (int) juce::jmin ((juce::int64) (n - done), len - local);
                for (int ch = 0; ch < chans; ++ch)
                    ptrs[ch] = buffer.getWritePointer (ch) + done;
                juce::AudioBuffer<float> sub (ptrs, chans, chunkLen);
                renderClip (c, sub, local, chunkLen);
                done += chunkLen;
                local = (local + chunkLen) % len;
            }
        }
        return;
    }

    if (rt == nullptr)
        return;

    for (const auto& c : rt->clips)
        renderClip (c, buffer, pos, n);
}

void ClipPlayerProcessor::renderClip (const AudioClipRT& c, juce::AudioBuffer<float>& out,
                                      juce::int64 blockStart, int n)
{
    renderClipSpan (c, out, blockStart, n, fileTemp);
}

} // namespace dg
