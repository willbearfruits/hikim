#pragma once
#include "../Engine/AudioEngine.h"
#include "UIState.h"
#include "Look.h"

namespace dg
{

// The Ableton layer: tracks as columns, scenes as rows, every cell a looping
// clip slot. Click launches quantized to the launch grid; scene buttons fire
// whole rows; clips loop in sync until stopped. Tab flips Session <-> Arrange.
// EXTEND: record into empty slots + capture session jam to the arrangement (Phase C).
class SessionGrid : public juce::Component,
                    public juce::FileDragAndDropTarget,
                    private juce::Timer
{
public:
    SessionGrid (AudioEngine&, SessionModel&, UIState&);
    ~SessionGrid() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

private:
    struct Cell { int col = -1, row = -1; bool sceneBtn = false, stopBtn = false, header = false; };

    std::vector<ValueTree> gridTracks() const;        // audio + midi only
    Cell cellAt (juce::Point<int>) const;
    juce::Rectangle<int> cellRect (int col, int row) const;
    ValueTree clipFor (int col, int row) const;
    ValueTree createMidiSlotClip (ValueTree track, const String& sceneUid);
    void launchScene (int row);
    void showCellMenu (int col, int row);
    void timerCallback() override { repaint(); }

    AudioEngine& engine;
    SessionModel& session;
    UIState& ui;

    juce::ComboBox quantBox;
    juce::TextButton stopAllBtn { "STOP ALL" }, addSceneBtn { "+ SCENE" };
    int scrollX = 0, scrollY = 0;
    juce::Point<int> hover { -1, -1 };

    static constexpr int kSceneW = 96, kHeadH = 46, kColW = 150, kRowH = 38, kBarH = 30;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionGrid)
};

} // namespace dg
