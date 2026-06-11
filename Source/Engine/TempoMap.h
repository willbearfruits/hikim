#pragma once
#include "../Model/Ids.h"

namespace dg
{

// Immutable snapshot of the tempo / time-signature map. The engine swaps a
// shared_ptr<const TempoMap> whenever the session's TEMPOMAP subtree changes,
// so audio-thread queries never race the editor.
class TempoMap
{
public:
    struct TempoEvent { double beat;  double bpm; };           // beat = quarter notes from 0
    struct SigEvent   { double beat;  int num; int den; };

    TempoMap (double sampleRate = 48000.0);
    TempoMap (const ValueTree& tempoMapTree, double sampleRate);

    double getSampleRate() const noexcept       { return sr; }

    double beatsToSeconds (double beats) const noexcept;
    double secondsToBeats (double seconds) const noexcept;
    juce::int64 beatsToSamples (double beats) const noexcept;
    double samplesToBeats (juce::int64 samples) const noexcept;

    double bpmAtBeat (double beat) const noexcept;
    SigEvent sigAtBeat (double beat) const noexcept;

    struct BarBeat { int bar; double beatInBar; int num; int den; double barStartBeat; };
    BarBeat barBeatAt (double beats) const noexcept;            // bar is 0-based
    double barStartBeat (int barIndex) const noexcept;
    double beatsPerBarAt (double beat) const noexcept;

    String formatBarsBeats (juce::int64 samples) const;
    String formatTimecode  (juce::int64 samples) const;

    const std::vector<TempoEvent>& getTempos() const noexcept { return tempos; }
    const std::vector<SigEvent>&   getSigs()   const noexcept { return sigs; }

private:
    void rebuildAnchors();

    double sr;
    std::vector<TempoEvent> tempos;     // sorted by beat, first at beat 0
    std::vector<SigEvent>   sigs;       // sorted by beat, first at beat 0
    std::vector<double> tempoSecondsAnchors;  // seconds position of each tempo event
};

} // namespace dg
