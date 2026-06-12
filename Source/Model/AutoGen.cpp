#include "AutoGen.h"

namespace dg::autogen
{

static void addPoint (ValueTree& lane, juce::UndoManager* undo, double t, double v)
{
    ValueTree p (id::PT);
    p.setProperty (id::t, t, nullptr);
    p.setProperty (id::v, juce::jlimit (0.0, 1.0, v), nullptr);
    lane.appendChild (p, undo);
}

int generate (SessionModel& session, const TempoMap& map, ValueTree lane, Gen gen,
              double t0, double t1, juce::int64 seed)
{
    if (! lane.isValid() || t1 <= t0) return 0;
    session.undo.beginNewTransaction ("generate automation");

    for (int i = lane.getNumChildren(); --i >= 0;)
    {
        auto p = lane.getChild (i);
        if (! p.hasType (id::PT)) continue;
        const double t = p[id::t];
        if (t >= t0 - 1.0e-9 && t <= t1 + 1.0e-9)
            lane.removeChild (i, &session.undo);
    }

    juce::Random rng (seed);
    int written = 0;
    constexpr int kMaxPts = 1024;
    const double b0 = map.secondsToBeats (t0), b1 = map.secondsToBeats (t1);

    if (gen == Gen::lorenz)
    {
        double x = rng.nextDouble() * 20.0 - 10.0;
        double y = rng.nextDouble() * 20.0 - 10.0;
        double z = rng.nextDouble() * 20.0 + 10.0;
        auto integrate = [&x, &y, &z] (int steps)
        {
            constexpr double dt = 0.004, sigma = 10.0, rho = 28.0, beta = 8.0 / 3.0;
            for (int i = 0; i < steps; ++i)
            {
                const double dx = sigma * (y - x);
                const double dy = x * (rho - z) - y;
                const double dz = x * y - beta * z;
                x += dx * dt; y += dy * dt; z += dz * dt;
            }
        };
        integrate (800);                                       // land on the attractor first

        double step = 0.25;                                    // 16ths
        while ((b1 - b0) / step > kMaxPts) step *= 2.0;
        for (double b = b0; b <= b1 + 1.0e-9 && written < kMaxPts; b += step)
        {
            integrate (10);
            addPoint (lane, &session.undo, map.beatsToSeconds (b), 0.5 + x / 40.0);
            ++written;
        }
    }
    else if (gen == Gen::drunk)
    {
        double step = 0.25;
        while ((b1 - b0) / step > kMaxPts) step *= 2.0;
        double v = 0.3 + rng.nextDouble() * 0.4;
        for (double b = b0; b <= b1 + 1.0e-9 && written < kMaxPts; b += step)
        {
            addPoint (lane, &session.undo, map.beatsToSeconds (b), v);
            ++written;
            v += (rng.nextDouble() * 2.0 - 1.0) * 0.12;
            if (v < 0.0) v = -v;                               // reflect, don't stick
            if (v > 1.0) v = 2.0 - v;
        }
    }
    else // ratchet: per-beat bursts of decaying spikes
    {
        static const int kHits[] = { 2, 3, 4, 6, 8 };
        for (double beat = b0; beat < b1 - 1.0e-9 && written < kMaxPts - 2; beat += 1.0)
        {
            const int hits = kHits[rng.nextInt (5)];
            const double decay = 0.55 + rng.nextDouble() * 0.25;
            const double beatLen = juce::jmin (1.0, b1 - beat);
            const double spacing = beatLen / hits;
            double amp = 0.85 + rng.nextDouble() * 0.15;
            for (int h = 0; h < hits && written < kMaxPts - 2; ++h)
            {
                const double hb = beat + h * spacing;
                addPoint (lane, &session.undo, map.beatsToSeconds (hb), amp);
                addPoint (lane, &session.undo, map.beatsToSeconds (hb + spacing * 0.9), 0.03);
                written += 2;
                amp *= decay;
            }
        }
    }
    return written;
}

} // namespace dg::autogen
