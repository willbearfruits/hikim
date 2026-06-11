#pragma once
#include "../Engine/AudioEngine.h"
#include "../Rack/RackProcessor.h"
#include "UIState.h"
#include "Look.h"

namespace dg
{

// Bitwig-style device chain for the selected track: one box per insert,
// inline power/edit/move/remove, TEETH boxes carry live macro knobs,
// "+" box appends devices.
class ChainPanel : public juce::Component, private juce::Timer
{
public:
    ChainPanel (AudioEngine&, SessionModel&, UIState&);
    ~ChainPanel() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void rebuild();

    std::function<void (ValueTree track, juce::Component* target)> showFxMenu;

    AudioEngine& engine;
    SessionModel& session;
    UIState& ui;

private:
    class DeviceBox;

    void timerCallback() override;

    juce::Viewport vp;
    juce::Component content;
    juce::OwnedArray<DeviceBox> boxes;
    juce::TextButton addBtn { "+" };
    juce::Label trackLabel;
    String shownTrack;
    int shownInsertCount = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainPanel)
};

} // namespace dg
