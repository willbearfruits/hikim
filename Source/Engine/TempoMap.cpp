#include "TempoMap.h"

namespace dg
{

TempoMap::TempoMap (double sampleRate) : sr (sampleRate)
{
    tempos.push_back ({ 0.0, 120.0 });
    sigs.push_back ({ 0.0, 4, 4 });
    rebuildAnchors();
}

TempoMap::TempoMap (const ValueTree& tree, double sampleRate) : sr (sampleRate)
{
    for (const auto& c : tree)
    {
        if (c.hasType (id::TEMPO))
            tempos.push_back ({ (double) c[id::beat], (double) c[id::bpm],
                                (bool) c.getProperty (id::ramp, false) });
        else if (c.hasType (id::TIMESIG))
            sigs.push_back ({ (double) c[id::beat], (int) c[id::num], (int) c[id::den] });
    }

    auto byBeat = [] (auto& a, auto& b) { return a.beat < b.beat; };
    std::sort (tempos.begin(), tempos.end(), byBeat);
    std::sort (sigs.begin(),   sigs.end(),   byBeat);

    if (tempos.empty() || tempos.front().beat > 0.0) tempos.insert (tempos.begin(), { 0.0, 120.0 });
    if (sigs.empty()   || sigs.front().beat   > 0.0) sigs.insert   (sigs.begin(),   { 0.0, 4, 4 });

    for (auto& t : tempos) t.bpm = juce::jlimit (10.0, 999.0, t.bpm);
    rebuildAnchors();
}

// A ramped segment glides bpm linearly in beats, so elapsed time follows a
// log law: t(b) = 60/slope * ln (bpm(b)/bpm0). Its closed-form inverse keeps
// beatsToSeconds/secondsToBeats exact inverses. Equal endpoint bpms (or no
// next event) degrade to the constant-tempo segment.
static bool rampActive (const std::vector<TempoMap::TempoEvent>& tempos, size_t i)
{
    return tempos[i].ramp && i + 1 < tempos.size()
        && std::abs (tempos[i + 1].bpm - tempos[i].bpm) >= 1.0e-9
        && tempos[i + 1].beat - tempos[i].beat >= 1.0e-9;
}

void TempoMap::rebuildAnchors()
{
    tempoSecondsAnchors.resize (tempos.size());
    double secs = 0.0;
    tempoSecondsAnchors[0] = 0.0;
    for (size_t i = 1; i < tempos.size(); ++i)
    {
        const auto& a = tempos[i - 1];
        const auto& b = tempos[i];
        if (rampActive (tempos, i - 1))
        {
            const double slope = (b.bpm - a.bpm) / (b.beat - a.beat);
            secs += 60.0 / slope * std::log (b.bpm / a.bpm);
        }
        else
            secs += (b.beat - a.beat) * 60.0 / a.bpm;
        tempoSecondsAnchors[i] = secs;
    }
}

double TempoMap::beatsToSeconds (double beats) const noexcept
{
    size_t i = tempos.size() - 1;
    while (i > 0 && tempos[i].beat > beats) --i;
    const auto& ev = tempos[i];
    if (beats > ev.beat && rampActive (tempos, i))
    {
        const auto& nx = tempos[i + 1];
        const double slope = (nx.bpm - ev.bpm) / (nx.beat - ev.beat);
        const double bpmAt = ev.bpm + (beats - ev.beat) * slope;
        return tempoSecondsAnchors[i] + 60.0 / slope * std::log (bpmAt / ev.bpm);
    }
    return tempoSecondsAnchors[i] + (beats - ev.beat) * 60.0 / ev.bpm;
}

double TempoMap::secondsToBeats (double seconds) const noexcept
{
    size_t i = tempos.size() - 1;
    while (i > 0 && tempoSecondsAnchors[i] > seconds) --i;
    const auto& ev = tempos[i];
    if (seconds > tempoSecondsAnchors[i] && rampActive (tempos, i))
    {
        const auto& nx = tempos[i + 1];
        const double slope = (nx.bpm - ev.bpm) / (nx.beat - ev.beat);
        const double bpmAt = ev.bpm * std::exp (slope * (seconds - tempoSecondsAnchors[i]) / 60.0);
        return ev.beat + (bpmAt - ev.bpm) / slope;
    }
    return ev.beat + (seconds - tempoSecondsAnchors[i]) * ev.bpm / 60.0;
}

juce::int64 TempoMap::beatsToSamples (double beats) const noexcept
{
    return (juce::int64) std::llround (beatsToSeconds (beats) * sr);
}

double TempoMap::samplesToBeats (juce::int64 samples) const noexcept
{
    return secondsToBeats ((double) samples / sr);
}

double TempoMap::bpmAtBeat (double beat) const noexcept
{
    size_t i = tempos.size() - 1;
    while (i > 0 && tempos[i].beat > beat) --i;
    const auto& ev = tempos[i];
    if (beat > ev.beat && rampActive (tempos, i))
    {
        const auto& nx = tempos[i + 1];
        const double u = juce::jlimit (0.0, 1.0, (beat - ev.beat) / (nx.beat - ev.beat));
        return ev.bpm + (nx.bpm - ev.bpm) * u;
    }
    return ev.bpm;
}

TempoMap::SigEvent TempoMap::sigAtBeat (double beat) const noexcept
{
    size_t i = sigs.size() - 1;
    while (i > 0 && sigs[i].beat > beat) --i;
    return sigs[i];
}

double TempoMap::beatsPerBarAt (double beat) const noexcept
{
    auto s = sigAtBeat (beat);
    return s.num * 4.0 / s.den;
}

TempoMap::BarBeat TempoMap::barBeatAt (double beats) const noexcept
{
    // Walk signature segments accumulating whole bars.
    int bar = 0;
    for (size_t i = 0; i < sigs.size(); ++i)
    {
        const double segStart = sigs[i].beat;
        const double segEnd = (i + 1 < sigs.size()) ? sigs[i + 1].beat : std::numeric_limits<double>::max();
        const double bpb = sigs[i].num * 4.0 / sigs[i].den;

        if (beats < segEnd)
        {
            const int barsIn = (int) std::floor (juce::jmax (0.0, beats - segStart) / bpb);
            const double barStart = segStart + barsIn * bpb;
            return { bar + barsIn, beats - barStart, sigs[i].num, sigs[i].den, barStart };
        }
        bar += (int) std::ceil ((segEnd - segStart) / bpb - 1.0e-9);
    }
    return { 0, 0.0, 4, 4, 0.0 };
}

double TempoMap::barStartBeat (int barIndex) const noexcept
{
    int bar = 0;
    for (size_t i = 0; i < sigs.size(); ++i)
    {
        const double segStart = sigs[i].beat;
        const double segEnd = (i + 1 < sigs.size()) ? sigs[i + 1].beat : std::numeric_limits<double>::max();
        const double bpb = sigs[i].num * 4.0 / sigs[i].den;
        const int barsInSeg = (segEnd == std::numeric_limits<double>::max())
                                  ? std::numeric_limits<int>::max()
                                  : (int) std::ceil ((segEnd - segStart) / bpb - 1.0e-9);
        if (barIndex < bar + barsInSeg || i + 1 == sigs.size())
            return segStart + (barIndex - bar) * bpb;
        bar += barsInSeg;
    }
    return 0.0;
}

String TempoMap::formatBarsBeats (juce::int64 samples) const
{
    auto bb = barBeatAt (samplesToBeats (samples));
    const double beatUnit = 4.0 / bb.den;
    const int beatNum = (int) std::floor (bb.beatInBar / beatUnit);
    const int ticks = (int) std::floor ((bb.beatInBar / beatUnit - beatNum) * 960.0);
    return String (bb.bar + 1) + "." + String (beatNum + 1) + "." + String (ticks).paddedLeft ('0', 3);
}

String TempoMap::formatTimecode (juce::int64 samples) const
{
    double s = (double) samples / sr;
    const int h = (int) (s / 3600.0); s -= h * 3600.0;
    const int m = (int) (s / 60.0);   s -= m * 60.0;
    const int sec = (int) s;
    const int ms = (int) std::round ((s - sec) * 1000.0);
    return String (h).paddedLeft ('0', 2) + ":" + String (m).paddedLeft ('0', 2) + ":"
         + String (sec).paddedLeft ('0', 2) + "." + String (ms).paddedLeft ('0', 3);
}

// ---- tap tempo ---------------------------------------------------------------

double TapTempo::tap (double nowMs)
{
    if (! times.empty() && nowMs - times.back() > 2500.0)
        times.clear();
    times.push_back (nowMs);
    if (times.size() > 9)
        times.erase (times.begin());                    // keep the last 8 intervals
    if (times.size() < 2)
        return 0.0;
    const double avg = (times.back() - times.front()) / (double) (times.size() - 1);
    return avg > 1.0e-3 ? juce::jlimit (10.0, 999.0, 60000.0 / avg) : 0.0;
}

void applyTapTempo (ValueTree tm, juce::UndoManager* undo, double beat, double bpm)
{
    bpm = juce::jlimit (10.0, 999.0, bpm);
    ValueTree best;
    double bestBeat = -1.0;
    for (auto c : tm)
        if (c.hasType (id::TEMPO))
        {
            const double b = c[id::beat];
            if (b <= beat + 1.0e-9 && b > bestBeat) { bestBeat = b; best = c; }
        }
    if (best.isValid())
        best.setProperty (id::bpm, bpm, undo);
    else
    {
        ValueTree ev (id::TEMPO);
        ev.setProperty (id::beat, 0.0, nullptr);
        ev.setProperty (id::bpm, bpm, nullptr);
        tm.appendChild (ev, undo);
    }
}

} // namespace dg
