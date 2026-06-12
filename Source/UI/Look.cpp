#include "Look.h"

namespace dg
{

namespace col
{
    juce::Colour bg, panel, panelHi, line, text, dim;
    juce::Colour accent, accent2, play, record, clipAudio, clipMidi;
    juce::Colour nodeSource, nodeEffect, nodeMath, nodeTime, nodeRouting;
}

Look& Look::get()
{
    static Look instance;
    return instance;
}

Look::Look()
{
    setTheme (false);
}

void Look::setTheme (bool lightTheme)
{
    light = lightTheme;

    if (light)
    {
        col::bg      = juce::Colour (0xfff0ede7);     // warm paper
        col::panel   = juce::Colour (0xffe4e0d8);
        col::panelHi = juce::Colour (0xffd8d3c9);
        col::line    = juce::Colour (0xffb9b4a9);
        col::text    = juce::Colour (0xff191815);
        col::dim     = juce::Colour (0xff6e6a60);
        col::accent  = juce::Colour (0xffc22d2d);     // same blood, printed
        col::accent2 = juce::Colour (0xffa06a10);
        col::play    = juce::Colour (0xff247a47);
        col::record  = juce::Colour (0xffc81d1d);
        col::clipAudio = juce::Colour (0xff2d5c8f);
        col::clipMidi  = juce::Colour (0xff5d3bb5);
        col::nodeSource  = juce::Colour (0xffa06a10);
        col::nodeEffect  = juce::Colour (0xffc22d2d);
        col::nodeMath    = juce::Colour (0xff4a6a8a);
        col::nodeTime    = juce::Colour (0xff247a47);
        col::nodeRouting = juce::Colour (0xff1a7a78);
    }
    else
    {
        col::bg      = juce::Colour (0xff0d0d0d);
        col::panel   = juce::Colour (0xff181818);
        col::panelHi = juce::Colour (0xff222222);
        col::line    = juce::Colour (0xff333333);
        col::text    = juce::Colour (0xffd8d8d8);
        col::dim     = juce::Colour (0xff808080);
        col::accent  = juce::Colour (0xffe04040);
        col::accent2 = juce::Colour (0xffe0a040);
        col::play    = juce::Colour (0xff50c878);
        col::record  = juce::Colour (0xffff2020);
        col::clipAudio = juce::Colour (0xff3a6ea5);
        col::clipMidi  = juce::Colour (0xff7a55d1);
        col::nodeSource  = juce::Colour (0xffe0a040);
        col::nodeEffect  = juce::Colour (0xffe04040);
        col::nodeMath    = juce::Colour (0xff7a9cc0);
        col::nodeTime    = juce::Colour (0xff50c878);
        col::nodeRouting = juce::Colour (0xff35b8b4);
    }

    auto scheme = light ? getLightColourScheme() : getDarkColourScheme();
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::windowBackground, col::bg);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::widgetBackground, col::panel);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::menuBackground, col::panel);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::outline, col::line);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::defaultText, col::text);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::defaultFill, col::accent);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::highlightedText,
                        light ? juce::Colours::black : juce::Colours::white);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::highlightedFill, col::accent.withAlpha (0.6f));
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::menuText, col::text);
    setColourScheme (scheme);

    setColour (juce::Slider::thumbColourId, col::accent);
    setColour (juce::Slider::trackColourId, col::accent.withAlpha (0.5f));
    setColour (juce::Slider::backgroundColourId, col::line);
    setColour (juce::Slider::textBoxTextColourId, col::dim);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TextButton::buttonColourId, col::panelHi);
    setColour (juce::TextButton::buttonOnColourId, col::accent);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, light ? juce::Colours::white : juce::Colours::white);
    setColour (juce::ToggleButton::textColourId, col::text);
    setColour (juce::ToggleButton::tickColourId, col::accent);
    setColour (juce::ComboBox::backgroundColourId, col::panelHi);
    setColour (juce::ComboBox::outlineColourId, col::line);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::arrowColourId, col::dim);
    setColour (juce::TextEditor::backgroundColourId, col::panelHi);
    setColour (juce::TextEditor::outlineColourId, col::line);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::PopupMenu::backgroundColourId, col::panel);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent.withAlpha (0.35f));
    setColour (juce::ScrollBar::thumbColourId, col::line);
    setColour (juce::TooltipWindow::backgroundColourId, col::panelHi);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::ListBox::backgroundColourId, col::panel);
    setColour (juce::TabbedComponent::backgroundColourId, col::bg);
    setColour (juce::TabbedButtonBar::tabTextColourId, col::dim);
    setColour (juce::TabbedButtonBar::frontTextColourId, col::text);
    setColour (juce::AlertWindow::backgroundColourId, col::panel);
    setColour (juce::AlertWindow::textColourId, col::text);
    setColour (juce::DocumentWindow::textColourId, col::text);
    setColour (juce::TreeView::backgroundColourId, col::panel);
    setColour (juce::FileBrowserComponent::currentPathBoxBackgroundColourId, col::panelHi);
    setColour (juce::FileBrowserComponent::currentPathBoxTextColourId, col::text);
    setColour (juce::FileBrowserComponent::filenameBoxBackgroundColourId, col::panelHi);
    setColour (juce::FileBrowserComponent::filenameBoxTextColourId, col::text);
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                             float pos, float startAngle, float endAngle, juce::Slider&)
{
    auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    auto r = bounds.withSizeKeepingCentre (size, size);
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float radius = size * 0.5f;
    const auto centre = r.getCentre();

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - 2, radius - 2, 0, startAngle, endAngle, true);
    g.setColour (col::line);
    g.strokePath (track, juce::PathStrokeType (2.4f));

    juce::Path val;
    val.addCentredArc (centre.x, centre.y, radius - 2, radius - 2, 0, startAngle, angle, true);
    g.setColour (col::accent);
    g.strokePath (val, juce::PathStrokeType (2.4f));

    g.setColour (col::text);
    juce::Path needle;
    needle.startNewSubPath (centre.getPointOnCircumference (radius * 0.35f, angle));
    needle.lineTo (centre.getPointOnCircumference (radius - 3.0f, angle));
    g.strokePath (needle, juce::PathStrokeType (1.8f));
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgCol,
                                 bool highlighted, bool down)
{
    auto bounds = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = bgCol;
    if (down) c = light ? c.darker (0.15f) : c.brighter (0.3f);
    else if (highlighted) c = light ? c.darker (0.06f) : c.brighter (0.12f);
    g.setColour (c);
    g.fillRoundedRectangle (bounds, 2.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (bounds, 2.0f, 1.0f);
}

juce::Font Look::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions (juce::jmin (15.0f, (float) buttonHeight * 0.62f), juce::Font::bold));
}

juce::Font Look::getComboBoxFont (juce::ComboBox&)  { return juce::Font (juce::FontOptions (14.0f)); }
juce::Font Look::getPopupMenuFont()                 { return juce::Font (juce::FontOptions (15.0f)); }
juce::Font Look::getLabelFont (juce::Label& l)      { return l.getFont(); }

} // namespace dg
