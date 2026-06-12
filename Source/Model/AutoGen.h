#pragma once
#include "Session.h"
#include "../Engine/TempoMap.h"

namespace dg::autogen
{
    // Chaos automation: "generate" on an automation lane draws parameter
    // movement with the same math as the canvas pieces. Pure tree edits
    // (PT children, t in seconds / v in 0..1) in one undo transaction,
    // deterministic for a given seed.
    enum class Gen { lorenz, drunk, ratchet };

    // Replaces the lane's points inside [t0, t1] with a generated curve
    // (points outside the region survive). Returns the points written.
    int generate (SessionModel&, const TempoMap&, ValueTree lane, Gen,
                  double t0, double t1, juce::int64 seed);
}
