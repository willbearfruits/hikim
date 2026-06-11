#include "PatchView.h"

namespace dg
{

static const char* kSrcIds[] = { "lfo1", "lfo2", "lfo3", "lfo4", "chaos", "follower" };

String PatchView::srcName (int idx)
{
    static const char* names[] = { "LFO 1", "LFO 2", "LFO 3", "LFO 4", "CHAOS", "FOLLOW" };
    return names[juce::jlimit (0, 5, idx)];
}

// =========================================================================== SourceNode
class PatchView::SourceNode : public juce::Component
{
public:
    SourceNode (PatchView& p, int idx) : pv (p), srcIdx (idx)
    {
        auto modsTree = pv.session.mods();

        if (srcIdx < 4)
        {
            rate.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            rate.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            rate.setRange (0.02, 30.0);
            rate.setSkewFactorFromMidPoint (2.0);
            rate.setValue ((double) modsTree.getProperty ("lfo" + String (srcIdx + 1) + "rate",
                                                          srcIdx == 0 ? 1.0 : srcIdx == 1 ? 0.5 : srcIdx == 2 ? 2.0 : 4.0),
                           juce::dontSendNotification);
            rate.onValueChange = [this]
            { pv.session.mods().setProperty ("lfo" + String (srcIdx + 1) + "rate", rate.getValue(), nullptr); };
            addAndMakeVisible (rate);

            shape.addItemList ({ "SIN", "SAW", "SQR", "S&H" }, 1);
            shape.setSelectedItemIndex ((int) modsTree.getProperty ("lfo" + String (srcIdx + 1) + "shape", 0),
                                        juce::dontSendNotification);
            shape.onChange = [this]
            { pv.session.mods().setProperty ("lfo" + String (srcIdx + 1) + "shape", shape.getSelectedItemIndex(), nullptr); };
            addAndMakeVisible (shape);
        }
        else if (srcIdx == 4)
        {
            rate.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            rate.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            rate.setRange (0.05, 8.0);
            rate.setValue ((double) modsTree.getProperty ("chaosRate", 1.0), juce::dontSendNotification);
            rate.onValueChange = [this] { pv.session.mods().setProperty ("chaosRate", rate.getValue(), nullptr); };
            addAndMakeVisible (rate);
        }
        else
        {
            trackBox.setTextWhenNothingSelected ("track...");
            int idNum = 1;
            for (const auto& t : pv.session.tracks())
            {
                const String type = t[id::type];
                if (type == "audio" || type == "midi" || type == "bus")
                {
                    trackBox.addItem (t[id::name].toString(), idNum);
                    uids.add (t[id::uid].toString());
                    ++idNum;
                }
            }
            const int cur = uids.indexOf (modsTree.getProperty ("followerTrack", "").toString());
            if (cur >= 0) trackBox.setSelectedItemIndex (cur, juce::dontSendNotification);
            trackBox.onChange = [this]
            {
                const int i = trackBox.getSelectedItemIndex();
                if (i >= 0 && i < uids.size())
                    pv.session.mods().setProperty ("followerTrack", uids[i], nullptr);
            };
            addAndMakeVisible (trackBox);
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (col::panelHi);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (col::line.brighter (0.3f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (srcName (srcIdx), 6, 3, getWidth() - 24, 14, juce::Justification::left);

        // output port, glowing with the live source value
        const float v = pv.engine.getModSourceValue (srcIdx);
        auto port = portBounds();
        g.setColour (col::accent.withAlpha (0.35f + 0.65f * std::abs (v)));
        g.fillEllipse (port);
        g.setColour (col::text);
        g.drawEllipse (port, 1.2f);
    }

    juce::Rectangle<float> portBounds() const
    {
        return { (float) getWidth() - 14.0f, (float) getHeight() * 0.5f - 6.0f, 12.0f, 12.0f };
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // start a cable from anywhere on the node
        pv.dragFromSrc = srcIdx;
        pv.dragPos = e.getEventRelativeTo (&pv).position;
        pv.repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        pv.dragPos = e.getEventRelativeTo (&pv).position;
        pv.repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        pv.endCableDrag (e.getEventRelativeTo (&pv).position);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (6).withTrimmedTop (16).withTrimmedRight (12);
        if (srcIdx < 4)
        {
            rate.setBounds (b.removeFromLeft (34));
            shape.setBounds (b.reduced (2, 6));
        }
        else if (srcIdx == 4)
            rate.setBounds (b.withSizeKeepingCentre (34, juce::jmin (34, b.getHeight())));
        else
            trackBox.setBounds (b.reduced (0, 8));
    }

    int srcIdx;

private:
    PatchView& pv;
    juce::Slider rate;
    juce::ComboBox shape;
    juce::ComboBox trackBox;
    juce::StringArray uids;
};

// =========================================================================== TargetNode
class PatchView::TargetNode : public juce::Component
{
public:
    TargetNode (PatchView& p, ValueTree t) : pv (p), target (t)
    {
        removeBtn.setButtonText ("x");
        removeBtn.onClick = [this]
        {
            auto modsTree = pv.session.mods();
            const String uid = target[id::uid].toString();
            for (int i = modsTree.getNumChildren(); --i >= 0;)
            {
                auto c = modsTree.getChild (i);
                if ((c.hasType (id::MOD) && c[id::target].toString() == uid)
                    || (c.hasType (id::MODTARGET) && c[id::uid].toString() == uid))
                    modsTree.removeChild (i, &pv.session.undo);
            }
        };
        addAndMakeVisible (removeBtn);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (col::panel.brighter (0.06f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (col::accent2.withAlpha (0.8f));
        g.drawRoundedRectangle (r, 4.0f, 1.2f);

        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawFittedText (target[id::name].toString(), getLocalBounds().reduced (16, 4),
                          juce::Justification::centredLeft, 2);

        // input port
        auto port = portBounds();
        g.setColour (col::accent2);
        g.fillEllipse (port);
        g.setColour (col::text);
        g.drawEllipse (port, 1.2f);

        // live value bar along the bottom
        if (auto* param = pv.engine.resolveParamTarget (target[id::track].toString(),
                                                        target[id::param].toString()))
        {
            g.setColour (col::accent.withAlpha (0.8f));
            g.fillRect (3.0f, (float) getHeight() - 5.0f,
                        ((float) getWidth() - 6.0f) * param->getValue(), 3.0f);
        }
    }

    juce::Rectangle<float> portBounds() const
    {
        return { 2.0f, (float) getHeight() * 0.5f - 6.0f, 12.0f, 12.0f };
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragger.startDraggingComponent (this, e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        dragger.dragComponent (this, e, nullptr);
        target.setProperty (id::x, getX(), nullptr);
        target.setProperty (id::y, getY(), nullptr);
        pv.repaint();
    }

    void resized() override
    {
        removeBtn.setBounds (getWidth() - 18, 2, 16, 14);
    }

    ValueTree target;

private:
    PatchView& pv;
    juce::TextButton removeBtn;
    juce::ComponentDragger dragger;
};

// =========================================================================== PatchView

PatchView::PatchView (AudioEngine& e, SessionModel& s, UIState& u)
    : engine (e), session (s), ui (u)
{
    addTargetBtn.onClick = [this] { addTargetMenu(); };
    addAndMakeVisible (addTargetBtn);

    amountSl.setSliderStyle (juce::Slider::LinearBar);
    amountSl.setRange (-1.0, 1.0);
    amountSl.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 16);
    amountSl.onValueChange = [this]
    {
        if (selectedMod.isValid())
            selectedMod.setProperty (id::amount, amountSl.getValue(), nullptr);
    };
    addChildComponent (amountSl);

    deleteCableBtn.onClick = [this]
    {
        if (selectedMod.isValid())
        {
            session.mods().removeChild (selectedMod, &session.undo);
            selectedMod = {};
            amountSl.setVisible (false);
            deleteCableBtn.setVisible (false);
            inspectorLabel.setVisible (false);
        }
    };
    addChildComponent (deleteCableBtn);

    inspectorLabel.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    inspectorLabel.setColour (juce::Label::textColourId, col::accent2);
    addChildComponent (inspectorLabel);

    startTimerHz (30);
    rebuild();
}

PatchView::~PatchView() = default;

void PatchView::rebuild()
{
    sources.clear();
    targets.clear();
    for (int i = 0; i < AudioEngine::kNumModSources; ++i)
    {
        auto* n = sources.add (new SourceNode (*this, i));
        addAndMakeVisible (n);
    }
    for (const auto& t : session.mods())
        if (t.hasType (id::MODTARGET))
        {
            auto* n = targets.add (new TargetNode (*this, t));
            addAndMakeVisible (n);
        }
    resized();
}

void PatchView::resized()
{
    addTargetBtn.setBounds (getWidth() - 110, 6, 100, 24);
    inspectorLabel.setBounds (8, getHeight() - 26, 160, 20);
    amountSl.setBounds (170, getHeight() - 26, 220, 20);
    deleteCableBtn.setBounds (400, getHeight() - 26, 90, 20);

    const int srcH = juce::jmax (44, (getHeight() - 16) / juce::jmax (1, sources.size()));
    int y = 4;
    for (auto* sn : sources)
    {
        sn->setBounds (4, y, 150, juce::jmin (srcH - 4, 64));
        y += juce::jmin (srcH, 68);
    }

    for (auto* tn : targets)
    {
        const int tx = (int) tn->target.getProperty (id::x, 260 + (targets.indexOf (tn) % 3) * 190);
        const int ty = (int) tn->target.getProperty (id::y, 30 + (targets.indexOf (tn) / 3) * 70);
        tn->setBounds (juce::jlimit (160, juce::jmax (161, getWidth() - 150), tx),
                       juce::jlimit (0, juce::jmax (1, getHeight() - 60), ty), 170, 52);
    }
}

juce::Point<float> PatchView::sourcePortPos (int srcIdx) const
{
    for (auto* sn : sources)
        if (sn->srcIdx == srcIdx)
            return sn->getBounds().toFloat().getTopLeft() + sn->portBounds().getCentre();
    return {};
}

juce::Point<float> PatchView::targetPortPos (const String& targetUid) const
{
    for (auto* tn : targets)
        if (tn->target[id::uid].toString() == targetUid)
            return tn->getBounds().toFloat().getTopLeft() + tn->portBounds().getCentre();
    return {};
}

static juce::Path cablePath (juce::Point<float> a, juce::Point<float> b)
{
    juce::Path p;
    p.startNewSubPath (a);
    const float dx = juce::jmax (40.0f, std::abs (b.x - a.x) * 0.5f);
    p.cubicTo (a.translated (dx, 0), b.translated (-dx, 0), b);
    return p;
}

void PatchView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::dim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("drag from a source onto a target box - click a cable to edit - right-click a cable to cut",
                getLocalBounds().removeFromTop (24).reduced (8, 0), juce::Justification::centredLeft);
}

void PatchView::paintOverChildren (juce::Graphics& g)
{
    for (const auto& m : session.mods())
    {
        if (! m.hasType (id::MOD)) continue;
        const String src = m[id::src].toString();
        int srcIdx = 5;
        for (int i = 0; i < 6; ++i)
            if (src == kSrcIds[i]) srcIdx = i;

        const auto a = sourcePortPos (srcIdx);
        const auto b = targetPortPos (m[id::target].toString());
        if (a.isOrigin() || b.isOrigin()) continue;

        const float amount = (float) (double) m.getProperty (id::amount, 0.5);
        const bool selected = m == selectedMod;
        const float live = std::abs (engine.getModSourceValue (srcIdx)) * std::abs (amount);

        g.setColour ((amount >= 0 ? col::accent : col::clipMidi)
                         .withAlpha (0.45f + 0.55f * live)
                         .brighter (selected ? 0.4f : 0.0f));
        g.strokePath (cablePath (a, b), juce::PathStrokeType (selected ? 3.0f : 1.6f + 1.4f * std::abs (amount)));
    }

    if (dragFromSrc >= 0)
    {
        g.setColour (col::text.withAlpha (0.8f));
        g.strokePath (cablePath (sourcePortPos (dragFromSrc), dragPos), juce::PathStrokeType (1.6f));
    }
}

ValueTree PatchView::modAt (juce::Point<float> p) const
{
    for (const auto& m : session.mods())
    {
        if (! m.hasType (id::MOD)) continue;
        const String src = m[id::src].toString();
        int srcIdx = 5;
        for (int i = 0; i < 6; ++i)
            if (src == kSrcIds[i]) srcIdx = i;
        auto path = cablePath (sourcePortPos (srcIdx), targetPortPos (m[id::target].toString()));
        juce::Point<float> nearest;
        path.getNearestPoint (p, nearest);
        if (nearest.getDistanceFrom (p) < 8.0f)
            return m;
    }
    return {};
}

void PatchView::mouseDown (const juce::MouseEvent& e)
{
    auto m = modAt (e.position);
    if (m.isValid())
    {
        if (e.mods.isPopupMenu())
        {
            session.mods().removeChild (m, &session.undo);
            if (m == selectedMod) selectedMod = {};
            return;
        }
        selectedMod = m;
        int srcIdx = 5;
        for (int i = 0; i < 6; ++i)
            if (m[id::src].toString() == kSrcIds[i]) srcIdx = i;
        auto t = session.mods().getChildWithProperty (id::uid, m[id::target]);
        inspectorLabel.setText (srcName (srcIdx) + " -> " + t[id::name].toString(), juce::dontSendNotification);
        amountSl.setValue ((double) m.getProperty (id::amount, 0.5), juce::dontSendNotification);
        inspectorLabel.setVisible (true);
        amountSl.setVisible (true);
        deleteCableBtn.setVisible (true);
        repaint();
        return;
    }
    selectedMod = {};
    inspectorLabel.setVisible (false);
    amountSl.setVisible (false);
    deleteCableBtn.setVisible (false);
    repaint();
}

void PatchView::mouseDrag (const juce::MouseEvent&) {}

void PatchView::mouseUp (const juce::MouseEvent& e)
{
    if (dragFromSrc >= 0)
        endCableDrag (e.position);
}

void PatchView::endCableDrag (juce::Point<float> p)
{
    const int src = dragFromSrc;
    dragFromSrc = -1;
    if (src < 0) { repaint(); return; }

    for (auto* tn : targets)
        if (tn->getBounds().toFloat().contains (p))
        {
            session.undo.beginNewTransaction ("patch cable");
            ValueTree m (id::MOD);
            m.setProperty (id::uid, SessionModel::newUID(), nullptr);
            m.setProperty (id::src, kSrcIds[src], nullptr);
            m.setProperty (id::target, tn->target[id::uid].toString(), nullptr);
            m.setProperty (id::amount, 0.5, nullptr);
            session.mods().appendChild (m, &session.undo);
            break;
        }
    repaint();
}

void PatchView::addTargetMenu()
{
    juce::PopupMenu menu;
    int nextId = 1;
    juce::StringArray entries;          // "trackUid\ntarget\nlabel"

    for (const auto& track : session.tracks())
    {
        const String type = track[id::type];
        if (type == "video") continue;
        juce::PopupMenu sub;
        const String tuid = track[id::uid];
        auto add = [&] (const String& label, const String& target)
        {
            sub.addItem (nextId++, label);
            entries.add (tuid + "\n" + target + "\n" + track[id::name].toString() + ": " + label);
        };
        add ("Volume", "strip:gain");
        add ("Pan", "strip:pan");
        if (type == "audio" || type == "midi")
        {
            add ("Send A", "send:A");
            add ("Send B", "send:B");
        }
        for (const auto& ins : SessionModel::insertsOf (track))
        {
            if (auto* proc = engine.getInsertProcessor (ins[id::uid].toString()))
            {
                juce::PopupMenu insSub;
                const auto& params = proc->getParameters();
                const int count = juce::jmin (128, params.size());
                for (int i = 0; i < count; ++i)
                {
                    insSub.addItem (nextId++, params[i]->getName (40));
                    entries.add (tuid + "\nins:" + ins[id::uid].toString() + ":" + String (i)
                                 + "\n" + ins[id::name].toString() + ": " + params[i]->getName (24));
                }
                sub.addSubMenu (ins[id::name].toString(), insSub);
            }
        }
        menu.addSubMenu (track[id::name].toString(), sub);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addTargetBtn),
                        [this, entries] (int r)
    {
        if (r <= 0 || r > entries.size()) return;
        auto parts = juce::StringArray::fromLines (entries[r - 1]);
        session.undo.beginNewTransaction ("add mod target");
        ValueTree t (id::MODTARGET);
        t.setProperty (id::uid, SessionModel::newUID(), nullptr);
        t.setProperty (id::track, parts[0], nullptr);
        t.setProperty (id::param, parts[1], nullptr);
        t.setProperty (id::name, parts[2], nullptr);
        session.mods().appendChild (t, &session.undo);
        rebuild();
    });
}

} // namespace dg
