#include "TimelineView.h"

namespace dg
{

static juce::Colour trackColour (const ValueTree& t)
{
    return juce::Colour::fromString (t.getProperty (id::colour, "ff808080").toString());
}

// =========================================================================== Ruler
class TimelineView::Ruler : public juce::Component
{
public:
    explicit Ruler (TimelineView& o) : tv (o) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::panel);
        auto map = tv.engine.getTempoMap();
        const double t0 = tv.xToTime (tv.vp.getViewPositionX());
        const double t1 = tv.xToTime (tv.vp.getViewPositionX() + tv.vp.getWidth());
        const int xOff = -tv.vp.getViewPositionX();

        // loop region
        auto tr = tv.session.transport();
        const double ls = tr[id::loopStart], le = tr[id::loopEnd];
        if (le > ls)
        {
            g.setColour (((bool) tr[id::loopOn] ? col::accent2 : col::dim).withAlpha (0.35f));
            g.fillRect ((int) (tv.timeToX (ls)) + xOff, 0, (int) ((le - ls) * tv.pps), getHeight() / 2);
        }
        // (punch UI removed by owner request - engine support stays dormant)

        // bar ticks + labels
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        double beat = map->barBeatAt (map->secondsToBeats (juce::jmax (0.0, t0))).barStartBeat;
        int guard = 0;
        while (guard++ < 4000)
        {
            const double sec = map->beatsToSeconds (beat);
            if (sec > t1) break;
            const int x = (int) tv.timeToX (sec) + xOff;
            auto bb = map->barBeatAt (beat + 1.0e-6);
            g.setColour (col::dim);
            g.drawLine ((float) x, (float) getHeight() - 12, (float) x, (float) getHeight());
            g.drawText (String (bb.bar + 1), x + 3, getHeight() - 14, 44, 12, juce::Justification::left);
            beat = bb.barStartBeat + bb.num * 4.0 / bb.den;
        }

        // tempo / timesig events
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        for (const auto& ev : map->getTempos())
        {
            const int x = (int) tv.timeToX (map->beatsToSeconds (ev.beat)) + xOff;
            g.setColour (col::accent2);
            g.drawText (String (ev.bpm, 1), x + 2, 1, 50, 10, juce::Justification::left);
        }

        // markers
        for (const auto& m : tv.session.markers())
        {
            const int x = (int) tv.timeToX ((double) m[id::t]) + xOff;
            g.setColour (col::accent);
            g.drawLine ((float) x, 0, (float) x, (float) getHeight(), 1.4f);
            g.drawText (m[id::name].toString(), x + 3, 10, 80, 11, juce::Justification::left);
        }

        g.setColour (col::line);
        g.drawLine (0, (float) getHeight() - 0.5f, (float) getWidth(), (float) getHeight() - 0.5f);

        // playhead
        const int px = (int) tv.timeToX (tv.engine.getPositionSeconds()) + xOff;
        g.setColour (col::record);
        g.drawLine ((float) px, 0, (float) px, (float) getHeight(), 1.6f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const double sec = juce::jmax (0.0, tv.xToTime (e.x + tv.vp.getViewPositionX()));
        if (e.mods.isPopupMenu()) { showMenu (sec); return; }
        if (e.y < getHeight() / 2) { loopAnchor = tv.snap (sec); draggingLoop = true; return; }
        tv.engine.seekSeconds (tv.snap (sec));
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const double sec = juce::jmax (0.0, tv.xToTime (e.x + tv.vp.getViewPositionX()));
        if (draggingLoop)
        {
            auto tr = tv.session.transport();
            const double s = tv.snap (juce::jmin (loopAnchor, sec)), en = tv.snap (juce::jmax (loopAnchor, sec));
            tr.setProperty (id::loopStart, s, nullptr);
            tr.setProperty (id::loopEnd, en, nullptr);
            if (en > s) tr.setProperty (id::loopOn, true, nullptr);
        }
        else
            tv.engine.seekSeconds (tv.snap (sec));
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { draggingLoop = false; }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const double sec = tv.snap (tv.xToTime (e.x + tv.vp.getViewPositionX()));
        tv.session.undo.beginNewTransaction ("marker");
        tv.session.addMarker (sec, "M" + String (tv.session.markers().getNumChildren() + 1));
    }

    void showMenu (double sec)
    {
        juce::PopupMenu m;
        m.addItem (3, "Clear loop");
        m.addSeparator();
        m.addItem (4, "Insert tempo change here...");
        m.addItem (5, "Insert time signature here...");
        m.addSeparator();
        m.addItem (6, "Delete nearest marker");

        m.showMenuAsync ({}, [this, sec] (int r)
        {
            auto tr = tv.session.transport();
            auto map = tv.engine.getTempoMap();
            if (r == 3) { tr.setProperty (id::loopOn, false, nullptr); tr.setProperty (id::loopEnd, 0.0, nullptr); tr.setProperty (id::loopStart, 0.0, nullptr); }
            else if (r == 4)
            {
                auto* w = new juce::AlertWindow ("Tempo change", "BPM at " + map->formatBarsBeats (tv.engine.secToSamples (sec)), juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("bpm", String (map->bpmAtBeat (map->secondsToBeats (sec)), 1));
                w->addButton ("OK", 1); w->addButton ("Cancel", 0);
                w->enterModalState (true, juce::ModalCallbackFunction::create ([this, sec, w] (int res)
                {
                    if (res == 1)
                    {
                        auto map2 = tv.engine.getTempoMap();
                        ValueTree ev (id::TEMPO);
                        ev.setProperty (id::beat, std::round (map2->secondsToBeats (sec)), nullptr);
                        ev.setProperty (id::bpm, juce::jlimit (10.0, 999.0, w->getTextEditorContents ("bpm").getDoubleValue()), nullptr);
                        tv.session.tempoMap().appendChild (ev, &tv.session.undo);
                    }
                }), true);
            }
            else if (r == 5)
            {
                auto* w = new juce::AlertWindow ("Time signature", "e.g. 7/8", juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("sig", "4/4");
                w->addButton ("OK", 1); w->addButton ("Cancel", 0);
                w->enterModalState (true, juce::ModalCallbackFunction::create ([this, sec, w] (int res)
                {
                    if (res == 1)
                    {
                        auto txt = w->getTextEditorContents ("sig");
                        const int num = txt.upToFirstOccurrenceOf ("/", false, false).getIntValue();
                        const int den = txt.fromFirstOccurrenceOf ("/", false, false).getIntValue();
                        if (num > 0 && den > 0)
                        {
                            auto map2 = tv.engine.getTempoMap();
                            auto bb = map2->barBeatAt (map2->secondsToBeats (sec));
                            ValueTree ev (id::TIMESIG);
                            ev.setProperty (id::beat, bb.barStartBeat, nullptr);
                            ev.setProperty (id::num, num, nullptr);
                            ev.setProperty (id::den, den, nullptr);
                            tv.session.tempoMap().appendChild (ev, &tv.session.undo);
                        }
                    }
                }), true);
            }
            else if (r == 6)
            {
                auto ms = tv.session.markers();
                int best = -1; double bestDist = 1.0e9;
                for (int i = 0; i < ms.getNumChildren(); ++i)
                {
                    const double d = std::abs ((double) ms.getChild (i)[id::t] - sec);
                    if (d < bestDist) { bestDist = d; best = i; }
                }
                if (best >= 0) ms.removeChild (best, &tv.session.undo);
            }
            repaint();
        });
    }

private:
    TimelineView& tv;
    bool draggingLoop = false;
    double loopAnchor = 0;
};

// =========================================================================== ClipComp
class TimelineView::ClipComp : public juce::Component
{
public:
    ClipComp (TimelineView& o, ValueTree c, ValueTree t) : tv (o), clip (c), track (t)
    {
        if (clip[id::type].toString() == "audio")
        {
            thumb = std::make_unique<juce::AudioThumbnail> (256, tv.engine.formatManager, tv.engine.thumbCache);
            thumb->setSource (new juce::FileInputSource (tv.engine.mediaFileFor (File (clip[id::file].toString()))));
        }
    }

    ValueTree clip, track;

    void paint (juce::Graphics& g) override
    {
        const bool isMidi = clip[id::type].toString() == "midi";
        const bool selected = tv.ui.selectedClips.count (clip[id::uid].toString()) > 0;
        const bool isTake = (int) clip.getProperty (id::lane, 0) != 0;
        auto base = trackColour (track).interpolatedWith (isMidi ? col::clipMidi : col::clipAudio, 0.5f);
        if (isTake) base = base.withMultipliedSaturation (0.4f).darker (0.4f);

        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (base.withAlpha (0.35f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (selected ? juce::Colours::white : base);
        g.drawRoundedRectangle (r, 3.0f, selected ? 1.8f : 1.0f);

        const double lenSec = clip[id::length];
        if (thumb != nullptr && thumb->getTotalLength() > 0)
        {
            g.setColour (base.brighter (0.6f));
            const double fileSR = clip.getProperty (id::fileSR, 48000.0);
            const double stretch = clip.getProperty (id::stretch, 1.0);
            const double srcStart = (double) clip[id::offset] / fileSR;
            const double srcLen = lenSec / stretch * 1.0;   // varispeed keeps file-time = len/stretch
            thumb->drawChannels (g, getLocalBounds().reduced (2, 12).withTrimmedTop (2),
                                 srcStart, srcStart + srcLen, 0.9f);
        }
        else if (isMidi)
        {
            g.setColour (base.brighter (0.7f));
            auto notes = clip.getChildWithName (id::NOTES);
            auto map = tv.engine.getTempoMap();
            const double clipStartBeat = map->secondsToBeats ((double) clip[id::start]);
            for (const auto& n : notes)
            {
                const double bs = map->beatsToSeconds (clipStartBeat + (double) n[id::beat]) - (double) clip[id::start];
                const double be = map->beatsToSeconds (clipStartBeat + (double) n[id::beat] + (double) n[id::len]) - (double) clip[id::start];
                const float y = (float) getHeight() * (1.0f - ((int) n[id::pitch] - 24) / 84.0f);
                g.fillRect ((float) (bs * tv.pps), juce::jlimit (2.0f, getHeight() - 4.0f, y),
                            juce::jmax (2.0f, (float) ((be - bs) * tv.pps)), 2.0f);
            }
        }

        // fades
        const double fi = clip.getProperty (id::fadeIn, 0.0), fo = clip.getProperty (id::fadeOut, 0.0);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        if (fi > 0)
        {
            juce::Path p; p.startNewSubPath (0, 0);
            p.lineTo ((float) (fi * tv.pps), 0); p.lineTo (0, (float) getHeight()); p.closeSubPath();
            g.fillPath (p);
        }
        if (fo > 0)
        {
            juce::Path p; p.startNewSubPath ((float) getWidth(), 0);
            p.lineTo ((float) getWidth() - (float) (fo * tv.pps), 0);
            p.lineTo ((float) getWidth(), (float) getHeight()); p.closeSubPath();
            g.fillPath (p);
        }

        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText (clip[id::name].toString() + (isTake ? "  [take " + clip[id::lane].toString() + "]" : ""),
                    4, 1, getWidth() - 8, 11, juce::Justification::left);
    }

    enum class Mode { none, move, trimL, trimR, fadeL, fadeR };

    void mouseDown (const juce::MouseEvent& e) override
    {
        const String uid = clip[id::uid];

        // premiere-style tools
        if (! e.mods.isPopupMenu() && tv.ui.tool == Tool::razor)
        {
            const double t = tv.snap (tv.xToTime (getX() + e.x));
            clipops::splitAt (tv.session, *tv.engine.getTempoMap(), { uid }, t);
            tv.rebuildPending = true;
            return;
        }
        if (! e.mods.isPopupMenu() && tv.ui.tool == Tool::erase)
        {
            clipops::deleteClips (tv.session, { uid });
            tv.ui.selectedClips.erase (uid);
            tv.rebuildPending = true;
            return;
        }

        if (! e.mods.isShiftDown() && tv.ui.selectedClips.count (uid) == 0)
            tv.ui.selectedClips.clear();
        tv.ui.selectedClips.insert (uid);
        tv.repaint();

        if (e.mods.isPopupMenu()) { showMenu(); return; }

        origStart = clip[id::start];
        origLen = clip[id::length];
        origOffset = clip[id::offset];
        const int w = getWidth();
        if (e.y < 14 && e.x < juce::jmin (24, w / 3)) mode = Mode::fadeL;
        else if (e.y < 14 && e.x > w - juce::jmin (24, w / 3)) mode = Mode::fadeR;
        else if (e.x < 7) mode = Mode::trimL;
        else if (e.x > w - 7) mode = Mode::trimR;
        else mode = Mode::move;

        duplicated = false;
        tv.session.undo.beginNewTransaction ("clip edit");
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const double dx = tv.xToTime (e.getDistanceFromDragStartX());
        auto& undo = tv.session.undo;

        if (mode == Mode::move && e.mods.isAltDown() && ! duplicated)
        {
            duplicated = true;
            auto copy = clip.createCopy();
            copy.setProperty (id::uid, SessionModel::newUID(), nullptr);
            clip.getParent().appendChild (copy, &undo);
            clip = copy;     // drag the duplicate
        }

        if (mode == Mode::move)
        {
            clip.setProperty (id::start, juce::jmax (0.0, tv.snap (origStart + dx)), &undo);
            // follow the cursor vertically so cross-track moves read clearly
            const int canvasY = e.getEventRelativeTo (getParentComponent()).y;
            const int rowIdx = tv.rowIndexAtY (canvasY);
            visualRowY = -1;
            if (rowIdx >= 0)
            {
                const auto& row = tv.rows[(size_t) rowIdx];
                if (! row.lane.isValid() && row.track != track
                    && row.track[id::type].toString() == track[id::type].toString())
                    visualRowY = row.y + 1;
            }
        }
        else if (mode == Mode::trimL)
        {
            const double ns = juce::jlimit (0.0, origStart + origLen - 0.05, tv.snap (origStart + dx));
            const double d = ns - origStart;
            clip.setProperty (id::start, ns, &undo);
            clip.setProperty (id::length, origLen - d, &undo);
            if (clip[id::type].toString() == "audio")
            {
                const double fileSR = clip.getProperty (id::fileSR, 48000.0);
                const double stretch = clip.getProperty (id::stretch, 1.0);
                clip.setProperty (id::offset, juce::jmax (0.0, origOffset + d * fileSR / stretch), &undo);
            }
        }
        else if (mode == Mode::trimR)
            clip.setProperty (id::length, juce::jmax (0.05, tv.snap (origStart + origLen + dx) - origStart), &undo);
        else if (mode == Mode::fadeL)
            clip.setProperty (id::fadeIn, juce::jlimit (0.0, (double) clip[id::length], (double) e.x / tv.pps), &undo);
        else if (mode == Mode::fadeR)
            clip.setProperty (id::fadeOut, juce::jlimit (0.0, (double) clip[id::length], (double) (getWidth() - e.x) / tv.pps), &undo);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (mode == Mode::move)
        {
            // vertical move to a compatible track
            const int canvasY = e.getEventRelativeTo (getParentComponent()).y;
            const int rowIdx = tv.rowIndexAtY (canvasY);
            if (rowIdx >= 0)
            {
                auto& row = tv.rows[(size_t) rowIdx];
                if (! row.lane.isValid() && row.track != track
                    && row.track[id::type].toString() == track[id::type].toString())
                {
                    auto copy = clip.createCopy();
                    clip.getParent().removeChild (clip, &tv.session.undo);
                    SessionModel::clipsOf (row.track).appendChild (copy, &tv.session.undo);
                }
            }
        }
        mode = Mode::none;
        visualRowY = -1;
        tv.rebuildPending = true;       // deferred: rebuild() would delete us mid-event
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (clip[id::type].toString() == "midi" && tv.ui.openPianoRoll)
            tv.ui.openPianoRoll (clip);
        else if (clip[id::type].toString() == "audio" && tv.ui.openSampleEditor)
            tv.ui.openSampleEditor (clip);
    }

    void showMenu()
    {
        juce::PopupMenu m;
        m.addItem (9, "Cut");
        m.addItem (10, "Copy");
        m.addItem (1, "Delete");
        m.addItem (11, "Ripple delete");
        m.addItem (2, "Duplicate");
        m.addItem (3, "Split at playhead");
        m.addItem (4, "Rename...");
        if (clip[id::type].toString() == "audio")
        {
            m.addItem (5, "Clip gain...");
            m.addItem (6, "Stretch (duration x)...");
            m.addItem (8, "Pitch-locked stretch (RubberBand)", StretchCache::available(),
                       (int) clip.getProperty (id::stretchMode, 0) == 1);
        }
        if ((int) clip.getProperty (id::lane, 0) != 0)
            m.addItem (7, "Promote take to lane 0");

        m.addSeparator();
        // capture the view pointer + clip tree, never `this`: the menu callback
        // can outlive this ClipComp (any rebuild deletes it)
        auto* view = &tv;
        ValueTree c = clip;
        m.showMenuAsync ({}, [view, c] (int r) mutable
        {
            auto& undo = view->session.undo;
            undo.beginNewTransaction ("clip menu");
            if (r == 1) c.getParent().removeChild (c, &undo);
            else if (r == 9) view->copySelected (true);
            else if (r == 10) view->copySelected (false);
            else if (r == 11) view->rippleDeleteSelected();
            else if (r == 2) view->duplicateSelected();
            else if (r == 3) view->splitSelectedAtPlayhead();
            else if (r == 4)
            {
                auto* w = new juce::AlertWindow ("Rename clip", {}, juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("n", c[id::name].toString());
                w->addButton ("OK", 1); w->addButton ("Cancel", 0);
                w->enterModalState (true, juce::ModalCallbackFunction::create ([view, c, w] (int res) mutable
                { if (res == 1) c.setProperty (id::name, w->getTextEditorContents ("n"), &view->session.undo); }), true);
            }
            else if (r == 5)
            {
                auto* w = new juce::AlertWindow ("Clip gain (dB)", {}, juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("g", c.getProperty (id::clipGain, 0.0).toString());
                w->addButton ("OK", 1); w->addButton ("Cancel", 0);
                w->enterModalState (true, juce::ModalCallbackFunction::create ([view, c, w] (int res) mutable
                { if (res == 1) c.setProperty (id::clipGain, w->getTextEditorContents ("g").getDoubleValue(), &view->session.undo); }), true);
            }
            else if (r == 6)
            {
                auto* w = new juce::AlertWindow ("Stretch (duration multiplier; 2 = twice as long)", {}, juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("s", c.getProperty (id::stretch, 1.0).toString());
                w->addButton ("OK", 1); w->addButton ("Cancel", 0);
                w->enterModalState (true, juce::ModalCallbackFunction::create ([view, c, w] (int res) mutable
                { if (res == 1) c.setProperty (id::stretch, juce::jlimit (0.1, 10.0, w->getTextEditorContents ("s").getDoubleValue()), &view->session.undo); }), true);
            }
            else if (r == 8)
                c.setProperty (id::stretchMode,
                               (int) c.getProperty (id::stretchMode, 0) == 1 ? 0 : 1,
                               &view->session.undo);
            else if (r == 7)
            {
                const int myLane = c.getProperty (id::lane, 0);
                for (auto other : c.getParent())
                    if (other != c && (int) other.getProperty (id::lane, 0) == 0
                        && (double) other[id::start] < (double) c[id::start] + (double) c[id::length]
                        && (double) other[id::start] + (double) other[id::length] > (double) c[id::start])
                        other.setProperty (id::lane, myLane, &view->session.undo);
                c.setProperty (id::lane, 0, &view->session.undo);
            }
        });
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (tv.ui.tool == Tool::razor)  { setMouseCursor (juce::MouseCursor::IBeamCursor); return; }
        if (tv.ui.tool == Tool::erase)  { setMouseCursor (juce::MouseCursor::CrosshairCursor); return; }
        const int w = getWidth();
        if (e.x < 7 || e.x > w - 7)
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        else if (e.y < 14 && (e.x < 24 || e.x > w - 24))
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        else
            setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    int visualRowY = -1;          // live cross-track drag feedback

private:
    TimelineView& tv;
    Mode mode = Mode::none;
    double origStart = 0, origLen = 0, origOffset = 0;
    bool duplicated = false;
    std::unique_ptr<juce::AudioThumbnail> thumb;
};

// =========================================================================== AutoLaneComp
class TimelineView::AutoLaneComp : public juce::Component
{
public:
    AutoLaneComp (TimelineView& o, ValueTree l) : tv (o), lane (l) {}

    ValueTree lane;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg.brighter (0.03f));
        g.setColour (col::line);
        g.drawRect (getLocalBounds());

        std::vector<std::pair<double, float>> pts;
        for (const auto& p : lane)
            if (p.hasType (id::PT))
                pts.emplace_back ((double) p[id::t], (float) (double) p[id::v]);
        std::sort (pts.begin(), pts.end());

        g.setColour (col::accent2);
        juce::Path path;
        const int xOff = 0;
        if (pts.empty())
        {
            g.setColour (col::dim);
            g.drawText ("click to add automation points", getLocalBounds(), juce::Justification::centred);
            return;
        }
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const float x = (float) tv.timeToX (pts[i].first) + xOff;
            const float y = (1.0f - pts[i].second) * (float) getHeight();
            if (i == 0) { path.startNewSubPath (0, y); path.lineTo (x, y); }
            else path.lineTo (x, y);
            if (i == pts.size() - 1) path.lineTo ((float) getWidth(), y);
        }
        g.strokePath (path, juce::PathStrokeType (1.4f));
        for (const auto& [t, v] : pts)
            g.fillEllipse ((float) tv.timeToX (t) - 3.0f, (1.0f - v) * (float) getHeight() - 3.0f, 6.0f, 6.0f);
    }

    int hitPoint (const juce::MouseEvent& e)
    {
        for (int i = 0; i < lane.getNumChildren(); ++i)
        {
            auto p = lane.getChild (i);
            if (! p.hasType (id::PT)) continue;
            const float x = (float) tv.timeToX ((double) p[id::t]);
            const float y = (1.0f - (float) (double) p[id::v]) * (float) getHeight();
            if (std::abs (x - e.x) < 6 && std::abs (y - e.y) < 6) return i;
        }
        return -1;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        tv.session.undo.beginNewTransaction ("automation");
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem (1, "Clear all points");
            m.showMenuAsync ({}, [this] (int r)
            {
                if (r == 1)
                {
                    for (int i = lane.getNumChildren(); --i >= 0;)
                        if (lane.getChild (i).hasType (id::PT))
                            lane.removeChild (i, &tv.session.undo);
                    repaint();
                }
            });
            return;
        }
        dragIdx = hitPoint (e);
        if (dragIdx < 0)
        {
            ValueTree p (id::PT);
            p.setProperty (id::t, juce::jmax (0.0, tv.xToTime (e.x)), nullptr);
            p.setProperty (id::v, juce::jlimit (0.0, 1.0, 1.0 - (double) e.y / getHeight()), nullptr);
            lane.appendChild (p, &tv.session.undo);
            dragIdx = lane.indexOf (p);
        }
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragIdx < 0 || dragIdx >= lane.getNumChildren()) return;
        auto p = lane.getChild (dragIdx);
        p.setProperty (id::t, juce::jmax (0.0, tv.xToTime (e.x)), &tv.session.undo);
        p.setProperty (id::v, juce::jlimit (0.0, 1.0, 1.0 - (double) e.y / getHeight()), &tv.session.undo);
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int i = hitPoint (e);
        if (i >= 0) { lane.removeChild (i, &tv.session.undo); repaint(); }
    }

private:
    TimelineView& tv;
    int dragIdx = -1;
};

// =========================================================================== LaneHeader
class TimelineView::LaneHeader : public juce::Component
{
public:
    LaneHeader (TimelineView& o, ValueTree t, ValueTree l) : tv (o), track (t), lane (l)
    {
        name.setText (lane[id::name].toString(), juce::dontSendNotification);
        name.setFont (juce::Font (juce::FontOptions (10.0f)));
        name.setColour (juce::Label::textColourId, col::accent2);
        addAndMakeVisible (name);

        mode.addItemList ({ "OFF", "READ", "TOUCH", "WRITE" }, 1);
        mode.setSelectedItemIndex ((int) lane.getProperty (id::mode, 1), juce::dontSendNotification);
        mode.onChange = [this] { lane.setProperty (id::mode, mode.getSelectedItemIndex(), nullptr); };
        addAndMakeVisible (mode);

        close.setButtonText ("x");
        close.onClick = [this]
        {
            // removal triggers the tree listener, which rebuilds on the next tick
            lane.getParent().removeChild (lane, &tv.session.undo);
        };
        addAndMakeVisible (close);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::panel.darker (0.15f));
        g.setColour (col::line);
        g.drawRect (getLocalBounds());
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (e.y > getHeight() - 6 ? juce::MouseCursor::UpDownResizeCursor
                                              : juce::MouseCursor::NormalCursor);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.y > getHeight() - 6) { resizing = true; origH = getHeight(); }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (resizing)
            lane.setProperty (id::height,
                              juce::jlimit (24, 400, origH + e.getDistanceFromDragStartY()), nullptr);
    }
    void mouseUp (const juce::MouseEvent&) override { resizing = false; }

    void resized() override
    {
        auto b = getLocalBounds().reduced (4, 2);
        close.setBounds (b.removeFromRight (18).reduced (0, 8));
        mode.setBounds (b.removeFromBottom (20).reduced (0, 1));
        name.setBounds (b);
    }

private:
    TimelineView& tv;
    ValueTree track, lane;
    juce::Label name;
    juce::ComboBox mode;
    juce::TextButton close;
    bool resizing = false;
    int origH = 0;
};

// =========================================================================== TrackHeader
class TimelineView::TrackHeader : public juce::Component
{
public:
    TrackHeader (TimelineView& o, ValueTree t) : tv (o), track (t)
    {
        name.setText (track[id::name].toString(), juce::dontSendNotification);
        name.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        name.setEditable (false, true);
        name.onTextChange = [this] { track.setProperty (id::name, name.getText(), &tv.session.undo); };
        addAndMakeVisible (name);

        const String type = track[id::type];
        const bool isChannel = type == "audio" || type == "midi";

        auto setupToggle = [this] (juce::TextButton& b, const Identifier& prop, juce::Colour on)
        {
            b.setClickingTogglesState (true);
            b.setToggleState ((bool) track[prop], juce::dontSendNotification);
            b.setColour (juce::TextButton::buttonOnColourId, on);
            b.onClick = [this, &b, prop] { track.setProperty (prop, b.getToggleState(), nullptr); };
            addAndMakeVisible (b);
        };

        if (type != "video")
        {
            setupToggle (muteBtn, id::mute, col::accent2);
            if (isChannel) setupToggle (soloBtn, id::solo, col::play);
        }
        if (isChannel)
        {
            setupToggle (armBtn, id::armed, col::record);

            monBtn.onClick = [this]
            {
                const int m = ((int) track[id::monitor] + 1) % 3;
                track.setProperty (id::monitor, m, nullptr);
                updateMonText();
            };
            updateMonText();
            addAndMakeVisible (monBtn);

            fxBtn.onClick = [this] { tv.showTrackFxMenu (track, &fxBtn); };
            addAndMakeVisible (fxBtn);

            autoBtn.onClick = [this] { tv.showAutomationMenu (track, &autoBtn); };
            addAndMakeVisible (autoBtn);
        }
        if (type == "audio")
        {
            const int numIns = juce::jmax (1, tv.engine.graph.getTotalNumInputChannels());
            for (int i = 0; i < numIns; ++i) inBox.addItem ("In " + String (i + 1), i + 1);
            for (int i = 0; i + 1 < numIns; i += 2) inBox.addItem ("In " + String (i + 1) + "/" + String (i + 2) + " st", 100 + i);
            const int ic = track[id::inputChan];
            inBox.setSelectedId ((bool) track[id::inputStereo] ? 100 + ic : ic + 1, juce::dontSendNotification);
            inBox.onChange = [this]
            {
                const int sel = inBox.getSelectedId();
                track.setProperty (id::inputStereo, sel >= 100, nullptr);
                track.setProperty (id::inputChan, sel >= 100 ? sel - 100 : sel - 1, nullptr);
            };
            addAndMakeVisible (inBox);
        }
    }

    void updateMonText()
    {
        const int m = track[id::monitor];
        monBtn.setButtonText (m == 0 ? "MON-" : m == 1 ? "MON:DIR" : "MON:FX");
        monBtn.setColour (juce::TextButton::buttonColourId, m == 0 ? col::panelHi : col::accent.darker (0.4f));
    }

    void paint (juce::Graphics& g) override
    {
        const bool selected = tv.ui.selectedTrack == track[id::uid].toString();
        g.fillAll (selected ? col::panelHi : col::panel);
        g.setColour (trackColour (track));
        g.fillRect (0, 0, 4, getHeight());
        g.setColour (selected ? col::accent.withAlpha (0.7f) : col::line);
        g.drawRect (getLocalBounds());
        const String type = track[id::type];
        if (type != "audio" && type != "midi")
        {
            g.setColour (col::dim);
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            g.drawText (type.toUpperCase(), getLocalBounds().reduced (8, 2), juce::Justification::bottomRight);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (e.y > getHeight() - 6 ? juce::MouseCursor::UpDownResizeCursor
                                              : juce::MouseCursor::NormalCursor);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (resizing)
            track.setProperty (id::height,
                               juce::jlimit (28, 400, origH + e.getDistanceFromDragStartY()), nullptr);
    }

    void mouseUp (const juce::MouseEvent&) override { resizing = false; }

    void mouseDown (const juce::MouseEvent& e) override
    {
        tv.ui.selectedTrack = track[id::uid].toString();
        tv.repaint();
        getParentComponent()->repaint();

        if (! e.mods.isPopupMenu())
        {
            if (e.y > getHeight() - 6) { resizing = true; origH = getHeight(); }
            return;
        }
        juce::PopupMenu m;
        m.addItem (1, "Rename");
        m.addItem (2, "Change colour");
        m.addItem (3, "Move up");
        m.addItem (4, "Move down");
        m.addSeparator();
        m.addItem (5, "Delete track", track[id::type].toString() != "master");
        m.showMenuAsync ({}, [this] (int r)
        {
            auto tracks = tv.session.tracks();
            const int idx = tracks.indexOf (track);
            if (r == 1) name.showEditor();
            else if (r == 2)
            {
                auto c = trackColour (track);
                track.setProperty (id::colour, c.withRotatedHue (0.13f).toString(), &tv.session.undo);
                repaint();
            }
            else if (r == 3 && idx > 0) tracks.moveChild (idx, idx - 1, &tv.session.undo);
            else if (r == 4 && idx < tracks.getNumChildren() - 1) tracks.moveChild (idx, idx + 1, &tv.session.undo);
            else if (r == 5) { tv.session.removeTrack (track); }
        });
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (8, 3);
        name.setBounds (b.removeFromTop (18));
        auto row = b.removeFromTop (20);
        const String type = track[id::type];
        if (type == "video") return;
        muteBtn.setBounds (row.removeFromLeft (22));
        row.removeFromLeft (2);
        if (type == "audio" || type == "midi")
        {
            soloBtn.setBounds (row.removeFromLeft (22));
            row.removeFromLeft (2);
            armBtn.setBounds (row.removeFromLeft (22));
            row.removeFromLeft (2);
            monBtn.setBounds (row.removeFromLeft (58));
            row.removeFromLeft (2);
            fxBtn.setBounds (row.removeFromLeft (28));
            row.removeFromLeft (2);
            autoBtn.setBounds (row.removeFromLeft (22));
            auto row2 = b.removeFromTop (18);
            if (type == "audio") inBox.setBounds (row2.removeFromLeft (96));
        }
    }

private:
    TimelineView& tv;
    ValueTree track;
    juce::Label name;
    juce::TextButton muteBtn { "M" }, soloBtn { "S" }, armBtn { "R" }, monBtn, fxBtn { "FX" }, autoBtn { "A" };
    juce::ComboBox inBox;
    bool resizing = false;
    int origH = 0;
};

// =========================================================================== Canvas
class TimelineView::Canvas : public juce::Component,
                             public juce::FileDragAndDropTarget,
                             public juce::DragAndDropTarget
{
public:
    explicit Canvas (TimelineView& o) : tv (o) {}

    // internal drags from the FILES bin and the FX explorer
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        const String desc = d.description.toString();
        return desc == "binfiles" || desc.startsWith ("fx:");
    }

    void itemDropped (const SourceDetails& d) override
    {
        const String desc = d.description.toString();
        if (desc == "binfiles")
        {
            if (auto* ftc = dynamic_cast<juce::FileTreeComponent*> (d.sourceComponent.get()))
            {
                juce::StringArray files;
                for (int i = 0; i < ftc->getNumSelectedFiles(); ++i)
                    files.add (ftc->getSelectedFile (i).getFullPathName());
                tv.importFiles (files, tv.getLocalPoint (this, d.localPosition));
            }
            return;
        }
        if (desc.startsWith ("fx:"))
        {
            const int rowIdx = tv.rowIndexAtY (d.localPosition.y);
            if (rowIdx >= 0 && ! tv.rows[(size_t) rowIdx].lane.isValid())
                tv.applyFxToTrack (tv.rows[(size_t) rowIdx].track, desc);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);
        auto map = tv.engine.getTempoMap();
        auto clip = g.getClipBounds();
        const double t0 = tv.xToTime (clip.getX());
        const double t1 = tv.xToTime (clip.getRight());

        // beat/bar grid
        double beat = map->barBeatAt (map->secondsToBeats (juce::jmax (0.0, t0))).barStartBeat;
        int guard = 0;
        while (guard++ < 8000)
        {
            const double sec = map->beatsToSeconds (beat);
            if (sec > t1) break;
            auto bb = map->barBeatAt (beat + 1.0e-6);
            const bool isBar = bb.beatInBar < 1.0e-6;
            const int x = (int) tv.timeToX (sec);
            g.setColour (isBar ? col::line : col::line.withAlpha (0.35f));
            if (isBar || tv.pps * 60.0 / map->bpmAtBeat (beat) > 14)
                g.drawLine ((float) x, (float) clip.getY(), (float) x, (float) clip.getBottom());
            beat += 4.0 / bb.den;
        }

        // row separators + bus/master shading
        for (const auto& row : tv.rows)
        {
            g.setColour (col::line.withAlpha (0.6f));
            g.drawLine (0.0f, (float) (row.y + row.h), (float) getWidth(), (float) (row.y + row.h));
            const String type = row.track[id::type];
            if (! row.lane.isValid() && (type == "bus" || type == "master"))
            {
                g.setColour (col::panel.withAlpha (0.5f));
                g.fillRect (0, row.y, getWidth(), row.h);
            }
        }

        // loop / punch shading
        auto tr = tv.session.transport();
        if ((bool) tr[id::loopOn])
        {
            const double ls = tr[id::loopStart], le = tr[id::loopEnd];
            g.setColour (col::accent2.withAlpha (0.05f));
            g.fillRect ((int) tv.timeToX (ls), clip.getY(), (int) ((le - ls) * tv.pps), clip.getHeight());
        }
        // live recording: growing region + waveform on armed tracks
        if (tv.engine.isRecording())
        {
            const double ph = tv.engine.getPositionSeconds();
            for (const auto& row : tv.rows)
            {
                if (row.lane.isValid() || ! (bool) row.track[id::armed]) continue;
                const String type = row.track[id::type];

                if (type == "midi")
                {
                    const double start = tv.engine.getRecordStartSeconds();
                    if (ph <= start) continue;
                    g.setColour (col::record.withAlpha (0.12f));
                    g.fillRect ((int) tv.timeToX (start), row.y + 1, (int) ((ph - start) * tv.pps), row.h - 2);
                }
                else if (auto* rs = tv.engine.getLiveRecording (row.track[id::uid].toString()))
                {
                    const int count = rs->peakCount.load();
                    const double dur = (double) count * rs->samplesPerPeak / rs->sampleRate;
                    if (count < 1) continue;
                    // anchor at the playhead so punch gaps / loop passes stay sane
                    juce::Rectangle<int> r ((int) tv.timeToX (juce::jmax (0.0, ph - dur)), row.y + 1,
                                            juce::jmax (2, (int) (dur * tv.pps)), row.h - 2);
                    g.setColour (col::record.withAlpha (0.15f));
                    g.fillRect (r);
                    g.setColour (col::record.withAlpha (0.7f));
                    g.drawRect (r);

                    g.setColour (col::record.brighter (0.35f));
                    const float midY = (float) row.y + (float) row.h * 0.5f;
                    const int x0 = juce::jmax (r.getX(), clip.getX());
                    const int x1 = juce::jmin (r.getRight(), clip.getRight());
                    for (int x = x0; x < x1; ++x)
                    {
                        const int p0 = (int) ((double) (x - r.getX()) / r.getWidth() * count);
                        const int p1 = juce::jmax (p0 + 1, (int) ((double) (x + 1 - r.getX()) / r.getWidth() * count));
                        float pk = 0.0f;
                        for (int p = p0; p < p1 && p < count; ++p)
                            pk = juce::jmax (pk, rs->peaks[p]);
                        const float h2 = juce::jmax (1.0f, pk * (float) row.h * 0.45f);
                        g.drawVerticalLine (x, midY - h2, midY + h2);
                    }
                }
            }
        }

        // playhead
        const int px = (int) tv.timeToX (tv.engine.getPositionSeconds());
        g.setColour (col::record.withAlpha (0.9f));
        g.drawLine ((float) px, (float) clip.getY(), (float) px, (float) clip.getBottom(), 1.4f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            const int rowIdx = tv.rowIndexAtY (e.y);
            if (rowIdx >= 0 && ! tv.rows[(size_t) rowIdx].lane.isValid())
            {
                auto track = tv.rows[(size_t) rowIdx].track;
                const String type = track[id::type];
                if (type == "midi")
                {
                    juce::PopupMenu m;
                    m.addItem (1, "New MIDI clip here");
                    const double sec = tv.snap (tv.xToTime (e.x));
                    m.showMenuAsync ({}, [this, track, sec] (int r) mutable
                    {
                        if (r == 1)
                        {
                            tv.session.undo.beginNewTransaction ("new clip");
                            auto map = tv.engine.getTempoMap();
                            const double beats = map->secondsToBeats (sec);
                            const double len = map->beatsToSeconds (beats + 4.0) - sec;
                            auto c = tv.session.addMidiClip (track, sec, len);
                            if (tv.ui.openPianoRoll) tv.ui.openPianoRoll (c);
                            tv.rebuild();
                        }
                    });
                }
            }
            return;
        }
        tv.ui.selectedClips.clear();
        tv.repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        {
            tv.zoomAround (w.deltaY > 0 ? 1.15 : 1.0 / 1.15, e.x);
            return;
        }
        Component::mouseWheelMove (e, w);
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (const auto& f : files)
            if (File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;m4a;caf;wma;opus;aac;wv;mp2;amr;mka"))
                return true;
        return false;
    }

    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        tv.importFiles (files, tv.getLocalPoint (this, juce::Point<int> (x, y)));
    }

private:
    TimelineView& tv;
};

// =========================================================================== ToolBar

namespace
{
    class ToolButton : public juce::Button
    {
    public:
        ToolButton (const juce::Path& p, const String& name) : juce::Button (name), icon (p)
        {
            setClickingTogglesState (false);
        }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            if (getToggleState()) { g.setColour (col::accent.withAlpha (0.3f)); g.fillRoundedRectangle (r, 2.0f); }
            else if (over || down) { g.setColour (col::panelHi); g.fillRoundedRectangle (r, 2.0f); }
            g.setColour (getToggleState() ? col::accent : col::text);
            g.fillPath (icon, icon.getTransformToScaleToFit (r.reduced (4.0f), true));
        }
    private:
        juce::Path icon;
    };

    juce::Path makeArrowIcon()
    {
        juce::Path p;
        p.startNewSubPath (0, 0); p.lineTo (0, 14); p.lineTo (4, 10);
        p.lineTo (7, 16); p.lineTo (9.5f, 14.5f); p.lineTo (6.5f, 9);
        p.lineTo (11, 8.5f); p.closeSubPath();
        return p;
    }
    juce::Path makeRazorIcon()
    {
        juce::Path p;                                   // blade + spine
        p.startNewSubPath (1, 11); p.lineTo (13, 11); p.lineTo (13, 8);
        p.lineTo (1, 4); p.closeSubPath();
        p.addRectangle (5.5f, 0.0f, 1.6f, 4.0f);        // handle stub
        return p;
    }
    juce::Path makeEraseIcon()
    {
        juce::Path p;
        p.addLineSegment ({ 1, 1, 11, 11 }, 2.4f);
        p.addLineSegment ({ 11, 1, 1, 11 }, 2.4f);
        return p;
    }
}

class TimelineToolBar : public juce::Component
{
public:
    TimelineToolBar (UIState& u, std::function<void()> onChangeFn)
        : ui (u), onChange (std::move (onChangeFn))
    {
        addBtn (makeArrowIcon(), "select (1)", Tool::select);
        addBtn (makeRazorIcon(), "razor (2)", Tool::razor);
        addBtn (makeEraseIcon(), "erase (3)", Tool::erase);
        sync();
    }

    void sync()
    {
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (ui.tool == tools[(size_t) i], juce::dontSendNotification);
        repaint();
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (3, 4);
        for (auto* btn : buttons)
        {
            btn->setBounds (b.removeFromLeft (24));
            b.removeFromLeft (2);
        }
    }

private:
    void addBtn (const juce::Path& icon, const String& name, Tool t)
    {
        auto* b = buttons.add (new ToolButton (icon, name));
        b->setTooltip (name);
        b->onClick = [this, t] { ui.tool = t; sync(); if (onChange) onChange(); };
        addAndMakeVisible (b);
        tools.push_back (t);
    }

    UIState& ui;
    std::function<void()> onChange;
    juce::OwnedArray<ToolButton> buttons;
    std::vector<Tool> tools;
};

// =========================================================================== TimelineView

TimelineView::TimelineView (AudioEngine& e, SessionModel& s, PluginHost& p, UIState& u)
    : engine (e), session (s), plugins (p), ui (u)
{
    ruler = std::make_unique<Ruler> (*this);
    addAndMakeVisible (*ruler);

    canvas = std::make_unique<Canvas> (*this);
    vp.setViewedComponent (canvas.get(), false);
    vp.setScrollBarsShown (true, true);
    addAndMakeVisible (vp);

    headerHolder.setInterceptsMouseClicks (false, true);
    headerHolder.addAndMakeVisible (headerStrip);
    addAndMakeVisible (headerHolder);

    toolbar = std::make_unique<TimelineToolBar> (ui, [this]
    {
        for (auto* cc : clipComps) cc->repaint();
    });
    addAndMakeVisible (*toolbar);

    session.root.addListener (this);
    startTimerHz (30);
    rebuild();
}

TimelineView::~TimelineView()
{
    session.root.removeListener (this);
}

void TimelineView::syncToolbar()
{
    if (auto* tb = dynamic_cast<TimelineToolBar*> (toolbar.get()))
        tb->sync();
    for (auto* cc : clipComps) cc->repaint();
}

double TimelineView::snap (double sec) const
{
    auto vt = session.video();
    return snapSeconds (sec, ui.snapMode, *engine.getTempoMap(), (double) vt.getProperty (id::fps, 25.0));
}

double TimelineView::contentLengthSec() const
{
    double end = 60.0;
    for (const auto& t : session.tracks())
        for (const auto& c : t.getChildWithName (id::CLIPS))
            end = juce::jmax (end, (double) c[id::start] + (double) c[id::length]);
    auto tr = session.transport();
    end = juce::jmax (end, (double) tr[id::loopEnd]);
    return end + 60.0;
}

void TimelineView::rebuild()
{
    headers.clear();
    clipComps.clear();
    laneComps.clear();
    rows.clear();

    for (auto track : session.tracks())
    {
        Row row;
        row.track = track;
        rows.push_back (row);

        auto* th = new TrackHeader (*this, track);
        headers.add (th);
        headerStrip.addAndMakeVisible (th);

        for (auto clip : SessionModel::clipsOf (track))
        {
            auto* cc = new ClipComp (*this, clip, track);
            clipComps.add (cc);
            canvas->addAndMakeVisible (cc);
        }

        for (auto lane : SessionModel::autoOf (track))
        {
            if (! (bool) lane.getProperty (id::visible, true)) continue;
            Row lr;
            lr.track = track;
            lr.lane = lane;
            rows.push_back (lr);

            auto* lh = new LaneHeader (*this, track, lane);
            headers.add (lh);
            headerStrip.addAndMakeVisible (lh);

            auto* lc = new AutoLaneComp (*this, lane);
            laneComps.add (lc);
            canvas->addAndMakeVisible (lc);
        }
    }
    layoutRows();
}

void TimelineView::layoutRows()
{
    int y = 0;
    for (auto& row : rows)
    {
        if (row.lane.isValid())
            row.h = juce::jlimit (24, 400, (int) row.lane.getProperty (id::height, 56));
        else
        {
            const String type = row.track[id::type];
            const int def = (type == "bus" || type == "master") ? 44 : (type == "video" ? 36 : 68);
            row.h = juce::jlimit (28, 400, (int) row.track.getProperty (id::height, def));
        }
        row.y = y;
        y += row.h;
    }
    const int w = juce::jmax (vp.getWidth(), (int) timeToX (contentLengthSec()));
    canvas->setSize (w, juce::jmax (y + 100, vp.getHeight()));
    headerStrip.setSize (kHeaderW, canvas->getHeight());

    for (size_t i = 0; i < rows.size(); ++i)
        if (i < (size_t) headers.size())
            headers[(int) i]->setBounds (0, rows[i].y, kHeaderW, rows[i].h);

    layoutCanvasChildren();
}

void TimelineView::layoutCanvasChildren()
{
    for (auto* cc : clipComps)
    {
        const Row* row = nullptr;
        for (const auto& r : rows)
            if (! r.lane.isValid() && r.track == cc->track) { row = &r; break; }
        if (row == nullptr) continue;

        int maxLane = 0;
        for (const auto& c : SessionModel::clipsOf (cc->track))
            maxLane = juce::jmax (maxLane, (int) c.getProperty (id::lane, 0));
        const int laneCount = maxLane + 1;
        const int laneH = juce::jmax (12, (row->h - 2) / laneCount);
        const int lane = cc->clip.getProperty (id::lane, 0);

        const int baseY = cc->visualRowY >= 0 ? cc->visualRowY : row->y + 1;
        cc->setBounds ((int) timeToX ((double) cc->clip[id::start]),
                       baseY + lane * laneH,
                       juce::jmax (8, (int) ((double) cc->clip[id::length] * pps)),
                       laneH);
    }
    for (auto* lc : laneComps)
    {
        const Row* row = nullptr;
        for (const auto& r : rows)
            if (r.lane == lc->lane) { row = &r; break; }
        if (row != nullptr)
            lc->setBounds (0, row->y, canvas->getWidth(), row->h);
    }
    canvas->repaint();
}

int TimelineView::rowIndexAtY (int y) const
{
    for (size_t i = 0; i < rows.size(); ++i)
        if (y >= rows[i].y && y < rows[i].y + rows[i].h)
            return (int) i;
    return -1;
}

void TimelineView::zoomAround (double factor, int pivotX)
{
    const double anchor = xToTime (pivotX);
    const int pivotInView = pivotX - vp.getViewPositionX();
    pps = juce::jlimit (4.0, 4000.0, pps * factor);
    layoutRows();
    vp.setViewPosition (juce::jmax (0, (int) timeToX (anchor) - pivotInView), vp.getViewPositionY());
    ruler->repaint();
}

void TimelineView::splitSelectedAtPlayhead()
{
    clipops::splitAt (session, *engine.getTempoMap(), ui.selectedClips, engine.getPositionSeconds());
    rebuild();
}

void TimelineView::deleteSelected()
{
    clipops::deleteClips (session, ui.selectedClips);
    ui.selectedClips.clear();
    rebuild();
}

void TimelineView::copySelected (bool cut)
{
    auto items = clipops::copyClips (session, ui.selectedClips);
    if (! items.empty())
    {
        clipboard = std::move (items);
        if (cut) deleteSelected();
    }
}

void TimelineView::pasteAtPlayhead()
{
    auto pasted = clipops::paste (session, clipboard, snap (engine.getPositionSeconds()));
    if (! pasted.isEmpty())
    {
        ui.selectedClips.clear();
        for (const auto& u : pasted) ui.selectedClips.insert (u);
        rebuild();
    }
}

void TimelineView::rippleDeleteSelected()
{
    clipops::rippleDelete (session, ui.selectedClips);
    ui.selectedClips.clear();
    rebuild();
}

void TimelineView::selectAll()
{
    ui.selectedClips.clear();
    for (auto track : session.tracks())
        for (auto clip : SessionModel::clipsOf (track))
            ui.selectedClips.insert (clip[id::uid].toString());
    repaint();
    for (auto* cc : clipComps) cc->repaint();
}

void TimelineView::duplicateSelected()
{
    clipops::duplicate (session, ui.selectedClips);
    rebuild();
}

void TimelineView::importFiles (const juce::StringArray& files, juce::Point<int> posInView)
{
    auto canvasPt = canvas->getLocalPoint (this, posInView);
    const bool overCanvas = vp.getBounds().contains (posInView);

    ValueTree track;
    double at = juce::jmax (0.0, engine.getPositionSeconds());

    // header drops share the canvas row layout (same y, just offset by the ruler/scroll)
    const int rowY = overCanvas ? canvasPt.y : posInView.y - kRulerH + vp.getViewPositionY();
    const int rowIdx = rowIndexAtY (rowY);
    if (rowIdx >= 0 && ! rows[(size_t) rowIdx].lane.isValid()
        && rows[(size_t) rowIdx].track[id::type].toString() == "audio")
        track = rows[(size_t) rowIdx].track;
    if (overCanvas)
        at = snap (juce::jmax (0.0, xToTime (canvasPt.x)));

    session.undo.beginNewTransaction ("import");

    juce::StringArray failed;
    bool added = false;
    for (const auto& fpath : files)
    {
        auto reader = engine.createAnyReader (File (fpath));
        if (reader == nullptr)
        {
            failed.add (File (fpath).getFileName());
            continue;
        }
        if (! track.isValid())                      // never leave an empty track behind
            track = session.addTrack ("audio", "Audio");
        const double len = (double) reader->lengthInSamples / reader->sampleRate;
        session.addAudioClip (track, File (fpath), at, len, reader->sampleRate);
        at += len;
        added = true;
    }

    if (! failed.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import",
            "No decoder for:\n" + failed.joinIntoString ("\n")
            + "\n\n(built-in formats + anything ffmpeg can read; is ffmpeg installed?)");
    if (added)
        rebuild();
}

void TimelineView::applyFxToTrack (ValueTree track, const String& fxId)
{
    if (! track.isValid()) return;
    const String type = track[id::type];
    if (type != "audio" && type != "midi" && type != "bus" && type != "master") return;

    session.undo.beginNewTransaction ("add fx");

    auto setInstrument = [this, &track] (const String& ident, const String& name)
    {
        if (track[id::type].toString() != "midi") return;
        auto inserts = SessionModel::insertsOf (track);
        for (int i = inserts.getNumChildren(); --i >= 0;)
            if (inserts.getChild (i)[id::type].toString() == "instrument")
                inserts.removeChild (i, &session.undo);
        if (ident.isNotEmpty())
        {
            auto ins = session.addInsert (track, "instrument");
            ins.setProperty (id::ident, ident, &session.undo);
            ins.setProperty (id::name, name, &session.undo);
        }
    };

    if (fxId == "fx:rack")
    {
        auto ins = session.addInsert (track, "rack");
        ins.setProperty (id::name, names::rackName, &session.undo);
    }
    else if (fxId.startsWith ("fx:builtin:"))
    {
        const String which = fxId.fromLastOccurrenceOf (":", false, false);
        setInstrument (which == "glitchtone" ? String() : "builtin:" + which, which.toUpperCase());
    }
    else if (fxId.startsWith ("fx:plug:"))
    {
        const String ident = fxId.fromFirstOccurrenceOf ("fx:plug:", false, false);
        if (auto desc = plugins.findByIdentifier (ident))
        {
            if (desc->isInstrument && type == "midi")
                setInstrument (ident, desc->name);
            else
            {
                auto ins = session.addInsert (track, "plugin");
                ins.setProperty (id::ident, ident, &session.undo);
                ins.setProperty (id::name, desc->name, &session.undo);
            }
        }
    }
}

// ---- track FX / automation menus -----------------------------------------

void TimelineView::showTrackFxMenu (ValueTree track, juce::Component* target)
{
    juce::PopupMenu m;
    const String trackUid = track[id::uid];
    auto inserts = SessionModel::insertsOf (track);

    int idx = 0;
    for (auto ins : inserts)
    {
        juce::PopupMenu sub;
        const String iuid = ins[id::uid];
        const bool isInstrument = ins[id::type].toString() == "instrument";
        sub.addItem (1000 + idx, "Open editor");
        sub.addItem (2000 + idx, "Bypass", true, (bool) ins[id::bypass]);
        sub.addItem (3000 + idx, "Remove");
        sub.addItem (4000 + idx, "Move up", idx > 0);
        sub.addItem (5000 + idx, "Move down", idx < inserts.getNumChildren() - 1);
        String label = ins[id::name].toString();
        if (label.isEmpty()) label = isInstrument ? "Instrument" : names::rackName;
        m.addSubMenu ((isInstrument ? "[INST] " : String (idx + 1) + ". ") + label, sub);
        ++idx;
    }
    if (idx > 0) m.addSeparator();
    m.addItem (1, String ("Add ") + names::rackName);

    juce::PopupMenu pluginMenu;
    auto types = plugins.knownList.getTypes();
    juce::KnownPluginList::addToMenu (pluginMenu, types, juce::KnownPluginList::sortByCategory);
    m.addSubMenu ("Add plugin", pluginMenu, types.size() > 0);

    if (track[id::type].toString() == "midi")
    {
        juce::PopupMenu instMenu;
        instMenu.addItem (2, "GlitchTone (default saw)");
        instMenu.addItem (3, "RUST - FM bell/metal");
        instMenu.addItem (4, "GRAVEL - noise percussion");
        instMenu.addItem (5, "HYMN - detuned pad");
        instMenu.addItem (6, "RUBBLE - drum kit");
        instMenu.addSeparator();
        int instId = 9000;
        juce::Array<juce::PluginDescription> instruments;
        for (const auto& d : types)
            if (d.isInstrument) instruments.add (d);
        for (const auto& d : instruments)
            instMenu.addItem (instId++, d.name);
        m.addSubMenu ("Set instrument", instMenu);
        // EXTEND: MIDI FX / arpeggiator slot in front of the instrument
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                     [this, track, types, inserts] (int r) mutable
    {
        if (r == 0) return;
        session.undo.beginNewTransaction ("inserts");

        if (r == 1)
        {
            auto ins = session.addInsert (track, "rack");
            ins.setProperty (id::name, names::rackName, &session.undo);
            return;
        }
        if (r >= 2 && r <= 6)   // built-in instruments
        {
            static const char* kBuiltins[] = { "glitchtone", "rust", "gravel", "hymn", "rubble" };
            applyFxToTrack (track, "fx:builtin:" + String (kBuiltins[r - 2]));
            return;
        }
        if (r >= 9000 && r < 9000 + 4096)
        {
            juce::Array<juce::PluginDescription> instruments;
            for (const auto& d : types)
                if (d.isInstrument) instruments.add (d);
            const int ii = r - 9000;
            if (ii < instruments.size())
            {
                auto insertsT = SessionModel::insertsOf (track);
                for (int i = insertsT.getNumChildren(); --i >= 0;)
                    if (insertsT.getChild (i)[id::type].toString() == "instrument")
                        insertsT.removeChild (i, &session.undo);
                auto ins = session.addInsert (track, "instrument");
                ins.setProperty (id::ident, instruments[ii].createIdentifierString(), &session.undo);
                ins.setProperty (id::name, instruments[ii].name, &session.undo);
            }
            return;
        }
        if (r >= 1000 && r < 6000)
        {
            const int slot = r % 1000;
            auto ins = inserts.getChild (slot);
            if (! ins.isValid()) return;
            const int action = r / 1000;
            if (action == 1 && ui.openInsertEditor) ui.openInsertEditor (track[id::uid].toString(), ins[id::uid].toString());
            else if (action == 2) ins.setProperty (id::bypass, ! (bool) ins[id::bypass], &session.undo);
            else if (action == 3) inserts.removeChild (ins, &session.undo);
            else if (action == 4) inserts.moveChild (slot, slot - 1, &session.undo);
            else if (action == 5) inserts.moveChild (slot, slot + 1, &session.undo);
            return;
        }
        // plugin chosen from the known-list menu
        const int chosen = juce::KnownPluginList::getIndexChosenByMenu (types, r);
        if (chosen >= 0)
        {
            auto ins = session.addInsert (track, "plugin");
            ins.setProperty (id::ident, types.getReference (chosen).createIdentifierString(), &session.undo);
            ins.setProperty (id::name, types.getReference (chosen).name, &session.undo);
        }
    });
}

juce::PopupMenu TimelineView::buildParamTargetMenu (ValueTree track, int& nextId, juce::StringArray& targets)
{
    juce::PopupMenu m;
    auto add = [&] (const String& label, const String& target)
    {
        m.addItem (nextId++, label);
        targets.add (target + "\n" + label);
    };
    add ("Volume", "strip:gain");
    add ("Pan", "strip:pan");
    add ("Mute", "strip:mute");
    add ("Send A", "send:A");
    add ("Send B", "send:B");

    for (auto ins : SessionModel::insertsOf (track))
    {
        if (auto* proc = engine.getInsertProcessor (ins[id::uid].toString()))
        {
            juce::PopupMenu sub;
            const auto& params = proc->getParameters();
            const int count = juce::jmin (128, params.size());   // EXTEND: searchable param browser
            for (int i = 0; i < count; ++i)
            {
                sub.addItem (nextId, params[i]->getName (40));
                targets.add ("ins:" + ins[id::uid].toString() + ":" + String (i)
                             + "\n" + ins[id::name].toString() + ": " + params[i]->getName (24));
                ++nextId;
            }
            m.addSubMenu (ins[id::name].toString(), sub);
        }
    }
    return m;
}

void TimelineView::showAutomationMenu (ValueTree track, juce::Component* target)
{
    int nextId = 100;
    juce::StringArray targets;
    auto addMenu = buildParamTargetMenu (track, nextId, targets);

    juce::PopupMenu m;
    int laneId = 1;
    for (auto lane : SessionModel::autoOf (track))
    {
        m.addItem (laneId++, "Lane: " + lane[id::name].toString(), true, (bool) lane.getProperty (id::visible, true));
    }
    if (laneId > 1) m.addSeparator();
    m.addSubMenu ("Add automation lane", addMenu);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                     [this, track, targets] (int r) mutable
    {
        if (r == 0) return;
        if (r < 100)
        {
            auto lane = SessionModel::autoOf (track).getChild (r - 1);
            if (lane.isValid())
                lane.setProperty (id::visible, ! (bool) lane.getProperty (id::visible, true), nullptr);
            rebuild();
            return;
        }
        const int ti = r - 100;
        if (ti < targets.size())
        {
            const String entry = targets[ti];
            session.undo.beginNewTransaction ("add lane");
            ValueTree lane (id::LANE);
            lane.setProperty (id::param, entry.upToFirstOccurrenceOf ("\n", false, false), nullptr);
            lane.setProperty (id::name, entry.fromFirstOccurrenceOf ("\n", false, false), nullptr);
            lane.setProperty (id::mode, 1, nullptr);
            lane.setProperty (id::visible, true, nullptr);
            SessionModel::autoOf (track).appendChild (lane, &session.undo);
            rebuild();
        }
    });
}

// ---- plumbing -------------------------------------------------------------

void TimelineView::resized()
{
    auto b = getLocalBounds();
    auto top = b.removeFromTop (kRulerH);
    toolbar->setBounds (top.removeFromLeft (kHeaderW));
    ruler->setBounds (top);

    auto left = b.removeFromLeft (kHeaderW);
    headerHolder.setBounds (left);
    vp.setBounds (b);
    layoutRows();
}

void TimelineView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::panel);
    g.fillRect (0, 0, kHeaderW, kRulerH);
}

void TimelineView::timerCallback()
{
    if (rebuildPending)
    {
        rebuildPending = layoutPending = false;
        rebuild();
    }
    else if (layoutPending)
    {
        layoutPending = false;
        layoutRows();               // heights/positions may have changed
    }

    // scroll sync: headers track canvas Y, ruler tracks X
    if (vp.getViewPositionY() != lastViewY || vp.getViewPositionX() != lastViewX)
    {
        lastViewY = vp.getViewPositionY();
        lastViewX = vp.getViewPositionX();
        headerStrip.setTopLeftPosition (0, -lastViewY);
        ruler->repaint();
    }

    // playhead follow + repaint
    if (engine.isPlaying())
    {
        const int px = (int) timeToX (engine.getPositionSeconds());
        if (px < vp.getViewPositionX() || px > vp.getViewPositionX() + vp.getWidth() - 40)
            vp.setViewPosition (juce::jmax (0, px - 60), vp.getViewPositionY());
    }
    canvas->repaint();
    ruler->repaint();
}

void TimelineView::valueTreePropertyChanged (ValueTree& tree, const Identifier& prop)
{
    if (tree.hasType (id::CLIP) || tree.hasType (id::LANE) || tree.hasType (id::PT)
        || tree.hasType (id::TRANSPORT) || tree.hasType (id::MARKER))
        layoutPending = true;
    else if (tree.hasType (id::TRACK))
    {
        if (prop == id::height) layoutPending = true;   // live resize: never rebuild mid-drag
        else rebuildPending = true;
    }
}

void TimelineView::valueTreeChildAdded (ValueTree& parent, ValueTree&)
{
    if (parent.hasType (id::NOTES) || parent.hasType (id::LANE)) layoutPending = true;
    else rebuildPending = true;
}

void TimelineView::valueTreeChildRemoved (ValueTree& parent, ValueTree&, int)
{
    if (parent.hasType (id::NOTES) || parent.hasType (id::LANE)) layoutPending = true;
    else rebuildPending = true;
}

void TimelineView::valueTreeChildOrderChanged (ValueTree&, int, int)
{
    rebuildPending = true;
}

} // namespace dg
