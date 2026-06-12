#pragma once
#include "../Common.h"
#include <array>

namespace dg
{

// The channel-tap ring behind WIRES' chan~ (NODES E1: channels-as-nodes).
// Single writer (the channel strip, on the audio thread) publishes every
// block; readers copy the most recent n samples. The whole graph renders on
// one thread, so a reader sees this block's audio when the strip ran first
// in the pass and last block's otherwise — Max send~/receive~ semantics.
// Crucially this works for ANY topology: AudioProcessorGraph refuses cycle
// connections, so tapping a downstream bus back into a patch (dub feedback)
// is only possible this way.
struct ChanTap
{
    static constexpr int kSize = 1 << 14, kMask = kSize - 1;

    ChanTap()
    {
        for (auto& c : ring) c.assign ((size_t) kSize, 0.0f);
    }

    void clear()
    {
        for (auto& c : ring) std::fill (c.begin(), c.end(), 0.0f);
    }

    void write (const float* l, const float* r, int n)
    {
        n = juce::jmin (n, kSize);
        const int p = wp.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            ring[0][(size_t) ((p + i) & kMask)] = l[i];
            ring[1][(size_t) ((p + i) & kMask)] = r != nullptr ? r[i] : l[i];
        }
        wp.store ((p + n) & kMask, std::memory_order_release);
    }

    void readLast (float* l, float* r, int n) const
    {
        n = juce::jmin (n, kSize);
        const int p = wp.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
        {
            const size_t idx = (size_t) ((p - n + i + kSize) & kMask);
            if (l != nullptr) l[i] = ring[0][idx];
            if (r != nullptr) r[i] = ring[1][idx];
        }
    }

    float peak (int n = 2048) const            // editor meters (message thread)
    {
        n = juce::jmin (n, kSize);
        const int p = wp.load (std::memory_order_acquire);
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const size_t idx = (size_t) ((p - n + i + kSize) & kMask);
            pk = juce::jmax (pk, std::abs (ring[0][idx]), std::abs (ring[1][idx]));
        }
        return pk;
    }

    std::array<std::vector<float>, 2> ring;
    std::atomic<int> wp { 0 };
};

} // namespace dg
