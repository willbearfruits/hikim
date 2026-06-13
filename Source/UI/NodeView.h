#pragma once
#include "../Engine/AudioEngine.h"
#include "../Patcher/PatcherProcessor.h"
#include "../Patcher/PatcherEditor.h"
#include "Look.h"

namespace dg
{

// The PATCHER view as one navigable canvas (NODES.md "three altitudes"): a
// breadcrumb bar over a swappable node editor. Root = the session graph;
// EDIT on a WIRES device dives into that device's patch, and the breadcrumb
// climbs back. Non-node devices (plugins, TEETH) still open their own window.
class NodeView : public juce::Component
{
public:
    explicit NodeView (AudioEngine& e) : engine (e) { showRoot(); }

    void showRoot()
    {
        divedInsert.clear();
        deviceName.clear();
        setEditorFor (engine.getSessionGraph());
    }

    // dive into a WIRES device's patch; returns false (and does nothing) if the
    // insert isn't a node device
    bool dive (const String& insertUid, const String& name)
    {
        auto* pp = dynamic_cast<PatcherProcessor*> (engine.getInsertProcessor (insertUid));
        if (pp == nullptr) return false;
        divedInsert = insertUid;
        deviceName = name.isEmpty() ? String (names::patcherName) : name;
        setEditorFor (pp);
        return true;
    }

    // engine fires before a device processor is destroyed: climb out first so
    // the editor dies before its processor
    void insertRemoved (const String& insertUid) { if (insertUid == divedInsert) showRoot(); }

    void paint (juce::Graphics& g) override
    {
        auto bar = getLocalBounds().removeFromTop (kBar);
        g.setColour (col::panel);
        g.fillRect (bar);
        g.setColour (col::line);
        g.drawHorizontalLine (kBar - 1, 0.0f, (float) getWidth());

        auto row = bar.reduced (8, 0);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        sessionRect = row.removeFromLeft (74);
        g.setColour (divedInsert.isEmpty() ? col::accent2 : col::accent);
        g.drawText ("SESSION", sessionRect, juce::Justification::centredLeft);
        if (! divedInsert.isEmpty())
        {
            g.setColour (col::dim);
            g.drawText (">", row.removeFromLeft (16), juce::Justification::centred);
            g.setColour (col::accent2);
            g.drawText (deviceName, row, juce::Justification::centredLeft);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! divedInsert.isEmpty() && sessionRect.contains (e.getPosition()))
            showRoot();                          // click SESSION to climb out
    }

    void resized() override
    {
        auto b = getLocalBounds();
        b.removeFromTop (kBar);
        if (editor != nullptr) editor->setBounds (b);
    }

private:
    static constexpr int kBar = 22;

    void setEditorFor (PatcherProcessor* pp)
    {
        editor.reset();                          // old editor dies before the new one
        if (pp != nullptr)
        {
            editor.reset (pp->createEditor());
            addAndMakeVisible (*editor);
        }
        resized();
        repaint();
    }

    AudioEngine& engine;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    String divedInsert, deviceName;
    juce::Rectangle<int> sessionRect;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodeView)
};

} // namespace dg
