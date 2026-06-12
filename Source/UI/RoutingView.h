#pragma once
#include "../Engine/AudioEngine.h"
#include "UIState.h"
#include "Look.h"
#include "NodeCanvas.h"

namespace dg
{

// Mode three: the whole session as a patch. Every channel is a movable box
// (name, meter, its device chain as clickable chips); cables are the real
// routing - track outputs into buses/master, sends as thinner lines, and the
// mod sources wired in dashed. Drag from an output port onto a bus to rewire;
// drag from a mod source onto a channel to modulate one of its parameters.
// The whole picture lives on the shared NodeCanvas (third altitude): same
// ctrl-wheel zoom / drag-space pan as WIRES and the PATCH bay; routing edits
// stay session edits on the global undo stack.
// EXTEND: free-form audio cables between arbitrary devices (the engine graph
// already supports it; this canvas is where it will land).
class RoutingView : public juce::Component, private juce::Timer,
                    private NodeCanvas::Delegate
{
public:
    RoutingView (AudioEngine&, SessionModel&, UIState&);
    ~RoutingView() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;

    std::function<void (ValueTree track, juce::Component* target)> showFxMenu;

private:
    enum class Drag { none, moveBox, out, sendA, sendB, mod };

    struct Box
    {
        ValueTree track;
        juce::Rectangle<int> r;
        std::vector<ValueTree> chips;        // inserts in chain order (instrument first)
    };

    std::vector<Box> layoutBoxes();          // computes rects from track x/y (+ defaults)
    juce::Point<float> outPort (const Box&) const;
    juce::Point<float> sendPort (const Box&, int which) const;
    juce::Point<float> inPort (const Box&) const;
    juce::Rectangle<int> modBox (int idx) const;
    const Box* boxAt (const std::vector<Box>&, juce::Point<float>) const;
    void openModTargetMenu (int srcIdx, ValueTree track);
    bool handlePress (juce::Point<float> p, bool popup);
    void timerCallback() override { canvas->repaint(); repaint(); }

    // NodeCanvas::Delegate (the session-as-a-patch content; canvas coords)
    void paintCables (juce::Graphics&) override;
    bool canvasClicked (juce::Point<float>) override;
    void canvasPopup (juce::Point<float>) override;
    void canvasDragged (juce::Point<float>) override;
    void canvasMouseUp (juce::Point<float>) override;
    void canvasDoubleClicked (juce::Point<int>) override;
    void canvasMoved (juce::Point<float>) override;

    std::unique_ptr<NodeCanvas> canvas;

    AudioEngine& engine;
    SessionModel& session;
    UIState& ui;

    Drag drag = Drag::none;
    String dragTrack;
    int dragMod = -1;
    juce::Point<float> dragPos;
    juce::Point<int> moveOffset;
    ValueTree movingTrack;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoutingView)
};

} // namespace dg
