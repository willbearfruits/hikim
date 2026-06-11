#include "SessionGrid.h"

namespace dg
{

SessionGrid::SessionGrid (AudioEngine& e, SessionModel& s, UIState& u)
    : engine (e), session (s), ui (u)
{
    quantBox.addItemList ({ "NONE", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS" }, 1);
    quantBox.setSelectedItemIndex (3, juce::dontSendNotification);
    quantBox.onChange = [this]
    {
        static const double q[] = { 0.0, 1.0, 2.0, 4.0, 8.0, 16.0 };
        engine.launchQuantizeBeats = q[juce::jmax (0, quantBox.getSelectedItemIndex())];
    };
    addAndMakeVisible (quantBox);

    stopAllBtn.onClick = [this] { engine.stopAllSession(); };
    addAndMakeVisible (stopAllBtn);

    addSceneBtn.onClick = [this]
    {
        session.undo.beginNewTransaction ("add scene");
        session.addScene ("Scene " + String (session.scenes().getNumChildren() + 1));
        repaint();
    };
    addAndMakeVisible (addSceneBtn);

    startTimerHz (15);
}

SessionGrid::~SessionGrid() = default;

std::vector<ValueTree> SessionGrid::gridTracks() const
{
    std::vector<ValueTree> out;
    for (const auto& t : session.tracks())
    {
        const String type = t[id::type];
        if (type == "audio" || type == "midi")
            out.push_back (t);
    }
    return out;
}

juce::Rectangle<int> SessionGrid::cellRect (int col, int row) const
{
    return { kSceneW + col * kColW - scrollX, kBarH + kHeadH + row * kRowH - scrollY,
             kColW - 4, kRowH - 4 };
}

SessionGrid::Cell SessionGrid::cellAt (juce::Point<int> p) const
{
    Cell c;
    if (p.y < kBarH) return c;
    const auto tracks = gridTracks();
    const int numScenes = session.scenes().getNumChildren();

    if (p.y < kBarH + kHeadH)                       // track header strip
    {
        const int col = (p.x - kSceneW + scrollX) / kColW;
        if (p.x >= kSceneW && col >= 0 && col < (int) tracks.size())
        {
            c.col = col;
            c.header = true;
            // bottom half of the header = stop button
            c.stopBtn = p.y > kBarH + kHeadH / 2;
        }
        return c;
    }

    const int row = (p.y - kBarH - kHeadH + scrollY) / kRowH;
    if (row < 0 || row >= numScenes) return c;
    c.row = row;
    if (p.x < kSceneW) { c.sceneBtn = true; return c; }
    const int col = (p.x - kSceneW + scrollX) / kColW;
    if (col >= 0 && col < (int) tracks.size())
        c.col = col;
    return c;
}

ValueTree SessionGrid::clipFor (int col, int row) const
{
    const auto tracks = gridTracks();
    if (col < 0 || col >= (int) tracks.size()) return {};
    auto scene = session.scenes().getChild (row);
    if (! scene.isValid()) return {};
    return session.getSlotClip (tracks[(size_t) col], scene[id::uid].toString());
}

void SessionGrid::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    const auto tracks = gridTracks();
    auto scenes = session.scenes();
    const int numScenes = scenes.getNumChildren();

    // top bar label
    g.setColour (col::dim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("SESSION  -  Tab / V / the SESSION button flips views", 8, 0, 400, kBarH, juce::Justification::centredLeft);

    // drop-target highlight while dragging files over the grid
    if (dragActive && dragHover.x >= 0)
    {
        auto r = cellRect (dragHover.x, dragHover.y);
        g.setColour (col::accent.withAlpha (0.25f));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (col::accent);
        g.drawRoundedRectangle (r.toFloat(), 3.0f, 2.0f);
        if (dragHover.x >= (int) tracks.size())
        {
            g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
            g.drawText ("NEW TRACK", r, juce::Justification::centred);
        }
    }

    // track headers
    for (int colIdx = 0; colIdx < (int) tracks.size(); ++colIdx)
    {
        const auto& t = tracks[(size_t) colIdx];
        juce::Rectangle<int> hr (kSceneW + colIdx * kColW - scrollX, kBarH, kColW - 4, kHeadH - 4);
        g.setColour (col::panel);
        g.fillRoundedRectangle (hr.toFloat(), 3.0f);
        g.setColour (juce::Colour::fromString (t.getProperty (id::colour, "ff808080").toString()));
        g.fillRect (hr.removeFromTop (3));
        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText (t[id::name].toString(), hr.removeFromTop (18).reduced (4, 0), juce::Justification::centredLeft);

        // stop button (square)
        auto st = engine.getSessionState (t[id::uid].toString());
        g.setColour (st.playing.isNotEmpty() ? col::text : col::dim.withAlpha (0.4f));
        g.fillRect (hr.getX() + 6, hr.getY() + 2, 10, 10);
    }

    // scene rows + cells
    const double blink = std::fmod (juce::Time::getMillisecondCounterHiRes() / 300.0, 2.0) < 1.0;
    for (int row = 0; row < numScenes; ++row)
    {
        auto scene = scenes.getChild (row);
        juce::Rectangle<int> sr (4, kBarH + kHeadH + row * kRowH - scrollY, kSceneW - 10, kRowH - 4);
        if (sr.getBottom() < kBarH + kHeadH) continue;

        g.setColour (col::panel);
        g.fillRoundedRectangle (sr.toFloat(), 3.0f);
        g.setColour (col::play.withAlpha (0.8f));
        juce::Path tri;
        tri.addTriangle ((float) sr.getX() + 6, (float) sr.getY() + 8,
                         (float) sr.getX() + 6, (float) sr.getBottom() - 8,
                         (float) sr.getX() + 16, (float) sr.getCentreY());
        g.fillPath (tri);
        g.setColour (col::dim);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText (scene[id::name].toString(), sr.reduced (20, 0), juce::Justification::centredLeft);

        for (int colIdx = 0; colIdx < (int) tracks.size(); ++colIdx)
        {
            const auto& t = tracks[(size_t) colIdx];
            auto r = cellRect (colIdx, row);
            if (r.getBottom() < kBarH + kHeadH || r.getY() > getHeight()) continue;

            auto clip = clipFor (colIdx, row);
            const bool hovered = hover == juce::Point<int> (colIdx, row);
            auto st = engine.getSessionState (t[id::uid].toString());

            if (! clip.isValid())
            {
                g.setColour (col::panel.withAlpha (hovered ? 0.9f : 0.5f));
                g.fillRoundedRectangle (r.toFloat(), 3.0f);
                if (hovered)
                {
                    g.setColour (col::dim);
                    g.drawText (t[id::type].toString() == "midi" ? "+" : "drop audio",
                                r, juce::Justification::centred);
                }
                continue;
            }

            const String cuid = clip[id::uid].toString();
            const bool playing = st.playing == cuid;
            const bool pending = st.pending == cuid;
            auto base = juce::Colour::fromString (t.getProperty (id::colour, "ff808080").toString());

            g.setColour (playing ? base.withAlpha (0.55f)
                         : pending && blink ? base.withAlpha (0.45f)
                         : col::panelHi);
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
            g.setColour (playing ? col::play : pending ? col::accent2 : base);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, playing ? 1.8f : 1.0f);

            // play triangle / playing bars
            g.setColour (playing ? col::play : col::text.withAlpha (0.8f));
            if (playing)
            {
                for (int b = 0; b < 3; ++b)
                    g.fillRect (r.getX() + 6 + b * 4,
                                r.getCentreY() - 5 + (int) (4.0 * std::abs (std::sin (
                                    juce::Time::getMillisecondCounterHiRes() * 0.006 + b))),
                                2, 9);
            }
            else
            {
                juce::Path p2;
                p2.addTriangle ((float) r.getX() + 6, (float) r.getY() + 8,
                                (float) r.getX() + 6, (float) r.getBottom() - 8,
                                (float) r.getX() + 15, (float) r.getCentreY());
                g.fillPath (p2);
            }

            g.setColour (col::text);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText (clip[id::name].toString(), r.reduced (20, 2), juce::Justification::centredLeft);
        }
    }
}

void SessionGrid::mouseDown (const juce::MouseEvent& e)
{
    auto c = cellAt (e.getPosition());
    const auto tracks = gridTracks();

    if (c.header && c.col >= 0)
    {
        const auto& t = tracks[(size_t) c.col];
        ui.selectedTrack = t[id::uid].toString();
        if (c.stopBtn)
            engine.stopTrackSession (t[id::uid].toString());
        repaint();
        return;
    }
    if (c.sceneBtn) { launchScene (c.row); return; }
    if (c.col < 0 || c.row < 0) return;

    if (e.mods.isPopupMenu()) { showCellMenu (c.col, c.row); return; }

    auto& track = tracks[(size_t) c.col];
    auto clip = clipFor (c.col, c.row);
    if (clip.isValid())
    {
        engine.launchSlot (track, clip);
        return;
    }
    // empty midi cell: create a loop and open it
    if (track[id::type].toString() == "midi")
    {
        auto scene = session.scenes().getChild (c.row);
        auto created = createMidiSlotClip (track, scene[id::uid].toString());
        if (ui.openPianoRoll) ui.openPianoRoll (created);
    }
}

ValueTree SessionGrid::createMidiSlotClip (ValueTree track, const String& sceneUid)
{
    session.undo.beginNewTransaction ("new slot clip");
    auto map = engine.getTempoMap();
    const double bpb = map->beatsPerBarAt (map->samplesToBeats (engine.getPositionSamples()));

    ValueTree c (id::CLIP);
    c.setProperty (id::uid, SessionModel::newUID(), nullptr);
    c.setProperty (id::type, "midi", nullptr);
    c.setProperty (id::name, "loop", nullptr);
    c.setProperty (id::start, 0.0, nullptr);
    c.setProperty (id::length, map->beatsToSeconds (bpb), nullptr);
    c.setProperty (id::loopBeats, bpb, nullptr);
    c.appendChild (ValueTree (id::NOTES), nullptr);
    session.setSlotClip (track, sceneUid, c);
    return c;
}

void SessionGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto c = cellAt (e.getPosition());
    auto clip = clipFor (c.col, c.row);
    if (clip.isValid() && clip[id::type].toString() == "midi" && ui.openPianoRoll)
        ui.openPianoRoll (clip);
}

void SessionGrid::launchScene (int row)
{
    auto scene = session.scenes().getChild (row);
    if (! scene.isValid()) return;
    for (auto& t : gridTracks())
    {
        auto clip = session.getSlotClip (t, scene[id::uid].toString());
        if (clip.isValid())
            engine.launchSlot (t, clip);
    }
}

void SessionGrid::showCellMenu (int colIdx, int row)
{
    auto clip = clipFor (colIdx, row);
    const auto tracks = gridTracks();
    auto track = tracks[(size_t) colIdx];
    auto scene = session.scenes().getChild (row);

    juce::PopupMenu m;
    if (clip.isValid())
    {
        m.addItem (1, "Delete clip");
        if (clip[id::type].toString() == "midi")
        {
            m.addItem (2, "Open piano roll");
            juce::PopupMenu loopMenu;
            for (int bars : { 1, 2, 4, 8 })
                loopMenu.addItem (10 + bars, String (bars) + (bars == 1 ? " bar" : " bars"));
            m.addSubMenu ("Loop length", loopMenu);
        }
        m.addItem (3, "Rename...");
    }
    else
        return;

    auto* view = this;
    ValueTree clipC = clip, trackC = track, sceneC = scene;
    m.showMenuAsync ({}, [view, clipC, trackC, sceneC] (int r) mutable
    {
        auto& sess = view->session;
        if (r == 1)
        {
            sess.undo.beginNewTransaction ("delete slot clip");
            sess.setSlotClip (trackC, sceneC[id::uid].toString(), {});
        }
        else if (r == 2 && view->ui.openPianoRoll)
            view->ui.openPianoRoll (clipC);
        else if (r == 3)
        {
            auto* w = new juce::AlertWindow ("Rename clip", {}, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("n", clipC[id::name].toString());
            w->addButton ("OK", 1); w->addButton ("Cancel", 0);
            w->enterModalState (true, juce::ModalCallbackFunction::create (
                [clipC, w, view] (int res) mutable
                { if (res == 1) clipC.setProperty (id::name, w->getTextEditorContents ("n"), &view->session.undo); }), true);
        }
        else if (r >= 11 && r <= 18)
        {
            auto map = view->engine.getTempoMap();
            const double bpb = map->beatsPerBarAt (0.0);
            const double beats = (r - 10) * bpb;
            clipC.setProperty (id::loopBeats, beats, &view->session.undo);
            clipC.setProperty (id::length, map->beatsToSeconds (beats), &view->session.undo);
        }
        view->repaint();
    });
}

void SessionGrid::mouseMove (const juce::MouseEvent& e)
{
    auto c = cellAt (e.getPosition());
    hover = { c.col, c.row };
}

void SessionGrid::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const auto tracks = gridTracks();
    const int maxX = juce::jmax (0, (int) tracks.size() * kColW - (getWidth() - kSceneW) + 40);
    const int maxY = juce::jmax (0, session.scenes().getNumChildren() * kRowH - (getHeight() - kBarH - kHeadH) + 40);
    if (w.deltaX != 0 || juce::ModifierKeys::currentModifiers.isShiftDown())
        scrollX = juce::jlimit (0, maxX, scrollX - (int) ((w.deltaX != 0 ? w.deltaX : w.deltaY) * 120));
    else
        scrollY = juce::jlimit (0, maxY, scrollY - (int) (w.deltaY * 120));
    repaint();
}

int SessionGrid::dropColumnAt (int x) const
{
    if (x < kSceneW) return -1;
    const int col = (x - kSceneW + scrollX) / kColW;
    const int n = (int) gridTracks().size();
    return col >= 0 && col <= n ? col : -1;     // == n means "make a new audio track"
}

int SessionGrid::dropRowAt (int y) const
{
    if (y < kBarH + kHeadH) return -1;
    return (y - kBarH - kHeadH + scrollY) / kRowH;
}

void SessionGrid::updateDragHover (juce::Point<int> pos)
{
    dragActive = true;
    const int col = dropColumnAt (pos.x);
    const int row = dropRowAt (pos.y);
    const auto tracks = gridTracks();
    // highlight only valid targets: audio columns or the "new track" lane
    const bool ok = col >= 0 && row >= 0
                    && (col == (int) tracks.size()
                        || tracks[(size_t) col][id::type].toString() == "audio");
    dragHover = ok ? juce::Point<int> (col, row) : juce::Point<int> (-1, -1);
    repaint();
}

bool SessionGrid::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;m4a;opus;aac;wv;wma;mp2;amr;mka;caf"))
            return true;
    return false;
}

void SessionGrid::fileDragMove (const juce::StringArray&, int x, int y) { updateDragHover ({ x, y }); }
void SessionGrid::fileDragExit (const juce::StringArray&)               { dragActive = false; dragHover = { -1, -1 }; repaint(); }

void SessionGrid::filesDropped (const juce::StringArray& files, int x, int y)
{
    dropFiles (files, { x, y });
}

bool SessionGrid::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString() == "binfiles";
}

void SessionGrid::itemDragMove (const SourceDetails& d) { updateDragHover (d.localPosition); }
void SessionGrid::itemDragExit (const SourceDetails&)   { dragActive = false; dragHover = { -1, -1 }; repaint(); }

void SessionGrid::itemDropped (const SourceDetails& d)
{
    if (auto* ftc = dynamic_cast<juce::FileTreeComponent*> (d.sourceComponent.get()))
    {
        juce::StringArray files;
        for (int i = 0; i < ftc->getNumSelectedFiles(); ++i)
            files.add (ftc->getSelectedFile (i).getFullPathName());
        dropFiles (files, d.localPosition);
    }
}

void SessionGrid::dropFiles (const juce::StringArray& files, juce::Point<int> pos)
{
    dragActive = false;
    dragHover = { -1, -1 };

    const int col = dropColumnAt (pos.x);
    int row = dropRowAt (pos.y);
    if (col < 0 || row < 0) { repaint(); return; }

    auto tracks = gridTracks();
    session.undo.beginNewTransaction ("drop slot clips");

    ValueTree track;
    if (col >= (int) tracks.size())
        track = session.addTrack ("audio", "Audio " + String (session.tracks().getNumChildren()));
    else
    {
        track = tracks[(size_t) col];
        if (track[id::type].toString() != "audio") { repaint(); return; }
    }

    // multiple files fill consecutive scenes downward, growing scene rows as needed
    for (const auto& fpath : files)
    {
        auto reader = engine.createAnyReader (File (fpath));
        if (reader == nullptr) continue;

        while (row >= session.scenes().getNumChildren())
            session.addScene ("Scene " + String (session.scenes().getNumChildren() + 1));
        auto scene = session.scenes().getChild (row);

        ValueTree clip (id::CLIP);
        clip.setProperty (id::uid, SessionModel::newUID(), nullptr);
        clip.setProperty (id::type, "audio", nullptr);
        clip.setProperty (id::name, File (fpath).getFileNameWithoutExtension(), nullptr);
        clip.setProperty (id::file, File (fpath).getFullPathName(), nullptr);
        clip.setProperty (id::fileSR, reader->sampleRate, nullptr);
        clip.setProperty (id::start, 0.0, nullptr);
        clip.setProperty (id::length, (double) reader->lengthInSamples / reader->sampleRate, nullptr);
        clip.setProperty (id::offset, 0.0, nullptr);
        clip.setProperty (id::clipGain, 0.0, nullptr);
        clip.setProperty (id::stretch, 1.0, nullptr);
        session.setSlotClip (track, scene[id::uid].toString(), clip);
        ++row;
    }
    repaint();
}

void SessionGrid::resized()
{
    auto bar = getLocalBounds().removeFromTop (kBarH).reduced (4, 3);
    bar.removeFromLeft (290);
    quantBox.setBounds (bar.removeFromLeft (90));
    bar.removeFromLeft (6);
    stopAllBtn.setBounds (bar.removeFromLeft (80));
    bar.removeFromLeft (6);
    addSceneBtn.setBounds (bar.removeFromLeft (80));
}

} // namespace dg
