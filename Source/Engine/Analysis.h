#pragma once
#include "../Common.h"

namespace dg
{

// Offline tempo estimate: onset-strength envelope (half-wave rectified energy
// flux) autocorrelated across 60-200 BPM, folded into the 85-180 dance range.
// Tuned for breaks and loops, not rubato ballads.
// EXTEND: live beat-tracking of the input bus for tap-free sync to a band.
inline double estimateBpmFromReader (juce::AudioFormatReader& reader)
{
    const double sr = reader.sampleRate;
    if (sr <= 0 || reader.lengthInSamples < (juce::int64) sr)
        return 0.0;

    const int hop = 512;
    const juce::int64 maxSamples = juce::jmin (reader.lengthInSamples, (juce::int64) (sr * 30.0));
    const int chans = juce::jmin (2, (int) reader.numChannels);

    juce::AudioBuffer<float> buf (chans, hop);
    std::vector<float> flux;
    flux.reserve ((size_t) (maxSamples / hop) + 1);
    float prev = 0.0f;

    for (juce::int64 p = 0; p + hop <= maxSamples; p += hop)
    {
        reader.read (buf.getArrayOfWritePointers(), chans, p, hop);
        float e = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
        {
            const float* d = buf.getReadPointer (ch);
            for (int i = 0; i < hop; ++i)
                e += d[i] * d[i];
        }
        e = std::sqrt (e / (float) (hop * chans));
        flux.push_back (juce::jmax (0.0f, e - prev));
        prev = e;
    }
    if (flux.size() < 128)
        return 0.0;

    const double hopsPerSec = sr / hop;
    double bestBpm = 0.0, bestScore = 0.0;

    for (double bpm = 60.0; bpm <= 200.0; bpm += 0.25)
    {
        const int l1 = (int) std::round (hopsPerSec * 60.0 / bpm);
        const int l2 = l1 * 2;
        if (l2 + 4 >= (int) flux.size()) continue;
        double s = 0.0;
        const size_t n = flux.size() - (size_t) l2;
        for (size_t i = 0; i < n; ++i)
            s += flux[i] * flux[i + (size_t) l1] + 0.5 * flux[i] * flux[i + (size_t) l2];
        s /= (double) n;
        if (s > bestScore) { bestScore = s; bestBpm = bpm; }
    }

    if (bestBpm <= 0.0) return 0.0;
    while (bestBpm < 85.0)  bestBpm *= 2.0;
    while (bestBpm > 180.0) bestBpm /= 2.0;
    return bestBpm;
}

} // namespace dg
