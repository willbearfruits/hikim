#pragma once
#include "../Engine/AudioEngine.h"
#include "UIState.h"
#include "Look.h"

namespace dg
{

// Console view: fader/pan/mute/solo/arm, sends with bus routing, output
// routing, meters on every channel, master at the right.
class MixerView : public juce::Component,
                  public juce::ValueTree::Listener,
                  private juce::Timer
{
public:
    MixerView (AudioEngine&, SessionModel&, UIState&);
    ~MixerView() override;

    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (col::bg); }
    void rebuild();

    AudioEngine& engine;
    SessionModel& session;
    UIState& ui;
    std::function<void (ValueTree track, juce::Component* target)> showFxMenu;

private:
    class Strip;

    void timerCallback() override;
    void valueTreePropertyChanged (ValueTree&, const Identifier&) override {}
    void valueTreeChildAdded (ValueTree& p, ValueTree&) override   { if (p.hasType (id::TRACKS)) pending = true; }
    void valueTreeChildRemoved (ValueTree& p, ValueTree&, int) override { if (p.hasType (id::TRACKS)) pending = true; }
    void valueTreeChildOrderChanged (ValueTree& p, int, int) override   { if (p.hasType (id::TRACKS)) pending = true; }
    void valueTreeParentChanged (ValueTree&) override {}

    juce::Viewport vp;
    juce::Component content;
    juce::OwnedArray<Strip> strips;
    bool pending = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};

} // namespace dg
