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

    if (auto* rs = rec.load(); rs != nullptr && armed.load() && engine.segRecordEnabled())
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
        if (tl.isLocked() && pending != rt)
            rt = pending;
    }
    if (rt == nullptr)
        return;

    for (const auto& c : rt->clips)
        renderClip (c, buffer, pos, n);
}

void ClipPlayerProcessor::renderClip (const AudioClipRT& c, juce::AudioBuffer<float>& out,
                                      juce::int64 blockStart, int n)
{
    const juce::int64 s = juce::jmax (blockStart, c.start);
    const juce::int64 e = juce::jmin (blockStart + (juce::int64) n, c.start + c.length);
    if (s >= e || c.reader == nullptr)
        return;

    int outOff = (int) (s - blockStart);
    juce::int64 clipPos = s - c.start;                       // engine samples into the clip
    int count = (int) (e - s);
    double srcPos = c.offset + (double) clipPos * c.ratio;   // file samples

    const int tempCap = fileTemp.getNumSamples();
    float* tempPtrs[2] = { fileTemp.getWritePointer (0), fileTemp.getWritePointer (1) };
    const int outChans = juce::jmin (2, out.getNumChannels());

    while (count > 0)
    {
        int chunk = juce::jmin (count, 512);
        // keep the source span inside the temp buffer
        while (chunk > 1 && (int) (chunk * c.ratio) + 4 > tempCap)
            chunk /= 2;

        const auto readStart = (juce::int64) srcPos;
        int readLen = (int) ((juce::int64) (srcPos + chunk * c.ratio) - readStart) + 3;
        readLen = juce::jmin (readLen, tempCap);

        if (readStart >= c.fileLength)
            return;

        c.reader->read (tempPtrs, 2, readStart, readLen);
        if (c.numFileChannels < 2)
            fileTemp.copyFrom (1, 0, fileTemp, 0, 0, readLen);

        for (int i = 0; i < chunk; ++i)
        {
            const double sp = srcPos + i * c.ratio - (double) readStart;
            const int i0 = juce::jlimit (0, readLen - 2, (int) sp);
            const float frac = (float) (sp - i0);

            float fade = 1.0f;
            const juce::int64 ip = clipPos + i;
            if (c.fadeIn > 0 && ip < c.fadeIn)                       fade *= (float) ip / (float) c.fadeIn;
            if (c.fadeOut > 0 && c.length - ip < c.fadeOut)          fade *= (float) (c.length - ip) / (float) c.fadeOut;
            const float g = c.gain * fade;

            for (int ch = 0; ch < outChans; ++ch)
            {
                const float* src = fileTemp.getReadPointer (ch);
                const float v = src[i0] + frac * (src[i0 + 1] - src[i0]);
                out.addSample (ch, outOff + i, v * g);
            }
        }

        srcPos += chunk * c.ratio;
        clipPos += chunk;
        outOff += chunk;
        count -= chunk;
    }
}

} // namespace dg
