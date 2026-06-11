#pragma once
#include "../Engine/TempoMap.h"
#include <set>

namespace dg
{

// Cross-view UI state owned by MainComponent.
struct UIState
{
    int snapMode = 5;                       // index into kSnapNames
    std::set<String> selectedClips;         // clip uids
    String selectedTrack;                   // uid; FX explorer double-click target
    std::function<void (ValueTree clip)> openPianoRoll;
    std::function<void (const String& trackUid, const String& insertUid)> openInsertEditor;
    std::function<void()> refreshAll;
};

static const juce::StringArray kSnapNames { "SNAP OFF", "BAR", "1/2", "1/4", "1/8", "1/16", "1/32", "FRAME" };
static constexpr double kSnapBeats[] = { 0.0, -1.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0 };

inline double snapSeconds (double sec, int mode, const TempoMap& map, double fps)
{
    if (mode <= 0) return sec;
    if (mode == 7) return fps > 0 ? std::round (sec * fps) / fps : sec;
    const double beats = map.secondsToBeats (juce::jmax (0.0, sec));
    double snapped;
    if (mode == 1)
    {
        auto bb = map.barBeatAt (beats);
        const double bpb = bb.num * 4.0 / bb.den;
        snapped = bb.beatInBar < bpb * 0.5 ? bb.barStartBeat : bb.barStartBeat + bpb;
    }
    else
    {
        const double g = kSnapBeats[mode];
        snapped = std::round (beats / g) * g;
    }
    return map.beatsToSeconds (juce::jmax (0.0, snapped));
}

} // namespace dg
