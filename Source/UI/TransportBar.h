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

    juce::TextButton rtzBtn { "|<" }, playBtn { ">" }, stopBtn { "[]" }, recBtn { "REC" };
    juce::TextButton loopBtn { "LOOP" }, punchBtn { "PUNCH" }, metroBtn { "CLICK" }, dubBtn { "OVERDUB" };
    juce::Label posBars, posTime, bpmLabel, cpuLabel;
    juce::ComboBox snapBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

} // namespace dg
