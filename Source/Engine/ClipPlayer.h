#pragma once
#include "Processors.h"

namespace dg
{

class AudioEngine;

// Immutable RT snapshot of one audio track's audible clips (lane 0 only).
struct AudioClipRT
{
    juce::int64 start = 0, length = 0;        // engine samples on the timeline
    double offset = 0;                         // source-file samples
    float gain = 1.0f;
    juce::int64 fadeIn = 0, fadeOut = 0;       // engine samples (manual, linear)
    juce::int64 xfadeIn = 0, xfadeOut = 0;     // auto comp crossfades (equal-power)
    juce::int64 loopLen = 0;                   // content pass length in engine samples; 0 = no loop
    double ratio = 1.0;                        // file samples consumed per engine sample
    std::shared_ptr<juce::AudioFormatReader> reader;
    int numFileChannels = 2;
    juce::int64 fileLength = 0;
};

struct AudioPlaylist { std::vector<AudioClipRT> clips; };

// Comp crossfades: only a PARTIAL head/tail overlap becomes an equal-power
// crossfade (a's tail against b's head). Containment or same-start overlaps
// simply sum - layering clips must never silence one of them.
inline void applyCompCrossfades (std::vector<AudioClipRT>& clips)
{
    std::sort (clips.begin(), clips.end(),
               [] (const AudioClipRT& a, const AudioClipRT& b) { return a.start < b.start; });
    for (size_t i = 0; i < clips.size(); ++i)
        for (size_t j = i + 1; j < clips.size(); ++j)
        {
            auto& a = clips[i];
            auto& b = clips[j];
            const juce::int64 aEnd = a.start + a.length;
            if (b.start >= aEnd) break;
            if (b.start > a.start && b.start + b.length > aEnd)     // partial tail/head only
            {
                const juce::int64 overlap = aEnd - b.start;
                a.xfadeOut = juce::jmax (a.xfadeOut, overlap);
                b.xfadeIn  = juce::jmax (b.xfadeIn, overlap);
            }
        }
}

// Renders one clip's contribution to engine span [blockStart, blockStart+n)
// into out (adds; out keeps prior content). Fades and crossfades span the
// whole clip envelope; loopLen > 0 repeats the source window every pass.
// scratch is the caller's >=2-channel read workspace. Header-inline so the
// headless suite drives the exact playback path without an engine.
inline void renderClipSpan (const AudioClipRT& c, juce::AudioBuffer<float>& out,
                            juce::int64 blockStart, int n, juce::AudioBuffer<float>& scratch)
{
    const juce::int64 s = juce::jmax (blockStart, c.start);
    const juce::int64 e = juce::jmin (blockStart + (juce::int64) n, c.start + c.length);
    if (s >= e || c.reader == nullptr)
        return;

    int outOff = (int) (s - blockStart);
    juce::int64 clipPos = s - c.start;                       // engine samples into the clip
    int count = (int) (e - s);

    const int tempCap = scratch.getNumSamples();
    float* tempPtrs[2] = { scratch.getWritePointer (0), scratch.getWritePointer (1) };
    const int outChans = juce::jmin (2, out.getNumChannels());

    while (count > 0)
    {
        juce::int64 posInPass = clipPos;
        juce::int64 passLeft = std::numeric_limits<juce::int64>::max();
        if (c.loopLen > 0)
        {
            posInPass = clipPos % c.loopLen;
            passLeft = c.loopLen - posInPass;
        }

        int chunk = (int) juce::jmin ((juce::int64) juce::jmin (count, 512), passLeft);
        // keep the source span inside the temp buffer
        while (chunk > 1 && (int) (chunk * c.ratio) + 4 > tempCap)
            chunk /= 2;

        const double srcPos = c.offset + (double) posInPass * c.ratio;   // file samples
        const auto readStart = (juce::int64) srcPos;
        int readLen = (int) ((juce::int64) (srcPos + chunk * c.ratio) - readStart) + 3;
        readLen = juce::jmin (readLen, tempCap);

        if (readStart >= c.fileLength)
        {
            if (c.loopLen <= 0)
                return;
            clipPos += chunk;                                // silent stretch inside a pass
            outOff += chunk;
            count -= chunk;
            continue;
        }

        c.reader->read (tempPtrs, 2, readStart, readLen);
        if (c.numFileChannels < 2)
            scratch.copyFrom (1, 0, scratch, 0, 0, readLen);

        for (int i = 0; i < chunk; ++i)
        {
            const double sp = srcPos + i * c.ratio - (double) readStart;
            const int i0 = juce::jlimit (0, readLen - 2, (int) sp);
            const float frac = (float) (sp - i0);

            float fade = 1.0f;
            const juce::int64 ip = clipPos + i;
            if (c.fadeIn > 0 && ip < c.fadeIn)                       fade *= (float) ip / (float) c.fadeIn;
            if (c.fadeOut > 0 && c.length - ip < c.fadeOut)          fade *= (float) (c.length - ip) / (float) c.fadeOut;
            // comp crossfades: equal-power so overlapping takes sum at unity
            if (c.xfadeIn > 0 && ip < c.xfadeIn)                     fade *= std::sqrt ((float) ip / (float) c.xfadeIn);
            if (c.xfadeOut > 0 && c.length - ip < c.xfadeOut)        fade *= std::sqrt ((float) (c.length - ip) / (float) c.xfadeOut);
            const float g = c.gain * fade;

            for (int ch = 0; ch < outChans; ++ch)
            {
                const float* src = scratch.getReadPointer (ch);
                const float v = src[i0] + frac * (src[i0 + 1] - src[i0]);
                out.addSample (ch, outOff + i, v * g);
            }
        }

        clipPos += chunk;
        outOff += chunk;
        count -= chunk;
    }
}

// One live recording pass (file shared across loop passes; takes sliced on stop).
struct RecordSession
{
    String trackUid;
    File file;
    double sampleRate = 48000.0;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    std::atomic<juce::int64> written { 0 };

    bool slotMode = false;                     // session-slot take: write while playing, no global record

    struct PassMark { juce::int64 fileOffset; double timelineStartSec; };
    std::vector<PassMark> passes;              // audio-thread push within reserved capacity
    std::atomic<bool> needPassMark { true };

    // live waveform for the timeline: audio thread fills, UI reads [0, peakCount)
    juce::HeapBlock<float> peaks;
    std::atomic<int> peakCount { 0 };
    int peakCapacity = 0, samplesPerPeak = 1024;
    float peakAccum = 0.0f;
    int peakAccumCount = 0;
};

// Audio-track source node: plays timeline clips, captures input for recording,
// and feeds input into the insert chain when monitoring "through chain".
class ClipPlayerProcessor : public BasicProcessor
{
public:
    ClipPlayerProcessor (AudioEngine& e, const String& trackUid);

    void prepareToPlay (double sr, int maxBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    void setPlaylist (std::shared_ptr<const AudioPlaylist> p);
    void setSessionClip (std::shared_ptr<const AudioPlaylist> p);   // message thread; engaged later by engine

    std::atomic<int>  monitorMode { 0 };       // 0 off, 1 direct (engine handles), 2 through chain
    std::atomic<bool> armed { false };
    std::atomic<RecordSession*> rec { nullptr };

    // session view: a looping slot overrides the timeline on this track
    std::atomic<bool> sessEngaged { false };
    std::atomic<juce::int64> sessStart { 0 }, sessLen { 0 };

private:
    void renderClip (const AudioClipRT& c, juce::AudioBuffer<float>& out, juce::int64 blockStart, int n);

    AudioEngine& engine;
    String trackUid;

    juce::SpinLock lock;
    std::shared_ptr<const AudioPlaylist> pending, rt;   // rt only touched on audio thread
    std::shared_ptr<const AudioPlaylist> sessPending, sessRT;

    juce::AudioBuffer<float> fileTemp, inputCopy;
};

} // namespace dg
