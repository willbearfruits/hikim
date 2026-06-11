#pragma once
#include "../Engine/AudioEngine.h"
#include "UIState.h"
#include "Look.h"

namespace dg
{

// Full piano roll on one MIDI clip: draw/move/resize/velocity, marquee select,
// quantize with strength + swing, humanize (off-grid is first-class), scale
// lock, MIDI step input. // EXTEND: note repeat as a live MIDI FX.
class PianoRoll : public juce::Component, private juce::Timer
{
public:
    PianoRoll (AudioEngine&, SessionModel&, UIState&);
    ~PianoRoll() override;

    void setClip (ValueTree midiClip);
    ValueTree getClip() const { return clip; }

    void resized() override;
    void paint (juce::Graphics&) override;

    AudioEngine& engine;
    SessionModel& session;
    UIState& ui;

    double ppb = 90.0;                       // pixels per beat
    static constexpr int kNoteH = 11;
    static constexpr int kLowNote = 12, kHighNote = 120;
    static constexpr int kVelH = 64;

    double gridBeats() const;
    int snapPitch (int note) const;
    double clipLenBeats() const;

    ValueTree clip;
    std::set<int> selected;                  // indices into NOTES

private:
    class Keys;
    class Grid;

    void quantizeSelected (bool humanizeOnly);
    void timerCallback() override;

    std::unique_ptr<Keys> keys;
    std::unique_ptr<Grid> grid;
    juce::Viewport vp;
    juce::Component keysHolder;

    juce::ComboBox gridBox, scaleBox;
    juce::Slider strengthSl, swingSl, humanSl;
    juce::TextButton quantBtn { "QUANTIZE" }, humanBtn { "HUMANIZE" }, stepBtn { "STEP REC" };
    juce::Label clipLabel;

    double stepPos = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};

} // namespace dg
