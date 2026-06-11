#pragma once
#include "../Engine/AudioEngine.h"
#include "UIState.h"
#include "Look.h"

namespace dg
{

// Sample editor: the whole source file as a waveform, the clip's used region
// highlighted and draggable (slide the body, trim the edges), plus gain,
// fades, stretch/pitch-lock and conform-to-tempo. Works on arrange clips and
// session slots alike - they're the same CLIP trees.
// EXTEND: zoom + region audition + onset slice markers (the slicer grows here).
class SampleEditor : public juce::Component, private juce::Timer
{
public:
    SampleEditor (AudioEngine&, SessionModel&, UIState&);
    ~SampleEditor() override;

    void setClip (ValueTree audioClip);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    enum class Drag { none, left, right, body };

    juce::Rectangle<int> waveArea() const;
    double xToFileSec (int x) const;
    int fileSecToX (double sec) const;
    double fileDur() const;
    double regionStart() const;        // file seconds
    double regionDur() const;          // file seconds
    void timerCallback() override { repaint(); }

    AudioEngine& engine;
    SessionModel& session;
    UIState& ui;

    ValueTree clip;
    std::unique_ptr<juce::AudioThumbnail> thumb;

    juce::Slider gainSl, fadeInSl, fadeOutSl;
    juce::Label gainL, fadeInL, fadeOutL, nameL, infoL;
    juce::TextButton pitchLockBtn { "PITCH-LOCK" }, conformBtn { "CONFORM" }, previewBtn { "PREVIEW" };

    Drag drag = Drag::none;
    double dragStartSec = 0, origOffset = 0, origLen = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleEditor)
};

} // namespace dg
