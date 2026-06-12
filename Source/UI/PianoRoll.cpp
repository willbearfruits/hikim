#include "PianoRoll.h"

namespace dg
{

static const double kGridChoices[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 0.0625 };
static const int kScales[][7] = {
    { 0, 2, 4, 5, 7, 9, 11 },   // major
    { 0, 2, 3, 5, 7, 8, 10 },   // minor
    { 0, 2, 3, 5, 7, 9, 10 },   // dorian
    { 0, 1, 3, 5, 7, 8, 10 },   // phrygian
    { 0, 2, 4, 5, 7, 9, 10 },   // mixolydian
    { 0, 3, 5, 7, 10, 0, 3 },   // minor penta (padded)
    { 0, 2, 4, 7, 9, 0, 2 },    // major penta (padded)
};

// =========================================================================== Keys
class PianoRoll::Keys : public juce::Component
{
public:
    explicit Keys (PianoRoll& o) : pr (o) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::panel);
        for (int n = kLowNote; n <= kHighNote; ++n)
        {
            const int y = noteY (n);
            const int pc = n % 12;
            const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            g.setColour (black ? col::bg : col::panelHi.brighter (0.25f));
            g.fillRect (0, y, getWidth(), kNoteH - 1);
            if (pc == 0)
            {
                g.setColour (col::dim);
                g.setFont (juce::Font (juce::FontOptions (9.0f)));
                g.drawText ("C" + String (n / 12 - 1), 2, y, getWidth() - 4, kNoteH, juce::Justification::centredRight);
            }
        }
    }

    int noteY (int note) const { return (kHighNote - note) * kNoteH; }

private:
    PianoRoll& pr;
};

// =========================================================================== Grid
class PianoRoll::Grid : public juce::Component
{
public:
    explicit Grid (PianoRoll& o) : pr (o) {}

    int noteY (int note) const  { return (kHighNote - note) * kNoteH; }
    int yToNote (int y) const   { return juce::jlimit (kLowNote, kHighNote, kHighNote - y / kNoteH); }
    int notesAreaH() const      { return (kHighNote - kLowNote + 1) * kNoteH; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);
        if (! pr.clip.isValid()) return;

        const double lenBeats = pr.clipLenBeats();

        // horizontal pitch lanes
        for (int n = kLowNote; n <= kHighNote; ++n)
        {
            const int pc = n % 12;
            const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            if (black) { g.setColour (col::panel.withAlpha (0.5f)); g.fillRect (0, noteY (n), getWidth(), kNoteH); }
            if (pc == 0 || pc == 5) { g.setColour (col::line.withAlpha (0.5f)); g.drawHorizontalLine (noteY (n) + kNoteH, 0, (float) getWidth()); }
        }

        // beat grid
        const double grid = pr.gridBeats();
        for (double b = 0; b <= lenBeats + 1.0e-6; b += grid)
        {
            const int x = (int) (b * pr.ppb);
            const bool whole = std::abs (b - std::round (b)) < 1.0e-6;
            g.setColour (col::line.withAlpha (whole ? 0.9f : 0.35f));
            g.drawVerticalLine (x, 0, (float) notesAreaH());
        }
        g.setColour (col::accent.withAlpha (0.25f));
        g.fillRect ((int) (lenBeats * pr.ppb), 0, getWidth() - (int) (lenBeats * pr.ppb), getHeight());

        // velocity strip
        const int velTop = notesAreaH();
        g.setColour (col::panel);
        g.fillRect (0, velTop, getWidth(), kVelH);
        g.setColour (col::dim);
        g.drawText ("VELOCITY", 4, velTop, 80, 12, juce::Justification::left);

        // notes
        auto notes = pr.clip.getChildWithName (id::NOTES);
        for (int i = 0; i < notes.getNumChildren(); ++i)
        {
            auto n = notes.getChild (i);
            const double b = n[id::beat], len = n[id::len];
            const int pitch = n[id::pitch], vel = n[id::vel];
            const bool sel = pr.selected.count (i) > 0;
            juce::Rectangle<float> r ((float) (b * pr.ppb), (float) noteY (pitch),
                                      juce::jmax (4.0f, (float) (len * pr.ppb)), (float) kNoteH - 1);
            g.setColour (col::clipMidi.withAlpha (0.3f + 0.6f * vel / 127.0f));
            g.fillRect (r);
            g.setColour (sel ? juce::Colours::white : col::clipMidi.brighter (0.5f));
            g.drawRect (r, sel ? 1.6f : 1.0f);

            // velocity bar
            g.setColour (sel ? juce::Colours::white : col::clipMidi.brighter (0.3f));
            const float vh = (float) (kVelH - 14) * vel / 127.0f;
            g.fillRect ((float) (b * pr.ppb), (float) (velTop + kVelH - vh), 3.0f, vh);
        }

        // playhead (relative to clip start)
        const double phBeat = pr.engine.getTempoMap()->secondsToBeats (pr.engine.getPositionSeconds())
                            - pr.engine.getTempoMap()->secondsToBeats ((double) pr.clip[id::start]);
        if (phBeat >= 0 && phBeat <= lenBeats)
        {
            g.setColour (col::record);
            g.drawVerticalLine ((int) (phBeat * pr.ppb), 0, (float) getHeight());
        }

        // marquee
        if (marquee)
        {
            g.setColour (col::accent.withAlpha (0.15f));
            g.fillRect (marqueeRect);
            g.setColour (col::accent);
            g.drawRect (marqueeRect);
        }
    }

    int hitNote (const juce::MouseEvent& e, bool& nearRightEdge)
    {
        auto notes = pr.clip.getChildWithName (id::NOTES);
        for (int i = notes.getNumChildren(); --i >= 0;)
        {
            auto n = notes.getChild (i);
            const int x0 = (int) ((double) n[id::beat] * pr.ppb);
            const int x1 = (int) (((double) n[id::beat] + (double) n[id::len]) * pr.ppb);
            const int y = noteY (n[id::pitch]);
            if (e.x >= x0 && e.x <= x1 + 2 && e.y >= y && e.y < y + kNoteH)
            {
                // tail zone scales with the note (5..8px) and never eats a
                // note too narrow to also grab by the body
                const int w = x1 - x0;
                nearRightEdge = w > 10 && e.x > x1 - juce::jmax (5, juce::jmin (8, w / 3));
                return i;
            }
        }
        return -1;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! pr.clip.isValid()) return;
        pr.grabKeyboardFocus();                // keys 1/2/3 + note clipboard act on the roll now
        pr.session.undo.beginNewTransaction ("piano roll");
        auto notes = pr.clip.getChildWithName (id::NOTES);
        marquee = false;
        dragMode = Drag::none;

        if (e.y >= notesAreaH())               // velocity strip works in every tool
        {
            dragMode = Drag::velocity;
            editVelocity (e);
            return;
        }

        bool rightEdge = false;
        const int hit = hitNote (e, rightEdge);

        if (e.mods.isPopupMenu())              // right-click erases in every tool
        {
            if (hit >= 0) { notes.removeChild (hit, &pr.session.undo); pr.selected.clear(); repaint(); }
            return;
        }

        if (pr.tool == RollTool::erase)
        {
            dragMode = Drag::eraseSweep;
            pr.selected.clear();
            if (hit >= 0) notes.removeChild (hit, &pr.session.undo);
            repaint();
            return;
        }

        if (hit >= 0)
        {
            if (! e.mods.isShiftDown() && pr.selected.count (hit) == 0)
                pr.selected.clear();
            pr.selected.insert (hit);
            // pencil resizes from anywhere on the note; select needs the tail
            dragMode = (pr.tool == RollTool::pencil || rightEdge) ? Drag::resize : Drag::move;
            dragAnchor = e.position;
            origNotes.clear();
            for (int i : pr.selected)
            {
                auto n = notes.getChild (i);
                if (n.isValid())
                    origNotes[i] = { (double) n[id::beat], (double) n[id::len], (int) n[id::pitch] };
            }
        }
        else if (pr.tool == RollTool::pencil && ! e.mods.isShiftDown())
        {
            // draw: the note is born one grid long and its tail follows the drag
            const double grid = pr.gridBeats();
            const double b = juce::jmax (0.0, e.mods.isAltDown()
                                                  ? (double) e.x / pr.ppb
                                                  : std::floor ((double) e.x / pr.ppb / grid) * grid);
            const int pitch = pr.snapPitch (yToNote (e.y));
            ValueTree n (id::NOTE);
            n.setProperty (id::beat, b, nullptr);
            n.setProperty (id::len, grid, nullptr);
            n.setProperty (id::pitch, pitch, nullptr);
            n.setProperty (id::vel, 100, nullptr);
            notes.appendChild (n, &pr.session.undo);
            pr.selected.clear();
            pr.selected.insert (notes.indexOf (n));
            dragMode = Drag::resize;
            dragAnchor = e.position;
            origNotes.clear();
            origNotes[notes.indexOf (n)] = { b, grid, pitch };
        }
        else
        {
            // empty space rubber-bands; Shift keeps what you already had
            marquee = true;
            marqueeRect = { e.x, e.y, 0, 0 };
            marqueeAnchor = e.getPosition();
            marqueeBase = e.mods.isShiftDown() ? pr.selected : std::set<int>();
            if (! e.mods.isShiftDown())
                pr.selected.clear();
        }
        repaint();
    }

    void editVelocity (const juce::MouseEvent& e)
    {
        auto notes = pr.clip.getChildWithName (id::NOTES);
        const int vel = juce::jlimit (1, 127, (int) (127.0 * (notesAreaH() + kVelH - e.y) / (double) (kVelH - 14)));
        const double beat = (double) e.x / pr.ppb;
        // nearest note start within half a grid
        int best = -1; double bestDist = pr.gridBeats();
        for (int i = 0; i < notes.getNumChildren(); ++i)
        {
            const double d = std::abs ((double) notes.getChild (i)[id::beat] - beat);
            if (d < bestDist) { bestDist = d; best = i; }
        }
        if (best >= 0)
        {
            if (! pr.selected.empty() && pr.selected.count (best) > 0)
                for (int i : pr.selected)
                    notes.getChild (i).setProperty (id::vel, vel, &pr.session.undo);
            else
                notes.getChild (best).setProperty (id::vel, vel, &pr.session.undo);
            repaint();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! pr.clip.isValid()) return;
        auto notes = pr.clip.getChildWithName (id::NOTES);

        if (dragMode == Drag::velocity) { editVelocity (e); return; }
        if (marquee)
        {
            marqueeRect = juce::Rectangle<int> (marqueeAnchor, e.getPosition());
            pr.selected = marqueeBase;
            for (int i = 0; i < notes.getNumChildren(); ++i)
            {
                auto n = notes.getChild (i);
                juce::Rectangle<int> nr ((int) ((double) n[id::beat] * pr.ppb), noteY (n[id::pitch]),
                                         juce::jmax (4, (int) ((double) n[id::len] * pr.ppb)), kNoteH);
                if (marqueeRect.intersects (nr)) pr.selected.insert (i);
            }
            repaint();
            return;
        }
        if (dragMode == Drag::eraseSweep)
        {
            bool edge = false;
            const int hit = hitNote (e, edge);
            if (hit >= 0) { notes.removeChild (hit, &pr.session.undo); repaint(); }
            return;
        }
        if (dragMode == Drag::none) return;

        const double db = (e.position.x - dragAnchor.x) / pr.ppb;
        const int dp = (int) std::round ((dragAnchor.y - e.position.y) / (float) kNoteH);
        const double grid = pr.gridBeats();
        const bool fine = e.mods.isAltDown();          // alt = no snap (off-grid)

        for (auto& [i, on] : origNotes)
        {
            auto n = notes.getChild (i);
            if (! n.isValid()) continue;
            if (dragMode == Drag::move)
            {
                double nb = on.beat + db;
                if (! fine) nb = std::round (nb / grid) * grid;
                n.setProperty (id::beat, juce::jmax (0.0, nb), &pr.session.undo);
                n.setProperty (id::pitch, pr.snapPitch (juce::jlimit (kLowNote, kHighNote, on.pitch + dp)), &pr.session.undo);
            }
            else        // Drag::resize: every selected tail moves together, relative
            {
                double nl = on.len + db;
                if (! fine) nl = juce::jmax (grid, std::round (nl / grid) * grid);
                n.setProperty (id::len, juce::jmax (0.02, nl), &pr.session.undo);
            }
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        marquee = false;
        dragMode = Drag::none;
        repaint();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (! pr.clip.isValid()) { setMouseCursor (juce::MouseCursor::NormalCursor); return; }
        if (e.y >= notesAreaH()) { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); return; }
        if (pr.tool == RollTool::erase) { setMouseCursor (juce::MouseCursor::CrosshairCursor); return; }
        bool rightEdge = false;
        const int hit = hitNote (e, rightEdge);
        if (pr.tool == RollTool::pencil)
            setMouseCursor (hit >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                                     : juce::MouseCursor::CopyingCursor);
        else
            setMouseCursor (hit >= 0 && rightEdge ? juce::MouseCursor::LeftRightResizeCursor
                                                  : juce::MouseCursor::NormalCursor);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        {
            pr.ppb = juce::jlimit (12.0, 600.0, pr.ppb * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15));
            pr.resized();
            repaint();
            return;
        }
        Component::mouseWheelMove (e, w);
    }

private:
    PianoRoll& pr;
    struct ON { double beat, len; int pitch; };
    std::map<int, ON> origNotes;
    juce::Point<float> dragAnchor;
    juce::Point<int> marqueeAnchor;
    juce::Rectangle<int> marqueeRect;
    std::set<int> marqueeBase;               // shift-marquee adds to this
    bool marquee = false;
    enum class Drag { none, move, resize, velocity, eraseSweep };
    Drag dragMode = Drag::none;
};

// =========================================================================== ToolStrip

namespace
{
    class RollToolButton : public juce::Button
    {
    public:
        RollToolButton (const juce::Path& p, const String& name) : juce::Button (name), icon (p) {}

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            if (getToggleState())  { g.setColour (col::accent.withAlpha (0.3f)); g.fillRoundedRectangle (r, 2.0f); }
            else if (over || down) { g.setColour (col::panelHi); g.fillRoundedRectangle (r, 2.0f); }
            g.setColour (getToggleState() ? col::accent : col::text);
            g.fillPath (icon, icon.getTransformToScaleToFit (r.reduced (4.0f), true));
        }

    private:
        juce::Path icon;
    };

    juce::Path rollArrowIcon()
    {
        juce::Path p;
        p.startNewSubPath (0, 0); p.lineTo (0, 14); p.lineTo (4, 10);
        p.lineTo (7, 16); p.lineTo (9.5f, 14.5f); p.lineTo (6.5f, 9);
        p.lineTo (11, 8.5f); p.closeSubPath();
        return p;
    }
    juce::Path rollPencilIcon()
    {
        juce::Path p;                                   // body + tip
        p.addQuadrilateral (9.0f, 0.0f, 12.0f, 3.0f, 4.5f, 10.5f, 1.5f, 7.5f);
        p.addTriangle (0.0f, 12.0f, 0.6f, 8.5f, 3.5f, 11.4f);
        return p;
    }
    juce::Path rollEraseIcon()
    {
        juce::Path p;
        p.addLineSegment ({ 1, 1, 11, 11 }, 2.4f);
        p.addLineSegment ({ 11, 1, 1, 11 }, 2.4f);
        return p;
    }
}

class PianoRoll::ToolStrip : public juce::Component
{
public:
    explicit ToolStrip (PianoRoll& o) : pr (o)
    {
        add (rollArrowIcon(), "select (1): drag empty space to rubber-band, note tails resize", RollTool::select);
        add (rollPencilIcon(), "pencil (2): drag draws a note, drag a note to resize it, right-click erases", RollTool::pencil);
        add (rollEraseIcon(), "erase (3): click or sweep notes away", RollTool::erase);
        sync();
    }

    void sync()
    {
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (pr.tool == tools[(size_t) i], juce::dontSendNotification);
        repaint();
    }

    void resized() override
    {
        auto b = getLocalBounds();
        for (auto* btn : buttons)
        {
            btn->setBounds (b.removeFromLeft (24));
            b.removeFromLeft (2);
        }
    }

private:
    void add (const juce::Path& icon, const String& name, RollTool t)
    {
        auto* b = buttons.add (new RollToolButton (icon, name));
        b->setTooltip (name);
        b->setWantsKeyboardFocus (false);    // the roll keeps the keys, not the button
        b->onClick = [this, t] { pr.setTool (t); };
        addAndMakeVisible (b);
        tools.push_back (t);
    }

    PianoRoll& pr;
    juce::OwnedArray<RollToolButton> buttons;
    std::vector<RollTool> tools;
};

// =========================================================================== PianoRoll

PianoRoll::PianoRoll (AudioEngine& e, SessionModel& s, UIState& u)
    : engine (e), session (s), ui (u)
{
    setWantsKeyboardFocus (true);            // clicking anywhere in the roll routes keys here
    keys = std::make_unique<Keys> (*this);
    grid = std::make_unique<Grid> (*this);
    vp.setViewedComponent (grid.get(), false);
    addAndMakeVisible (vp);
    keysHolder.addAndMakeVisible (*keys);
    addAndMakeVisible (keysHolder);

    toolStrip = std::make_unique<ToolStrip> (*this);
    addAndMakeVisible (*toolStrip);

    gridBox.addItemList ({ "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", "1/64" }, 1);
    gridBox.setSelectedItemIndex (2);
    addAndMakeVisible (gridBox);

    scaleBox.addItemList ({ "CHROMATIC", "MAJOR", "MINOR", "DORIAN", "PHRYGIAN", "MIXO", "PENTA MIN", "PENTA MAJ" }, 1);
    scaleBox.setSelectedItemIndex (0);
    addAndMakeVisible (scaleBox);

    auto setupSlider = [this] (juce::Slider& sl, double lo, double hi, double def)
    {
        sl.setRange (lo, hi);
        sl.setValue (def, juce::dontSendNotification);
        sl.setSliderStyle (juce::Slider::LinearBar);
        sl.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        addAndMakeVisible (sl);
    };
    setupSlider (strengthSl, 0.0, 1.0, 1.0);
    setupSlider (swingSl, 0.0, 0.75, 0.0);
    setupSlider (humanSl, 0.0, 1.0, 0.25);

    quantBtn.onClick = [this] { quantizeSelected (false); };
    humanBtn.onClick = [this] { quantizeSelected (true); };
    stepBtn.setClickingTogglesState (true);
    stepBtn.onClick = [this] { stepPos = 0.0; };
    addAndMakeVisible (quantBtn);
    addAndMakeVisible (humanBtn);
    addAndMakeVisible (stepBtn);

    clipLabel.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    clipLabel.setColour (juce::Label::textColourId, col::accent2);
    addAndMakeVisible (clipLabel);

    startTimerHz (24);
}

PianoRoll::~PianoRoll() = default;

void PianoRoll::setClip (ValueTree midiClip)
{
    clip = midiClip;
    selected.clear();
    stepPos = 0.0;
    clipLabel.setText (clip.isValid() ? clip[id::name].toString() : "(no clip)", juce::dontSendNotification);
    resized();
    vp.setViewPosition (0, (kHighNote - 84) * kNoteH);
    repaint();
}

bool PianoRoll::keyPressed (const juce::KeyPress& k)
{
    if (! clip.isValid()) return false;
    const int kc = k.getKeyCode();
    auto is = [kc] (char c) { return kc == c || kc == c + 32; };   // letter, either case

    if (k.getModifiers().isCommandDown())
    {
        if (is ('C')) { copySelectedNotes (false); return true; }
        if (is ('X')) { copySelectedNotes (true); return true; }
        if (is ('V')) { pasteClipboard(); return true; }
        if (is ('D')) { duplicateSelectedNotes(); return true; }
        if (is ('A')) { selectAllNotes(); return true; }
        return false;                        // Ctrl+Z/S/... stay global
    }
    if (kc == '1' || kc == '2' || kc == '3')
    {
        setTool (kc == '1' ? RollTool::select : kc == '2' ? RollTool::pencil : RollTool::erase);
        return true;
    }
    if (kc == juce::KeyPress::deleteKey || kc == juce::KeyPress::backspaceKey)
    {
        deleteSelectedNotes();
        return true;                         // never falls through to clip delete mid-edit
    }
    if (kc == juce::KeyPress::escapeKey && ! selected.empty())
    {
        selected.clear();
        grid->repaint();
        return true;
    }
    return false;
}

void PianoRoll::copySelectedNotes (bool cut)
{
    if (! clip.isValid() || selected.empty()) return;
    noteClipboard = clipops::copyNotes (clip, selected);
    if (cut)
    {
        clipops::deleteNotes (session, clip, selected);
        selected.clear();
        grid->repaint();
    }
}

void PianoRoll::pasteClipboard()
{
    if (! clip.isValid() || noteClipboard.empty()) return;
    auto map = engine.getTempoMap();
    const double phBeat = map->secondsToBeats (engine.getPositionSeconds())
                        - map->secondsToBeats ((double) clip[id::start]);
    double at = 0.0;                         // playhead elsewhere: land at the clip start
    if (phBeat > 0.0 && phBeat < clipLenBeats())
        at = std::round (phBeat / gridBeats()) * gridBeats();
    const auto fresh = clipops::pasteNotes (session, clip, noteClipboard, at);
    selected = std::set<int> (fresh.begin(), fresh.end());
    grid->repaint();
}

void PianoRoll::duplicateSelectedNotes()
{
    if (! clip.isValid() || selected.empty()) return;
    const auto fresh = clipops::duplicateNotes (session, clip, selected);
    if (! fresh.empty())
        selected = std::set<int> (fresh.begin(), fresh.end());
    grid->repaint();
}

void PianoRoll::deleteSelectedNotes()
{
    if (! clip.isValid() || selected.empty()) return;
    clipops::deleteNotes (session, clip, selected);
    selected.clear();
    grid->repaint();
}

void PianoRoll::selectAllNotes()
{
    if (! clip.isValid()) return;
    auto notes = clip.getChildWithName (id::NOTES);
    selected.clear();
    for (int i = 0; i < notes.getNumChildren(); ++i)
        selected.insert (i);
    grid->repaint();
}

void PianoRoll::setTool (RollTool t)
{
    tool = t;
    if (toolStrip != nullptr)
        toolStrip->sync();
}

double PianoRoll::gridBeats() const
{
    return kGridChoices[juce::jmax (0, gridBox.getSelectedItemIndex())];
}

double PianoRoll::clipLenBeats() const
{
    if (! clip.isValid()) return 4.0;
    auto map = engine.getTempoMap();
    return map->secondsToBeats ((double) clip[id::start] + (double) clip[id::length])
         - map->secondsToBeats ((double) clip[id::start]);
}

int PianoRoll::snapPitch (int note) const
{
    const int s = scaleBox.getSelectedItemIndex() - 1;
    if (s < 0) return note;
    const int pc = note % 12;
    int best = 0, bestDist = 12;
    for (int k = 0; k < 7; ++k)
    {
        const int d = std::abs (kScales[s][k] - pc);
        if (d < bestDist) { bestDist = d; best = kScales[s][k]; }
    }
    return juce::jlimit (kLowNote, kHighNote, note - pc + best);
}

void PianoRoll::quantizeSelected (bool humanizeOnly)
{
    if (! clip.isValid()) return;
    session.undo.beginNewTransaction (humanizeOnly ? "humanize" : "quantize");
    auto notes = clip.getChildWithName (id::NOTES);
    const double gridLen = gridBeats();
    const double strength = strengthSl.getValue();
    const double swing = swingSl.getValue();
    const double human = humanSl.getValue();
    auto& rng = juce::Random::getSystemRandom();

    for (int i = 0; i < notes.getNumChildren(); ++i)
    {
        if (! selected.empty() && selected.count (i) == 0) continue;
        auto n = notes.getChild (i);
        double b = n[id::beat];

        if (! humanizeOnly)
        {
            const double slot = std::round (b / gridLen);
            double target = slot * gridLen;
            if (((juce::int64) slot & 1) == 1) target += swing * gridLen;   // swing the off-slots
            b = b + (target - b) * strength;
        }
        else
        {
            b += (rng.nextDouble() * 2.0 - 1.0) * human * gridLen * 0.5;
            const int v = juce::jlimit (1, 127, (int) n[id::vel] + rng.nextInt (21) - 10);
            n.setProperty (id::vel, v, &session.undo);
        }
        n.setProperty (id::beat, juce::jmax (0.0, b), &session.undo);
    }
    grid->repaint();
}

void PianoRoll::timerCallback()
{
    if (! clip.isValid()) return;

    // MIDI step input: incoming notes append at the step cursor
    if (stepBtn.getToggleState())
    {
        for (const auto& m : engine.drainUiMidi())
        {
            if (! m.isNoteOn()) continue;
            auto notes = clip.getChildWithName (id::NOTES);
            ValueTree n (id::NOTE);
            n.setProperty (id::beat, stepPos, nullptr);
            n.setProperty (id::len, gridBeats(), nullptr);
            n.setProperty (id::pitch, snapPitch (m.getNoteNumber()), nullptr);
            n.setProperty (id::vel, (int) m.getVelocity(), nullptr);
            notes.appendChild (n, &session.undo);
            stepPos += gridBeats();
            if (stepPos >= clipLenBeats()) stepPos = 0.0;
        }
    }
    keys->setTopLeftPosition (0, -vp.getViewPositionY());
    grid->repaint();
}

void PianoRoll::resized()
{
    auto b = getLocalBounds();
    auto bar = b.removeFromTop (26).reduced (2);
    toolStrip->setBounds (bar.removeFromLeft (78).reduced (0, 1));
    bar.removeFromLeft (6);
    clipLabel.setBounds (bar.removeFromLeft (120));
    gridBox.setBounds (bar.removeFromLeft (70).reduced (1));
    bar.removeFromLeft (4);
    quantBtn.setBounds (bar.removeFromLeft (74).reduced (1));
    bar.removeFromLeft (2);
    auto strengthArea = bar.removeFromLeft (60).reduced (1, 4);
    strengthSl.setBounds (strengthArea);
    bar.removeFromLeft (6);
    swingSl.setBounds (bar.removeFromLeft (60).reduced (1, 4));
    bar.removeFromLeft (6);
    humanBtn.setBounds (bar.removeFromLeft (80).reduced (1));
    humanSl.setBounds (bar.removeFromLeft (60).reduced (1, 4));
    bar.removeFromLeft (8);
    scaleBox.setBounds (bar.removeFromLeft (100).reduced (1));
    bar.removeFromLeft (8);
    stepBtn.setBounds (bar.removeFromLeft (80).reduced (1));

    keysHolder.setBounds (b.removeFromLeft (52));
    vp.setBounds (b);

    const int gridH = (kHighNote - kLowNote + 1) * kNoteH + kVelH;
    const int gridW = juce::jmax (vp.getWidth(), (int) (clipLenBeats() * ppb) + 100);
    grid->setSize (gridW, gridH);
    keys->setBounds (0, -vp.getViewPositionY(), 52, gridH);
}

void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    if (! clip.isValid())
    {
        g.setColour (col::dim);
        g.drawText ("double-click a MIDI clip in the timeline to edit it",
                    getLocalBounds(), juce::Justification::centred);
    }
}

} // namespace dg
