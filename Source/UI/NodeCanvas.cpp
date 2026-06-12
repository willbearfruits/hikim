#include "NodeCanvas.h"

namespace dg
{

NodeCanvas::NodeCanvas (Delegate& d) : delegate (d)
{
    setSize (8000, 8000);
    setWantsKeyboardFocus (false);
}

void NodeCanvas::applyView()
{
    setTransform (juce::AffineTransform::scale (zoom));
    setTopLeftPosition (pan);
    if (auto* p = getParentComponent())
        p->repaint();
}

void NodeCanvas::zoomAbout (juce::Point<float> canvasPt, float newZoom)
{
    newZoom = juce::jlimit (0.35f, 2.5f, newZoom);
    const auto inParent = pan.toFloat() + canvasPt * zoom;     // anchor stays put
    zoom = newZoom;
    pan = (inParent - canvasPt * zoom).toInt();
    applyView();
}

juce::Path NodeCanvas::cablePath (juce::Point<float> a, juce::Point<float> b)
{
    juce::Path p;
    p.startNewSubPath (a);
    const float dy = juce::jmax (30.0f, std::abs (b.y - a.y) * 0.5f);
    p.cubicTo (a.translated (0, dy), b.translated (0, -dy), b);
    return p;
}

void NodeCanvas::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::line.withAlpha (0.25f));
    for (int x = 0; x < getWidth(); x += 40)        // quiet dot grid
        for (int y = 0; y < getHeight(); y += 40)
            g.fillRect (x, y, 1, 1);
}

void NodeCanvas::paintOverChildren (juce::Graphics& g)
{
    delegate.paintCables (g);
}

void NodeCanvas::mouseDown (const juce::MouseEvent& e)
{
    panning = false;
    if (e.mods.isPopupMenu())
    {
        delegate.canvasPopup (e.position);
        repaint();
        return;
    }
    if (delegate.canvasClicked (e.position))    // content under the click ate it
        return;
    panning = true;                             // drag empty canvas to pan
    panAtDragStart = pan;
}

void NodeCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (! panning) return;
    pan = panAtDragStart + (e.getOffsetFromDragStart().toFloat() * zoom).toInt();
    applyView();
}

void NodeCanvas::mouseUp (const juce::MouseEvent& e)
{
    panning = false;
    delegate.canvasMouseUp (e.position);
}

void NodeCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    delegate.canvasDoubleClicked (e.getPosition());
}

void NodeCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        zoomAbout (e.position, zoom * (1.0f + w.deltaY * 0.8f));
        return;
    }
    pan += juce::Point<int> ((int) ((e.mods.isShiftDown() ? w.deltaY : w.deltaX) * 240.0f),
                             (int) ((e.mods.isShiftDown() ? 0.0f : w.deltaY) * 240.0f));
    applyView();
}

void NodeCanvas::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    zoomAbout (e.position, zoom * scaleFactor);
}

} // namespace dg
