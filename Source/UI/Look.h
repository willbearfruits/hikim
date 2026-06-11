#pragma once
#include "../Common.h"

namespace dg
{

// Runtime palette: every view reads these at paint time, so switching theme
// repaints the whole app. Dark = the default brutal night; light = warm
// paper-zine brutalism, same blood accent.
namespace col
{
    extern juce::Colour bg, panel, panelHi, line, text, dim;
    extern juce::Colour accent, accent2, play, record, clipAudio, clipMidi;
}

class Look : public juce::LookAndFeel_V4
{
public:
    Look();
    static Look& get();

    void setTheme (bool lightTheme);
    bool isLight() const { return light; }

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getLabelFont (juce::Label&) override;

private:
    bool light = false;
};

} // namespace dg
