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
            tempos.push_back ({ (double) c[id::beat], (double) c[id::bpm] });
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

void TempoMap::rebuildAnchors()
{
    tempoSecondsAnchors.resize (tempos.size());
    double secs = 0.0;
    tempoSecondsAnchors[0] = 0.0;
    for (size_t i = 1; i < tempos.size(); ++i)
    {
        secs += (tempos[i].beat - tempos[i - 1].beat) * 60.0 / tempos[i - 1].bpm;
        tempoSecondsAnchors[i] = secs;
    }
}

double TempoMap::beatsToSeconds (double beats) const noexcept
{
    size_t i = tempos.size() - 1;
    while (i > 0 && tempos[i].beat > beats) --i;
    return tempoSecondsAnchors[i] + (beats - tempos[i].beat) * 60.0 / tempos[i].bpm;
}

double TempoMap::secondsToBeats (double seconds) const noexcept
{
    size_t i = tempos.size() - 1;
    while (i > 0 && tempoSecondsAnchors[i] > seconds) --i;
    return tempos[i].beat + (seconds - tempoSecondsAnchors[i]) * tempos[i].bpm / 60.0;
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
    return tempos[i].bpm;
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
