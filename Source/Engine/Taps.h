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

// master~ injection ring: single writer (a patch), single consumer (the
// master strip). Reads are destructive — a patch that stops writing
// (deleted object, bypassed insert) goes silent instead of freeze-looping
// its last block, which readLast-style taps would do. int64 cursors never
// wrap; index masking does the ring.
struct InjectRing
{
    static constexpr int kSize = 1 << 14, kMask = kSize - 1;

    InjectRing()
    {
        for (auto& c : ring) c.assign ((size_t) kSize, 0.0f);
    }

    void write (const float* l, const float* r, int n)
    {
        n = juce::jmin (n, kSize);
        const juce::int64 w = wTotal.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            ring[0][(size_t) ((w + i) & kMask)] = l[i];
            ring[1][(size_t) ((w + i) & kMask)] = r != nullptr ? r[i] : l[i];
        }
        wTotal.store (w + n, std::memory_order_release);
    }

    void consumeAdd (float* l, float* r, int n)     // adds what's available, then dry
    {
        const juce::int64 w = wTotal.load (std::memory_order_acquire);
        juce::int64 rp = rTotal.load (std::memory_order_relaxed);
        if (w - rp > (juce::int64) (2 * n)) rp = w - n;             // hiccup resync
        const int avail = (int) juce::jmin ((juce::int64) n, juce::jmax ((juce::int64) 0, w - rp));
        for (int i = 0; i < avail; ++i)
        {
            const size_t idx = (size_t) ((rp + i) & kMask);
            if (l != nullptr) l[i] += ring[0][idx];
            if (r != nullptr) r[i] += ring[1][idx];
        }
        rTotal.store (rp + avail, std::memory_order_relaxed);
    }

    std::array<std::vector<float>, 2> ring;
    std::atomic<juce::int64> wTotal { 0 }, rTotal { 0 };
};

// An immutable loaded sample for WIRES sample~ (engine loads via
// createAnyReader and caches; patches share buffers by path).
struct SampleBuf
{
    juce::AudioBuffer<float> buf;
    double sr = 48000.0;
};

// Patch-driven channel strip control (WIRES `strip` object). The patcher
// writes a target value stamped with the strip's block counter; the strip
// honours it only while the stamp stays fresh (<= 1 block old), so deleting
// the cable or the object releases the fader within a block — freshness IS
// the lifecycle, no driven-flag bookkeeping to unwind. cur* flow the other
// way: the strip publishes its effective values for `strip` outlets.
struct StripControl
{
    std::atomic<int>   blockStamp { 0 };                    // strip bumps per block
    std::atomic<float> gainDb { 0 }, pan { 0 }, mute { 0 };
    std::atomic<int>   gainStamp { -9 }, panStamp { -9 }, muteStamp { -9 };
    std::atomic<float> curGainDb { 0 }, curPan { 0 }, curMute { 0 };
};

} // namespace dg
