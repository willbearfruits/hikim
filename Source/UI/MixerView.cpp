#include "MixerView.h"

namespace dg
{

class MixerView::Strip : public juce::Component
{
public:
    Strip (MixerView& m, ValueTree t) : mv (m), track (t)
    {
        const String type = track[id::type];
        const String uid = track[id::uid];
        isChannel = type == "audio" || type == "midi";

        name.setText (track[id::name].toString(), juce::dontSendNotification);
        name.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        name.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (name);

        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 14);
        addAndMakeVisible (fader);
        pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible (pan);

        if (auto* strip = mv.engine.getStrip (uid))
        {
            faderAtt = std::make_unique<juce::SliderParameterAttachment> (*strip->gainDb, fader);
            panAtt = std::make_unique<juce::SliderParameterAttachment> (*strip->pan, pan);
        }

        auto setupToggle = [this] (juce::TextButton& b, const Identifier& prop, juce::Colour on)
        {
            b.setClickingTogglesState (true);
            b.setToggleState ((bool) track[prop], juce::dontSendNotification);
            b.setColour (juce::TextButton::buttonOnColourId, on);
            b.onClick = [this, &b, prop] { track.setProperty (prop, b.getToggleState(), nullptr); };
            addAndMakeVisible (b);
        };
        if (type != "master") setupToggle (muteBtn, id::mute, col::accent2);
        if (isChannel)
        {
            setupToggle (soloBtn, id::solo, col::play);
            setupToggle (armBtn, id::armed, col::record);

            for (int s = 0; s < 2; ++s)
            {
                sendKnob[s].setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                sendKnob[s].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                addAndMakeVisible (sendKnob[s]);
                if (auto* sp = mv.engine.getSend (uid, s))
                    sendAtt[s] = std::make_unique<juce::SliderParameterAttachment> (*sp->levelDb, sendKnob[s]);

                rebuildBusBox (sendBox[s], s == 0 ? id::sendABus : id::sendBBus, true);
                addAndMakeVisible (sendBox[s]);
            }
            rebuildBusBox (outBox, id::outputBus, false);
            addAndMakeVisible (outBox);

            fxBtn.onClick = [this] { if (mv.showFxMenu) mv.showFxMenu (track, &fxBtn); };
            addAndMakeVisible (fxBtn);
        }
    }

    void rebuildBusBox (juce::ComboBox& box, const Identifier& prop, bool allowNone)
    {
        box.clear (juce::dontSendNotification);
        juce::StringArray uids;
        int idNum = 1;
        if (allowNone) { box.addItem ("-", idNum++); uids.add (""); }
        box.addItem ("MASTER", idNum++); uids.add ("master");
        for (const auto& t : mv.session.tracks())
            if (t[id::type].toString() == "bus")
            {
                box.addItem (t[id::name].toString(), idNum++);
                uids.add (t[id::uid].toString());
            }
        const String cur = track[prop].toString();
        const int curIdx = juce::jmax (0, uids.indexOf (cur));
        box.setSelectedItemIndex (curIdx, juce::dontSendNotification);
        box.onChange = [this, &box, prop, uids]
        {
            const int i = box.getSelectedItemIndex();
            if (i >= 0 && i < uids.size())
                track.setProperty (prop, uids[i], nullptr);
        };
    }

    void updateMeter()
    {
        if (auto* strip = mv.engine.getStrip (track[id::uid].toString()))
        {
            const float decay = 0.86f;
            meterL = juce::jmax (meterL * decay, strip->peakL.load());
            meterR = juce::jmax (meterR * decay, strip->peakR.load());
            repaint (meterArea);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (track[id::type].toString() == "master" ? col::panelHi : col::panel);
        g.setColour (juce::Colour::fromString (track.getProperty (id::colour, "ff808080").toString()));
        g.fillRect (0, 0, getWidth(), 3);
        g.setColour (col::line);
        g.drawRect (getLocalBounds());

        // meters
        auto drawMeter = [&g] (juce::Rectangle<int> r, float v)
        {
            g.setColour (col::bg);
            g.fillRect (r);
            const float db = juce::Decibels::gainToDecibels (v, -60.0f);
            const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
            auto fill = r.removeFromBottom ((int) (frac * (float) r.getHeight()));
            g.setColour (db > 0 ? col::record : db > -12 ? col::accent2 : col::play);
            g.fillRect (fill);
        };
        auto m = meterArea;
        drawMeter (m.removeFromLeft (m.getWidth() / 2 - 1), meterL);
        m.removeFromLeft (2);
        drawMeter (m, meterR);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (4);
        name.setBounds (b.removeFromTop (16));

        if (isChannel)
        {
            auto btns = b.removeFromTop (18);
            const int bw = btns.getWidth() / 4;
            muteBtn.setBounds (btns.removeFromLeft (bw).reduced (1, 0));
            soloBtn.setBounds (btns.removeFromLeft (bw).reduced (1, 0));
            armBtn.setBounds (btns.removeFromLeft (bw).reduced (1, 0));
            fxBtn.setBounds (btns.reduced (1, 0));

            auto sends = b.removeFromTop (40);
            for (int s = 0; s < 2; ++s)
            {
                auto half = s == 0 ? sends.removeFromLeft (sends.getWidth() / 2) : sends;
                sendKnob[s].setBounds (half.removeFromTop (24).withSizeKeepingCentre (24, 24));
                sendBox[s].setBounds (half.reduced (1, 0));
            }
            outBox.setBounds (b.removeFromBottom (18));
        }
        else if (muteBtn.getParentComponent() == this)
            muteBtn.setBounds (b.removeFromTop (18).reduced (8, 0));

        pan.setBounds (b.removeFromTop (30).withSizeKeepingCentre (30, 30));
        meterArea = b.removeFromRight (12).reduced (0, 4);
        fader.setBounds (b);
    }

    ValueTree track;
    juce::Rectangle<int> meterArea;

private:
    MixerView& mv;
    bool isChannel = false;
    juce::Label name;
    juce::Slider fader, pan, sendKnob[2];
    std::unique_ptr<juce::SliderParameterAttachment> faderAtt, panAtt, sendAtt[2];
    juce::TextButton muteBtn { "M" }, soloBtn { "S" }, armBtn { "R" }, fxBtn { "FX" };
    juce::ComboBox sendBox[2], outBox;
    float meterL = 0, meterR = 0;
};

// ---------------------------------------------------------------------------

MixerView::MixerView (AudioEngine& e, SessionModel& s, UIState& u)
    : engine (e), session (s), ui (u)
{
    vp.setViewedComponent (&content, false);
    vp.setScrollBarsShown (false, true);
    addAndMakeVisible (vp);
    session.root.addListener (this);
    startTimerHz (24);
    rebuild();
}

MixerView::~MixerView()
{
    session.root.removeListener (this);
}

void MixerView::rebuild()
{
    strips.clear();
    for (auto t : session.tracks())
    {
        if (t[id::type].toString() == "video") continue;
        auto* st = strips.add (new Strip (*this, t));
        content.addAndMakeVisible (st);
    }
    resized();
}

void MixerView::timerCallback()
{
    if (pending) { pending = false; rebuild(); }
    for (auto* s : strips)
        s->updateMeter();
}

void MixerView::resized()
{
    vp.setBounds (getLocalBounds());
    const int stripW = 110;
    const int h = juce::jmax (100, getHeight() - (vp.isHorizontalScrollBarShown() ? vp.getScrollBarThickness() : 0));
    content.setSize (juce::jmax (strips.size() * stripW, 1), h);
    int x = 0;
    for (auto* s : strips)
    {
        s->setBounds (x, 0, stripW - 2, h);
        x += stripW;
    }
}

} // namespace dg
