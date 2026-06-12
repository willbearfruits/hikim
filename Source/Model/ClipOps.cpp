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

} // namespace dg::clipops
