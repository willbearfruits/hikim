#pragma once
#include "RackProcessor.h"

namespace dg
{

class RackEditor : public juce::AudioProcessorEditor
{
public:
    explicit RackEditor (RackProcessor&);
    ~RackEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void rebuildModulePanels();

private:
    class ModulePanel;
    class StepEditor;

    RackProcessor& rack;

    juce::Label title;
    juce::ComboBox gestureBox;
    juce::TextButton saveBtn { "SAVE" }, loadBtn { "LOAD" };
    juce::Slider macroKnobs[4];
    juce::Label macroLabels[4];
    std::unique_ptr<juce::SliderParameterAttachment> macroAtt[4];

    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<ModulePanel> panels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackEditor)
};

} // namespace dg
