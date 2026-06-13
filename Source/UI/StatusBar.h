#pragma once
#include "Look.h"
#include "UIState.h"

namespace dg
{

// The v2 footer: an always-on strip across the very bottom. Left = a context
// hint that tells you what you can do right now (the cheapest answer to "not
// intuitive"); right = the active editing tool. Bitwig-shaped - its footer
// always surfaces the available actions. Panels push hints via ui.setHint;
// the tool chip mirrors ui.tool live.
class StatusBar : public juce::Component, private juce::Timer
{
public:
    explicit StatusBar (UIState& u) : ui (u) { startTimerHz (10); }

    void setHint (const String& h) { if (h != hint) { hint = h; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::panel);
        g.setColour (col::line);
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());

        auto b = getLocalBounds().reduced (10, 0);

        // right: the active tool chip
        const String tn = toolName (ui.tool);
        const int tw = tn.length() * 8 + 16;
        auto chip = b.removeFromRight (tw).reduced (0, 3);
        g.setColour (col::accent.withAlpha (0.85f));
        g.fillRoundedRectangle (chip.toFloat(), 3.0f);
        g.setColour (col::bg);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (tn, chip, juce::Justification::centred);

        b.removeFromRight (10);

        // left: the live hint
        g.setColour (col::dim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (hint, b, juce::Justification::centredLeft, true);
    }

private:
    void timerCallback() override
    {
        if (ui.tool != lastTool) { lastTool = ui.tool; repaint(); }
    }

    UIState& ui;
    String hint;
    Tool lastTool = Tool::select;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StatusBar)
};

} // namespace dg
