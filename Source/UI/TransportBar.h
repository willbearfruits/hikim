#pragma once
#include "../Engine/AudioEngine.h"
#include "UIState.h"
#include "Look.h"

namespace dg
{

class TransportBar : public juce::Component, private juce::Timer
{
public:
    TransportBar (AudioEngine&, SessionModel&, UIState&);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    void updateToggleStates();

    AudioEngine& engine;
    SessionModel& session;
    UIState& ui;

public:
    std::function<void()> onToggleView;
    std::function<void()> onHelp;
    void setViewLabel (const String& s) { viewBtn.setButtonText (s); }

private:
    juce::TextButton rtzBtn { "|<" }, playBtn { ">" }, stopBtn { "[]" }, recBtn { "REC" };
    juce::TextButton viewBtn { "SESSION" };
    juce::TextButton loopBtn { "LOOP" }, metroBtn { "CLICK" }, dubBtn { "OVERDUB" }, helpBtn { "?" };
    juce::Label posBars, posTime, bpmLabel, cpuLabel;
    juce::ComboBox snapBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

} // namespace dg
