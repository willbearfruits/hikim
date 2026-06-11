#pragma once
#include "../Common.h"

namespace dg
{

// Dark, high-contrast, slightly brutal. A tool, not a poster.
namespace col
{
    const juce::Colour bg        (0xff0d0d0d);
    const juce::Colour panel     (0xff181818);
    const juce::Colour panelHi   (0xff222222);
    const juce::Colour line      (0xff333333);
    const juce::Colour text      (0xffd8d8d8);
    const juce::Colour dim       (0xff808080);
    const juce::Colour accent    (0xffe04040);     // blood
    const juce::Colour accent2   (0xffe0a040);     // rust
    const juce::Colour play      (0xff50c878);
    const juce::Colour record    (0xffff2020);
    const juce::Colour clipAudio (0xff3a6ea5);
    const juce::Colour clipMidi  (0xff7a55d1);
}

class Look : public juce::LookAndFeel_V4
{
public:
    Look();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

} // namespace dg
