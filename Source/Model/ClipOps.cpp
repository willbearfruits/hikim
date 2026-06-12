#include "ClipOps.h"

namespace dg::clipops
{

juce::StringArray splitAt (SessionModel& session, const TempoMap& map,
                           const std::set<String>& selected, double ph)
{
    juce::StringArray newUids;
    session.undo.beginNewTransaction ("split");

    for (auto track : session.tracks())
    {
        // snapshot first: we append while editing
        std::vector<ValueTree> toSplit;
        for (auto clip : SessionModel::clipsOf (track))
            if (selected.count (clip[id::uid].toString()) > 0)
                toSplit.push_back (clip);

        for (auto clip : toSplit)
        {
            const double s = clip[id::start], len = clip[id::length];
            if (ph <= s + 0.01 || ph >= s + len - 0.01) continue;

            auto right = clip.createCopy();
            right.setProperty (id::uid, SessionModel::newUID(), nullptr);
            right.setProperty (id::start, ph, nullptr);
            right.setProperty (id::length, s + len - ph, nullptr);
            right.setProperty (id::fadeIn, 0.0, nullptr);

            const bool looped = (bool) clip.getProperty (id::loop, false);

            if (clip[id::type].toString() == "audio")
            {
                const double fileSR = clip.getProperty (id::fileSR, 48000.0);
                const double stretch = clip.getProperty (id::stretch, 1.0);
                double d = ph - s;
                const double passSec = (double) clip.getProperty (id::loopLen, 0.0);
                if (looped && passSec > 1.0e-9)
                    d = std::fmod (d, passSec);        // passes re-anchor at the new clip start
                right.setProperty (id::offset, (double) clip[id::offset] + d * fileSR / stretch, nullptr);
            }
            else
            {
                const double splitBeat = map.secondsToBeats (ph) - map.secondsToBeats (s);
                const double loopBeats = (double) clip.getProperty (id::loopBeats, 0.0);
                auto rnotes = right.getChildWithName (id::NOTES);
                if (looped && loopBeats > 1.0e-9)
                {
                    // both halves keep looping the full pass; the right one
                    // starts mid-pass, so rotate its audible notes to phase
                    double phase = std::fmod (splitBeat, loopBeats);
                    if (phase < 0) phase += loopBeats;
                    for (auto n : rnotes)
                    {
                        const double nb = (double) n[id::beat];
                        if (nb >= loopBeats) continue;             // silent extras stay put
                        double rb = std::fmod (nb - phase, loopBeats);
                        if (rb < 0) rb += loopBeats;
                        n.setProperty (id::beat, rb, nullptr);
                    }
                }
                else
                {
                    for (int i = rnotes.getNumChildren(); --i >= 0;)
                    {
                        auto n = rnotes.getChild (i);
                        const double nb = (double) n[id::beat] - splitBeat;
                        if (nb < 0) rnotes.removeChild (i, nullptr);
                        else n.setProperty (id::beat, nb, nullptr);
                    }
                    auto lnotes = clip.getChildWithName (id::NOTES);
                    for (int i = lnotes.getNumChildren(); --i >= 0;)
                        if ((double) lnotes.getChild (i)[id::beat] >= splitBeat)
                            lnotes.removeChild (i, nullptr);
                }
            }

            clip.setProperty (id::length, ph - s, &session.undo);
            clip.setProperty (id::fadeOut, 0.0, &session.undo);
            SessionModel::clipsOf (track).appendChild (right, &session.undo);
            newUids.add (right[id::uid].toString());
        }
    }
    return newUids;
}

void deleteClips (SessionModel& session, const std::set<String>& uids)
{
    session.undo.beginNewTransaction ("delete clips");
    for (auto track : session.tracks())
    {
        auto clips = SessionModel::clipsOf (track);
        for (int i = clips.getNumChildren(); --i >= 0;)
            if (uids.count (clips.getChild (i)[id::uid].toString()) > 0)
                clips.removeChild (i, &session.undo);
    }
}

void rippleDelete (SessionModel& session, const std::set<String>& uids)
{
    session.undo.beginNewTransaction ("ripple delete");
    for (auto track : session.tracks())
    {
        std::vector<ValueTree> sel;
        for (auto clip : SessionModel::clipsOf (track))
            if (uids.count (clip[id::uid].toString()) > 0)
                sel.push_back (clip);
        std::sort (sel.begin(), sel.end(),
                   [] (const ValueTree& a, const ValueTree& b)
                   { return (double) a[id::start] > (double) b[id::start]; });   // rightmost first

        for (auto& clip : sel)
        {
            const double s = clip[id::start], len = clip[id::length];
            auto clips = SessionModel::clipsOf (track);
            clips.removeChild (clip, &session.undo);
            for (auto other : clips)
                if ((double) other[id::start] >= s)
                    other.setProperty (id::start,
                                       juce::jmax (0.0, (double) other[id::start] - len), &session.undo);
        }
    }
}

std::vector<ClipboardItem> copyClips (SessionModel& session, const std::set<String>& uids)
{
    std::vector<ClipboardItem> out;
    for (auto track : session.tracks())
        for (auto clip : SessionModel::clipsOf (track))
            if (uids.count (clip[id::uid].toString()) > 0)
                out.push_back ({ track[id::uid].toString(), clip.createCopy() });
    return out;
}

juce::StringArray paste (SessionModel& session, const std::vector<ClipboardItem>& items, double atSec)
{
    juce::StringArray newUids;
    if (items.empty()) return newUids;

    double earliest = std::numeric_limits<double>::max();
    for (const auto& it : items)
        earliest = juce::jmin (earliest, (double) it.clip[id::start]);

    session.undo.beginNewTransaction ("paste");
    for (const auto& it : items)
    {
        auto track = session.findTrack (it.trackUid);
        if (! track.isValid()) continue;
        auto copy = it.clip.createCopy();
        copy.setProperty (id::uid, SessionModel::newUID(), nullptr);
        copy.setProperty (id::start, atSec + ((double) it.clip[id::start] - earliest), nullptr);
        SessionModel::clipsOf (track).appendChild (copy, &session.undo);
        newUids.add (copy[id::uid].toString());
    }
    return newUids;
}

void setLoop (SessionModel& session, const TempoMap& map, const std::set<String>& uids, bool on,
              const std::function<double (const ValueTree&)>& contentLenSecFor)
{
    session.undo.beginNewTransaction (on ? "loop clip" : "unloop clip");
    for (auto track : session.tracks())
        for (auto clip : SessionModel::clipsOf (track))
        {
            if (uids.count (clip[id::uid].toString()) == 0) continue;
            clip.setProperty (id::loop, on, &session.undo);
            if (! on) continue;                                    // pass length kept for re-enable

            const double len = clip[id::length];
            if (clip[id::type].toString() == "audio")
            {
                if ((double) clip.getProperty (id::loopLen, 0.0) > 1.0e-4) continue;
                double pass = len;
                if (contentLenSecFor != nullptr)
                {
                    const double avail = contentLenSecFor (clip);
                    if (avail > 1.0e-4) pass = juce::jmin (pass, avail);
                }
                clip.setProperty (id::loopLen, pass, &session.undo);
            }
            else
            {
                if ((double) clip.getProperty (id::loopBeats, 0.0) > 1.0e-4) continue;
                const double s = clip[id::start];
                const double beats = map.secondsToBeats (s + len) - map.secondsToBeats (s);
                clip.setProperty (id::loopBeats, juce::jmax (1.0, std::round (beats)), &session.undo);
            }
        }
}

void slip (SessionModel& session, const TempoMap& map, const std::set<String>& uids,
           double deltaSec, bool newTransaction)
{
    if (newTransaction)
        session.undo.beginNewTransaction ("slip");

    for (auto track : session.tracks())
        for (auto clip : SessionModel::clipsOf (track))
        {
            if (uids.count (clip[id::uid].toString()) == 0) continue;

            if (clip[id::type].toString() == "audio")
            {
                const double fileSR = clip.getProperty (id::fileSR, 48000.0);
                const double stretch = clip.getProperty (id::stretch, 1.0);
                clip.setProperty (id::offset,
                                  juce::jmax (0.0, (double) clip[id::offset] - deltaSec * fileSR / stretch),
                                  &session.undo);
            }
            else
            {
                const double s = clip[id::start];
                const double dBeats = map.secondsToBeats (s + deltaSec) - map.secondsToBeats (s);
                const bool looped = (bool) clip.getProperty (id::loop, false);
                const double loopBeats = (double) clip.getProperty (id::loopBeats, 0.0);
                for (auto n : clip.getChildWithName (id::NOTES))
                {
                    const double nb = (double) n[id::beat];
                    double moved = nb + dBeats;
                    if (looped && loopBeats > 1.0e-9 && nb < loopBeats)    // rotate the audible pass
                    {
                        moved = std::fmod (moved, loopBeats);
                        if (moved < 0) moved += loopBeats;
                    }
                    n.setProperty (id::beat, moved, &session.undo);
                }
            }
        }
}

juce::StringArray duplicate (SessionModel& session, const std::set<String>& uids)
{
    juce::StringArray newUids;
    session.undo.beginNewTransaction ("duplicate");
    for (auto track : session.tracks())
    {
        std::vector<ValueTree> sel;
        for (auto clip : SessionModel::clipsOf (track))
            if (uids.count (clip[id::uid].toString()) > 0)
                sel.push_back (clip);
        for (auto& clip : sel)
        {
            auto copy = clip.createCopy();
            copy.setProperty (id::uid, SessionModel::newUID(), nullptr);
            copy.setProperty (id::start, (double) clip[id::start] + (double) clip[id::length], nullptr);
            SessionModel::clipsOf (track).appendChild (copy, &session.undo);
            newUids.add (copy[id::uid].toString());
        }
    }
    return newUids;
}

// ---- crossfade handles ------------------------------------------------------

static bool audibleAudio (const ValueTree& c)
{
    return (int) c.getProperty (id::lane, 0) == 0 && c[id::type].toString() == "audio";
}

// a's tail against b's head - the shape applyCompCrossfades fades
static bool partialOverlap (const ValueTree& a, const ValueTree& b)
{
    const double aS = a[id::start], aE = aS + (double) a[id::length];
    const double bS = b[id::start], bE = bS + (double) b[id::length];
    return bS > aS && bS < aE && bE > aE;
}

Overlap overlapAt (ValueTree track, double timeSec)
{
    auto clips = SessionModel::clipsOf (track);
    for (auto a : clips)
    {
        if (! audibleAudio (a)) continue;
        for (auto b : clips)
        {
            if (b == a || ! audibleAudio (b) || ! partialOverlap (a, b)) continue;
            const double s = b[id::start], e = (double) a[id::start] + (double) a[id::length];
            if (timeSec >= s && timeSec <= e)
                return { a, b, s, e };
        }
    }
    return {};
}

std::vector<Overlap> overlapsOf (ValueTree clip)
{
    std::vector<Overlap> out;
    auto clips = clip.getParent();
    if (! clips.isValid() || ! audibleAudio (clip)) return out;
    for (auto other : clips)
    {
        if (other == clip || ! audibleAudio (other)) continue;
        if (partialOverlap (clip, other))
            out.push_back ({ clip, other, (double) other[id::start],
                             (double) clip[id::start] + (double) clip[id::length] });
        if (partialOverlap (other, clip))
            out.push_back ({ other, clip, (double) clip[id::start],
                             (double) other[id::start] + (double) other[id::length] });
    }
    return out;
}

// Move an audio clip's content reference when its start shifts by dSec
// (loop-aware: a looping clip wraps its pass phase instead).
static void shiftStartContent (SessionModel& session, ValueTree clip, double dSec)
{
    if (clip[id::type].toString() != "audio") return;
    const double fileSR = clip.getProperty (id::fileSR, 48000.0);
    const double stretch = clip.getProperty (id::stretch, 1.0);
    double d = dSec;
    const double passSec = (double) clip.getProperty (id::loopLen, 0.0);
    if ((bool) clip.getProperty (id::loop, false) && passSec > 1.0e-4)
    {
        d = std::fmod (d, passSec);
        if (d < 0) d += passSec;
    }
    clip.setProperty (id::offset,
                      juce::jmax (0.0, (double) clip[id::offset] + d * fileSR / stretch),
                      &session.undo);
}

// How far a clip's start may move earlier before its source runs out
// (non-looping audio only; a looping clip wraps and is unconstrained).
static double minStartShift (const ValueTree& clip)
{
    if (clip[id::type].toString() != "audio") return -1.0e18;
    if ((bool) clip.getProperty (id::loop, false)) return -1.0e18;
    const double fileSR = clip.getProperty (id::fileSR, 48000.0);
    const double stretch = clip.getProperty (id::stretch, 1.0);
    return -(double) clip[id::offset] * stretch / fileSR;
}

double rollBoundary (SessionModel& session, ValueTree left, ValueTree right,
                     double deltaSec, bool newTransaction)
{
    if (! left.isValid() || ! right.isValid()) return 0.0;
    const double lS = left[id::start],  lL = left[id::length];
    const double rS = right[id::start], rL = right[id::length];
    const double overlap = lS + lL - rS;

    double d = deltaSec;
    d = juce::jmax (d, 0.05 - lL);                       // left keeps a body
    d = juce::jmax (d, lS - rS + 0.001);                 // stays a partial overlap...
    d = juce::jmin (d, rL - juce::jmax (0.05, overlap + 0.001));   // ...on both sides
    d = juce::jmax (d, minStartShift (right));           // right content can't pre-date its file
    if (std::abs (d) < 1.0e-9) return 0.0;

    if (newTransaction) session.undo.beginNewTransaction ("roll crossfade");
    left.setProperty  (id::length, lL + d, &session.undo);
    right.setProperty (id::start,  rS + d, &session.undo);
    right.setProperty (id::length, rL - d, &session.undo);
    shiftStartContent (session, right, d);               // content stays anchored in time
    return d;
}

double resizeOverlap (SessionModel& session, ValueTree left, ValueTree right,
                      double newOverlapSec, bool newTransaction)
{
    if (! left.isValid() || ! right.isValid()) return 0.0;
    const double lS = left[id::start],  lL = left[id::length], lE = lS + lL;
    const double rS = right[id::start], rL = right[id::length], rE = rS + rL;
    const double centre = (rS + lE) * 0.5;

    double ov = juce::jmax (0.0, newOverlapSec);
    ov = juce::jmin (ov, 2.0 * (centre - lS) - 0.05);    // right.start stays after left.start
    ov = juce::jmin (ov, 2.0 * (rE - centre) - 0.05);    // left.end stays before right.end
    const double minShift = minStartShift (right);       // right.start earlier needs source
    ov = juce::jmin (ov, 2.0 * (centre - rS - minShift));
    ov = juce::jmax (ov, 0.0);

    const double dR = (centre - ov * 0.5) - rS;
    if (newTransaction) session.undo.beginNewTransaction ("resize crossfade");
    left.setProperty  (id::length, (centre + ov * 0.5) - lS, &session.undo);
    right.setProperty (id::start,  rS + dR, &session.undo);
    right.setProperty (id::length, rL - dR, &session.undo);
    shiftStartContent (session, right, dR);
    return ov;
}

} // namespace dg::clipops
