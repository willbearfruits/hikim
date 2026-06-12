#pragma once
#include "../Engine/AudioEngine.h"
#include "UIState.h"
#include "Look.h"

namespace dg
{

class TransportBar : public juce::Component, private juce::Timer,
                     public juce::DragAndDropTarget
{
public:
    TransportBar (AudioEngine&, SessionModel&, UIState&);

    void resized() override;
    void paint (juce::Graphics&) override;

    // dragging a session slot over the transport flips to ARRANGE so the drop
    // can land on the timeline (the views are never visible together)
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return d.description.toString().startsWith ("slotclip:");
    }
    void itemDragEnter (const SourceDetails&) override { if (onSetView) onSetView (0); }
    void itemDropped (const SourceDetails&) override {}

private:
    void timerCallback() override;
    void updateToggleStates();

    AudioEngine& engine;
    SessionModel& session;
    UIState& ui;

public:
    std::function<void()> onToggleView;
    std::function<void(int)> onSetView;
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
