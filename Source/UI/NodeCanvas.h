#pragma once
#include "Look.h"

namespace dg
{

// The shared zoomable node surface — "one graph, three altitudes" (NODES.md).
// The canvas owns navigation (ctrl-wheel/pinch zoom about the cursor, wheel
// pans, dragging empty space pans), the dot grid, and the cable bezier shape;
// a Delegate supplies content and receives the interactions that mean
// something to its model. Views own their box components as ordinary
// children of the canvas. WIRES adopts this first; the PATCH bay and
// PATCHER-mode are next — same surface, different delegates.
class NodeCanvas : public juce::Component
{
public:
    struct Delegate
    {
        virtual ~Delegate() = default;
        virtual void paintCables (juce::Graphics&) {}            // canvas coords, over children
        virtual void canvasDoubleClicked (juce::Point<int>) {}   // empty-space double-click
        virtual void canvasMouseUp (juce::Point<float>) {}       // e.g. drop a dragged cable
        virtual void canvasPopup (juce::Point<float>) {}         // right-click on the surface
        virtual bool canvasClicked (juce::Point<float>) { return false; }  // true = consumed, don't pan
    };

    explicit NodeCanvas (Delegate& d);

    void applyView();                                  // push zoom+pan into the transform
    void zoomAbout (juce::Point<float> canvasPt, float newZoom);

    static juce::Path cablePath (juce::Point<float> a, juce::Point<float> b);

    float zoom = 1.0f;
    juce::Point<int> pan;

private:
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    Delegate& delegate;
    juce::Point<int> panAtDragStart;
    bool panning = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodeCanvas)
};

} // namespace dg
