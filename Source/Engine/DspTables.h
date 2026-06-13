#pragma once
#include <array>
#include <cmath>

// Shared, load-time-built lookup tables so the audio thread never computes a
// transcendental per sample and never pays an init lock. C++17 inline variables
// give exactly one instance across translation units, initialised before first
// use (program load), so the RT thread only ever reads them. Pure, header-only,
// no JUCE dependency - safe to include anywhere, identical on every platform.
namespace dg::tables
{
    inline constexpr double kTwoPi = 6.283185307179586476925286766559;

    // raised-cosine (Hann) window, indexed 0..1024 by grain phase
    inline const std::array<float, 1025> hann = []
    {
        std::array<float, 1025> t {};
        for (size_t i = 0; i < t.size(); ++i)
            t[i] = 0.5f - 0.5f * (float) std::cos (kTwoPi * (double) i / 1024.0);
        return t;
    }();

    // one period of sine; +1 guard sample so linear interp never reads past the end.
    // 8192 points + interp ~= -90 dB THD - transparent for audio-rate oscillators.
    inline constexpr int kSineBits = 13;
    inline constexpr int kSineSize = 1 << kSineBits;          // 8192
    inline const std::array<float, kSineSize + 1> sine = []
    {
        std::array<float, kSineSize + 1> t {};
        for (size_t i = 0; i < t.size(); ++i)
            t[i] = (float) std::sin (kTwoPi * (double) i / (double) kSineSize);
        return t;
    }();

    // phase in [0,1) (caller already wrapped) -> interpolated sine
    inline float sineAt (double phase01) noexcept
    {
        const double x = phase01 * (double) kSineSize;
        const int i = (int) x;                                // phase01 in [0,1) => i in [0, kSineSize)
        const float f = (float) (x - (double) i);
        return sine[(size_t) i] + (sine[(size_t) i + 1] - sine[(size_t) i]) * f;
    }
}
