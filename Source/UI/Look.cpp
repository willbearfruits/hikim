#include "Look.h"

namespace dg
{

Look::Look()
{
    auto scheme = getDarkColourScheme();
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::windowBackground, col::bg);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::widgetBackground, col::panel);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::menuBackground, col::panel);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::outline, col::line);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::defaultText, col::text);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::defaultFill, col::accent);
    scheme.setUIColour (juce::LookAndFeel_V4::ColourScheme::highlightedText, juce::Colours::white);
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
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, col::panelHi);
    setColour (juce::ComboBox::outlineColourId, col::line);
    setColour (juce::TextEditor::backgroundColourId, col::panelHi);
    setColour (juce::TextEditor::outlineColourId, col::line);
    setColour (juce::PopupMenu::backgroundColourId, col::panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent.withAlpha (0.35f));
    setColour (juce::ScrollBar::thumbColourId, col::line);
    setColour (juce::TooltipWindow::backgroundColourId, col::panelHi);
    setColour (juce::ListBox::backgroundColourId, col::panel);
    setColour (juce::TabbedComponent::backgroundColourId, col::bg);
    setColour (juce::TabbedButtonBar::tabTextColourId, col::dim);
    setColour (juce::TabbedButtonBar::frontTextColourId, col::text);
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

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg,
                                 bool highlighted, bool down)
{
    auto bounds = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = bg;
    if (down) c = c.brighter (0.3f);
    else if (highlighted) c = c.brighter (0.12f);
    g.setColour (c);
    g.fillRoundedRectangle (bounds, 2.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (bounds, 2.0f, 1.0f);
}

juce::Font Look::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions (juce::jmin (13.0f, (float) buttonHeight * 0.6f), juce::Font::bold));
}

juce::Font Look::getComboBoxFont (juce::ComboBox&)  { return juce::Font (juce::FontOptions (13.0f)); }
juce::Font Look::getPopupMenuFont()                 { return juce::Font (juce::FontOptions (14.0f)); }

} // namespace dg
