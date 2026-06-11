#include "RackEditor.h"

namespace dg
{

namespace
{
    // rotary knob with right-click menu for macro assign / MIDI learn
    class RKnob : public juce::Slider
    {
    public:
        std::function<void()> onRightClick;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() && onRightClick != nullptr) { onRightClick(); return; }
            juce::Slider::mouseDown (e);
        }
    };
}

// ---------------------------------------------------------------------------
class RackEditor::StepEditor : public juce::Component
{
public:
    explicit StepEditor (RackProcessor& r) : rack (r) {}

    void paint (juce::Graphics& g) override
    {
        const int numSteps = juce::jlimit (2, 32, (int) rack.apvts.getRawParameterValue ("gs_steps")->load());
        const float w = (float) getWidth() / 32.0f;
        for (int i = 0; i < 32; ++i)
        {
            const float v = rack.gateSteps[(size_t) i].load();
            juce::Rectangle<float> bar (i * w + 1, (float) getHeight() * (1.0f - v), w - 2, (float) getHeight() * v);
            g.setColour (i < numSteps ? (i % 4 == 0 ? juce::Colour (0xffe04040) : juce::Colour (0xffb0b0b0))
                                      : juce::Colour (0xff3a3a3a));
            g.fillRect (bar);
        }
        g.setColour (juce::Colour (0xff505050));
        g.drawRect (getLocalBounds());
    }

    void edit (const juce::MouseEvent& e)
    {
        const int i = juce::jlimit (0, 31, (int) (e.x / ((float) getWidth() / 32.0f)));
        const float v = juce::jlimit (0.0f, 1.0f, 1.0f - (float) e.y / (float) getHeight());
        rack.gateSteps[(size_t) i].store (v);
        rack.markGateStepsDirty();
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override { edit (e); }
    void mouseDrag (const juce::MouseEvent& e) override { edit (e); }

private:
    RackProcessor& rack;
};

// ---------------------------------------------------------------------------
class RackEditor::ModulePanel : public juce::Component
{
public:
    ModulePanel (RackEditor& ed, RackProcessor& r, int moduleIdx, int slotIdx, int slotCount)
        : editor (ed), rack (r), modIdx (moduleIdx), slot (slotIdx), slots (slotCount)
    {
        const String prefix (RackProcessor::kModuleIds[modIdx]);

        power.setButtonText ({});
        power.setClickingTogglesState (true);
        addAndMakeVisible (power);
        powerAtt = std::make_unique<juce::ButtonParameterAttachment> (*rack.apvts.getParameter (prefix + "_on"), power);

        name.setText (RackProcessor::kModuleNames[modIdx], juce::dontSendNotification);
        name.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        addAndMakeVisible (name);

        up.setButtonText ("^"); down.setButtonText ("v");
        // rebuild deletes this panel (and the clicked button) - defer it
        auto deferredRebuild = [safe = juce::Component::SafePointer<RackEditor> (&ed)]
        {
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->rebuildModulePanels(); });
        };
        up.onClick = [this, deferredRebuild] { rack.moveModule (slot, slot - 1); deferredRebuild(); };
        down.onClick = [this, deferredRebuild] { rack.moveModule (slot, slot + 1); deferredRebuild(); };
        up.setEnabled (slot > 0); down.setEnabled (slot < slots - 1);
        addAndMakeVisible (up);
        addAndMakeVisible (down);

        addKnob (prefix + "_mix", "MIX");

        for (auto* p : rack.getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (rp == nullptr) continue;
            const String pid = rp->getParameterID();
            if (! pid.startsWith (prefix + "_") || pid.endsWith ("_on") || pid.endsWith ("_mix")) continue;

            if (auto* cp = dynamic_cast<juce::AudioParameterChoice*> (rp))
            {
                auto* box = combos.add (new juce::ComboBox());
                box->addItemList (cp->choices, 1);
                addAndMakeVisible (box);
                comboAtts.add (new juce::ComboBoxParameterAttachment (*cp, *box));
                auto* lab = labels.add (new juce::Label ({}, rp->getName (16)));
                lab->setFont (juce::Font (juce::FontOptions (10.0f)));
                lab->setJustificationType (juce::Justification::centred);
                addAndMakeVisible (lab);
                controlIsCombo.add (true);
                controlIdx.add (combos.size() - 1);
            }
            else if (auto* bp = dynamic_cast<juce::AudioParameterBool*> (rp))
            {
                auto* tb = toggles.add (new juce::ToggleButton (rp->getName (16)));
                addAndMakeVisible (tb);
                toggleAtts.add (new juce::ButtonParameterAttachment (*bp, *tb));
                controlIsCombo.add (false);
                controlIdx.add (-1000 - (toggles.size() - 1));
            }
            else
                addKnob (pid, rp->getName (16).toUpperCase());
        }

        if (prefix == "gs")
        {
            stepEd = std::make_unique<StepEditor> (rack);
            addAndMakeVisible (*stepEd);
        }
        if (prefix == "cj")
        {
            irBtn.setButtonText ("LOAD IR...");
            irBtn.onClick = [this]
            {
                juce::FileChooser fc ("Impulse / junk file", {}, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
                if (fc.browseForFileToOpen())
                    rack.setIRFile (fc.getResult(), (int) rack.apvts.getRawParameterValue ("cj_mangle")->load());
            };
            addAndMakeVisible (irBtn);
            irName.setFont (juce::Font (juce::FontOptions (11.0f)));
            irName.setText (rack.getIRFile().exists() ? rack.getIRFile().getFileName() : "(no IR loaded)",
                            juce::dontSendNotification);
            addAndMakeVisible (irName);
        }
    }

    void addKnob (const String& pid, const String& text)
    {
        auto* k = knobs.add (new RKnob());
        k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 14);
        addAndMakeVisible (k);
        if (auto* param = rack.apvts.getParameter (pid))
        {
            knobAtts.add (new juce::SliderParameterAttachment (*param, *k));
            k->onRightClick = [this, pid] { showParamMenu (pid); };
        }
        auto* lab = labels.add (new juce::Label ({}, text));
        lab->setFont (juce::Font (juce::FontOptions (10.0f)));
        lab->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (lab);
        controlIsCombo.add (false);
        controlIdx.add (knobs.size() - 1);
    }

    void showParamMenu (const String& pid)
    {
        juce::PopupMenu m;
        for (int i = 0; i < 4; ++i)
            m.addItem (1 + i, "Assign to Macro " + String (i + 1));
        m.addItem (5, "Clear macro assign");
        m.addSeparator();
        const String cc = rack.getCCMapDescription (pid);
        m.addItem (6, rack.isLearning() ? "MIDI learn (waiting...)" : "MIDI learn");
        m.addItem (7, "Clear MIDI map" + (cc.isEmpty() ? String() : " (" + cc + ")"), cc.isNotEmpty());

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, pid] (int r)
        {
            if (r >= 1 && r <= 4) rack.assignMacro (r - 1, pid);
            else if (r == 5) rack.clearMacro (pid);
            else if (r == 6) rack.armMidiLearn (pid);
            else if (r == 7) rack.clearMidiMap (pid);
        });
    }

    int idealHeight() const
    {
        const int rows = (knobs.size() + combos.size() + toggles.size() + 4) / 5;
        int h = 30 + rows * 78 + 8;
        if (stepEd != nullptr) h += 70;
        if (irBtn.isVisible() && irBtn.getParentComponent() == this) h += 34;
        return h;
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff181818));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
        g.setColour (power.getToggleState() ? juce::Colour (0xffe04040) : juce::Colour (0xff333333));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 4.0f, 1.2f);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (6);
        auto head = b.removeFromTop (24);
        power.setBounds (head.removeFromLeft (24));
        down.setBounds (head.removeFromRight (22).reduced (1));
        up.setBounds (head.removeFromRight (22).reduced (1));
        name.setBounds (head);

        // lay knobs/combos/toggles in rows of 5; label rides under each control
        int col = 0;
        auto row = b.removeFromTop (78);
        int knobI = 0, comboI = 0, toggleI = 0, labelI = 0;
        auto next = [&]() -> juce::Rectangle<int>
        {
            if (col == 5) { row = b.removeFromTop (78); col = 0; }
            return row.removeFromLeft (juce::jmax (1, getWidth() / 5 - 2)).reduced (2);
        };

        // mix knob first (index 0), then everything in creation order
        const int total = controlIdx.size();
        for (int i = 0; i < total; ++i)
        {
            auto cell = next(); ++col;
            if (controlIdx[i] <= -1000)
            {
                toggles[(-1000 - controlIdx[i])]->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 24));
                ++toggleI;
                continue;
            }
            auto labArea = cell.removeFromBottom (13);
            if (controlIsCombo[i])
                combos[comboI++]->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 22));
            else
                knobs[knobI++]->setBounds (cell);
            if (labelI < labels.size())
                labels[labelI++]->setBounds (labArea);
        }
        juce::ignoreUnused (knobI, comboI, toggleI);

        if (stepEd != nullptr)
            stepEd->setBounds (getLocalBounds().reduced (8).removeFromBottom (62));
        if (irBtn.getParentComponent() == this)
        {
            auto ir = getLocalBounds().reduced (8).removeFromBottom (28);
            irBtn.setBounds (ir.removeFromLeft (110));
            irName.setBounds (ir.reduced (6, 0));
        }
    }

private:
    RackEditor& editor;
    RackProcessor& rack;
    int modIdx, slot, slots;

    juce::ToggleButton power;
    juce::Label name;
    juce::TextButton up, down;
    std::unique_ptr<juce::ButtonParameterAttachment> powerAtt;

    juce::OwnedArray<RKnob> knobs;
    juce::OwnedArray<juce::SliderParameterAttachment> knobAtts;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::ComboBoxParameterAttachment> comboAtts;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<juce::ButtonParameterAttachment> toggleAtts;
    juce::OwnedArray<juce::Label> labels;
    juce::Array<bool> controlIsCombo;
    juce::Array<int> controlIdx;

    std::unique_ptr<StepEditor> stepEd;
    juce::TextButton irBtn;
    juce::Label irName;
};

// ---------------------------------------------------------------------------
RackEditor::RackEditor (RackProcessor& r) : juce::AudioProcessorEditor (r), rack (r)
{
    title.setText (names::rackName, juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    addAndMakeVisible (title);

    gestureBox.setTextWhenNothingSelected ("GESTURES");
    gestureBox.addItemList (RackProcessor::getFactoryGestureNames(), 1);
    gestureBox.onChange = [this]
    {
        const int idx = gestureBox.getSelectedItemIndex();
        if (idx >= 0) rack.applyFactoryGesture (idx);
        gestureBox.setSelectedId (0, juce::dontSendNotification);
    };
    addAndMakeVisible (gestureBox);

    saveBtn.onClick = [this]
    {
        juce::FileChooser fc ("Save rack", File::getSpecialLocation (File::userDocumentsDirectory), "*.rack");
        if (fc.browseForFileToSave (true))
            rack.saveUserRack (fc.getResult().withFileExtension (".rack"));
    };
    loadBtn.onClick = [this]
    {
        juce::FileChooser fc ("Load rack", File::getSpecialLocation (File::userDocumentsDirectory), "*.rack");
        if (fc.browseForFileToOpen() && rack.loadUserRack (fc.getResult()))
            rebuildModulePanels();
    };
    addAndMakeVisible (saveBtn);
    addAndMakeVisible (loadBtn);

    for (int i = 0; i < 4; ++i)
    {
        macroKnobs[i].setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        macroKnobs[i].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible (macroKnobs[i]);
        macroAtt[i] = std::make_unique<juce::SliderParameterAttachment> (
            *rack.apvts.getParameter ("macro" + String (i + 1)), macroKnobs[i]);
        macroLabels[i].setText ("M" + String (i + 1), juce::dontSendNotification);
        macroLabels[i].setJustificationType (juce::Justification::centred);
        macroLabels[i].setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        addAndMakeVisible (macroLabels[i]);
    }

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    setResizable (true, true);
    setResizeLimits (480, 400, 1200, 1600);
    setSize (600, 820);
    rebuildModulePanels();
}

RackEditor::~RackEditor() = default;

void RackEditor::rebuildModulePanels()
{
    panels.clear();
    auto order = rack.getOrder();
    for (int slot = 0; slot < order.size(); ++slot)
    {
        int modIdx = 0;
        for (int m = 0; m < RackProcessor::kNumModules; ++m)
            if (order[slot] == RackProcessor::kModuleIds[m]) modIdx = m;
        auto* p = panels.add (new ModulePanel (*this, rack, modIdx, slot, order.size()));
        content.addAndMakeVisible (p);
    }
    resized();
}

void RackEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0d0d0d));
}

void RackEditor::resized()
{
    auto b = getLocalBounds().reduced (8);
    auto head = b.removeFromTop (30);
    title.setBounds (head.removeFromLeft (220));
    loadBtn.setBounds (head.removeFromRight (60).reduced (2));
    saveBtn.setBounds (head.removeFromRight (60).reduced (2));
    gestureBox.setBounds (head.removeFromRight (180).reduced (2));

    auto macros = b.removeFromTop (76);
    const int mw = macros.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto cell = macros.removeFromLeft (mw).reduced (4);
        macroLabels[i].setBounds (cell.removeFromBottom (14));
        macroKnobs[i].setBounds (cell);
    }

    viewport.setBounds (b);
    const int w = b.getWidth() - viewport.getScrollBarThickness() - 2;
    int y = 0;
    for (auto* p : panels)
    {
        p->setBounds (0, y, w, p->idealHeight());
        y += p->idealHeight() + 6;
    }
    content.setSize (w, y);
}

} // namespace dg
