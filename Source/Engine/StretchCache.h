#pragma once
#include "../Common.h"

namespace dg
{

// Pitch-locked time-stretch as a render cache: a background thread runs
// RubberBand offline over the source file and the playlist swaps to the
// stretched render when it's ready (varispeed plays in the meantime).
// EXTEND: live RT RubberBand per clip for instant scrubbing of stretch.
class StretchCache : private juce::Thread
{
public:
    explicit StretchCache (juce::AudioFormatManager& fm);
    ~StretchCache() override;

    // Returns the stretched file if cached. Otherwise schedules a background
    // render and returns an invalid File; onReady fires on the message thread
    // when the render lands (never fires on failure).
    File get (const File& source, double ratio, std::function<void()> onReady);

    static constexpr bool available()
    {
       #if DG_HAVE_RUBBERBAND
        return true;
       #else
        return false;
       #endif
    }

private:
    void run() override;
    bool renderJob (const File& src, double ratio, const File& dest);
    File cacheFileFor (const File& src, double ratio) const;

    juce::AudioFormatManager& formats;
    File dir;

    struct Job { File src; double ratio; File dest; std::function<void()> onReady; };
    juce::CriticalSection lock;
    std::vector<Job> jobs;
    juce::StringArray inFlightOrFailed;
    juce::WaitableEvent wake;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchCache)
};

} // namespace dg
