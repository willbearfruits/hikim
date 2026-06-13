#include "ChainPanel.h"

namespace dg
{

class ChainPanel::DeviceBox : public juce::Component
{
public:
    DeviceBox (ChainPanel& cp, ValueTree trackIn, ValueTree insertIn, int slotIn, int slotCount)
        : panel (cp), track (trackIn), insert (insertIn), slot (slotIn), slots (slotCount)
    {
        const String type = insert[id::type];
        isInstrument = type == "instrument";

        power.setButtonText ({});
        power.setClickingTogglesState (true);
        power.setToggleState (! (bool) insert[id::bypass], juce::dontSendNotification);
        power.onClick = [this] { insert.setProperty (id::bypass, ! power.getToggleState(), nullptr); };
        power.setColour (juce::TextButton::buttonOnColourId, col::play.darker (0.2f));
        if (! isInstrument) addAndMakeVisible (power);

        name.setText (insert[id::name].toString().isEmpty()
                          ? (type == "rack" ? String (names::rackName) : type.toUpperCase())
                          : insert[id::name].toString(),
                      juce::dontSendNotification);
        name.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        name.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (name);

        editBtn.onClick = [this]
        {
            if (panel.ui.openInsertEditor)
                panel.ui.openInsertEditor (track[id::uid].toString(), insert[id::uid].toString());
        };
        addAndMakeVisible (editBtn);

        left.setButtonText ("<"); right.setButtonText (">"); removeBtn.setButtonText ("x");
        auto move = [this] (int delta)
        {
            auto inserts = SessionModel::insertsOf (track);
            const int idx = inserts.indexOf (insert);
            inserts.moveChild (idx, idx + delta, &panel.session.undo);
        };
        left.onClick = [move] { move (-1); };
        right.onClick = [move] { move (1); };
        removeBtn.onClick = [this]
        {
            SessionModel::insertsOf (track).removeChild (insert, &panel.session.undo);
        };
        left.setEnabled (slot > 0 && ! isInstrument);
        right.setEnabled (slot < slots - 1 && ! isInstrument);
        if (! isInstrument)
        {
            addAndMakeVisible (left);
            addAndMakeVisible (right);
        }
        addAndMakeVisible (removeBtn);

        // TEETH boxes get live macro knobs
        if (type == "rack")
            if (auto* rack = dynamic_cast<RackProcessor*> (panel.engine.getInsertProcessor (insert[id::uid].toString())))
                for (int i = 0; i < 4; ++i)
                {
                    auto* k = macros.add (new juce::Slider());
                    k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                    k->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                    addAndMakeVisible (k);
                    macroAtts.add (new juce::SliderParameterAttachment (
                        *rack->apvts.getParameter ("macro" + String (i + 1)), *k));
                }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.5f);
        g.setColour (col::panelHi);
        g.fillRoundedRectangle (r, 4.0f);
        const bool on = isInstrument || ! (bool) insert[id::bypass];
        g.setColour (insert[id::type].toString() == "rack" && on ? col::accent
                     : on ? col::line.brighter (0.4f) : col::line);
        g.drawRoundedRectangle (r, 4.0f, on ? 1.4f : 1.0f);
        if (isInstrument)
        {
            g.setColour (col::dim);
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            g.drawText ("INSTRUMENT", getLocalBounds().reduced (4).removeFromBottom (10),
                        juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (6);
        auto top = b.removeFromTop (18);
        if (! isInstrument) power.setBounds (top.removeFromLeft (18));
        removeBtn.setBounds (top.removeFromRight (18));
        name.setBounds (top);

        auto bottom = b.removeFromBottom (18);
        if (! isInstrument)
        {
            left.setBounds (bottom.removeFromLeft (20));
            right.setBounds (bottom.removeFromRight (20));
        }
        editBtn.setBounds (bottom.reduced (2, 0));

        if (! macros.isEmpty())
        {
            const int kw = b.getWidth() / 4;
            for (auto* k : macros)
                k->setBounds (b.removeFromLeft (kw).reduced (2));
        }
    }

    ValueTree insert;

private:
    ChainPanel& panel;
    ValueTree track;
    int slot, slots;
    bool isInstrument = false;
    juce::ToggleButton power;
    juce::Label name;
    juce::TextButton editBtn { "EDIT" }, left, right, removeBtn;
    juce::OwnedArray<juce::Slider> macros;
    juce::OwnedArray<juce::SliderParameterAttachment> macroAtts;
};

// ---------------------------------------------------------------------------

ChainPanel::ChainPanel (AudioEngine& e, SessionModel& s, UIState& u)
    : engine (e), session (s), ui (u)
{
    vp.setViewedComponent (&content, false);
    vp.setScrollBarsShown (false, true);
    addAndMakeVisible (vp);

    trackLabel.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    trackLabel.setColour (juce::Label::textColourId, col::accent2);
    addAndMakeVisible (trackLabel);

    addBtn.onClick = [this]
    {
        auto track = session.findTrack (ui.selectedTrack);
        if (track.isValid() && showFxMenu)
            showFxMenu (track, &addBtn);
    };
    content.addAndMakeVisible (addBtn);

    startTimerHz (8);
    rebuild();
}

ChainPanel::~ChainPanel() = default;

void ChainPanel::timerCallback()
{
    auto track = session.findTrack (ui.selectedTrack);
    const int count = track.isValid() ? SessionModel::insertsOf (track).getNumChildren() : -1;
    if (ui.selectedTrack != shownTrack || count != shownInsertCount)
        rebuild();
}

void ChainPanel::rebuild()
{
    boxes.clear();
    shownTrack = ui.selectedTrack;
    auto track = session.findTrack (shownTrack);
    if (! track.isValid())
    {
        shownInsertCount = -1;
        trackLabel.setText ("select a track to see its chain", juce::dontSendNotification);
        resized();
        return;
    }
    trackLabel.setText ("DEVICES: " + track[id::name].toString(), juce::dontSendNotification);

    auto inserts = SessionModel::insertsOf (track);
    shownInsertCount = inserts.getNumChildren();

    // instrument first (the way signal flows), then fx in chain order
    std::vector<ValueTree> ordered;
    for (auto ins : inserts)
        if (ins[id::type].toString() == "instrument") ordered.push_back (ins);
    for (auto ins : inserts)
        if (ins[id::type].toString() != "instrument") ordered.push_back (ins);

    int fxSlots = 0;
    for (const auto& ins : ordered)
        if (ins[id::type].toString() != "instrument") ++fxSlots;
    int fxSlot = 0;
    for (const auto& ins : ordered)
    {
        const bool isInst = ins[id::type].toString() == "instrument";
        auto* box = boxes.add (new DeviceBox (*this, track, ins, isInst ? 0 : fxSlot, fxSlots));
        if (! isInst) ++fxSlot;
        content.addAndMakeVisible (box);
    }
    resized();
}

void ChainPanel::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
}

void ChainPanel::resized()
{
    auto b = getLocalBounds().reduced (6);
    trackLabel.setBounds (b.removeFromTop (18));
    b.removeFromTop (2);

    const int boxW = 190;
    const int h = juce::jmax (40, b.getHeight() - 10);
    content.setSize (boxes.size() * (boxW + 6) + 56, h);
    int x = 0;
    for (auto* box : boxes)
    {
        box->setBounds (x, 0, boxW, h);
        x += boxW + 6;
    }
    addBtn.setBounds (x + 4, h / 2 - 14, 36, 28);
    vp.setBounds (b);
}

} // namespace dg
