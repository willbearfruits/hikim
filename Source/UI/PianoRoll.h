#pragma once
#include "../Engine/AudioEngine.h"
#include "../Model/ClipOps.h"
#include "UIState.h"
#include "Look.h"

namespace dg
{

// Piano-roll grid tools, switched by the header strip or keys 1/2/3 while
// the roll has focus (mirrors the timeline's tool keys).
enum class RollTool { select, pencil, erase };

// Full piano roll on one MIDI clip: select/pencil/erase tools (rubber-band,
// move, tail-resize, draw, sweep-erase), velocity strip, note clipboard
// (Ctrl+C/X/V/D when the roll has focus), quantize with strength + swing,
// humanize (off-grid is first-class), scale lock, MIDI step input.
// // EXTEND: note repeat as a live MIDI FX.
class PianoRoll : public juce::Component, private juce::Timer
{
public:
    PianoRoll (AudioEngine&, SessionModel&, UIState&);
    ~PianoRoll() override;

    void setClip (ValueTree midiClip);
    ValueTree getClip() const { return clip; }

    void resized() override;
    void paint (juce::Graphics&) override;

    // note clipboard: keys act here while the roll has focus (click it first)
    bool keyPressed (const juce::KeyPress&) override;
    void copySelectedNotes (bool cut);
    void pasteClipboard();                   // at the playhead inside the clip, else beat 0
    void duplicateSelectedNotes();           // copies land one selection-span later
    void deleteSelectedNotes();
    void selectAllNotes();

    RollTool tool = RollTool::select;
    void setTool (RollTool);

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
    class ToolStrip;

    void quantizeSelected (bool humanizeOnly);
    void timerCallback() override;

    std::unique_ptr<Keys> keys;
    std::unique_ptr<Grid> grid;
    std::unique_ptr<ToolStrip> toolStrip;
    juce::Viewport vp;
    juce::Component keysHolder;

    juce::ComboBox gridBox, scaleBox;
    juce::Slider strengthSl, swingSl, humanSl;
    juce::TextButton quantBtn { "QUANTIZE" }, humanBtn { "HUMANIZE" }, stepBtn { "STEP REC" };
    juce::Label clipLabel;

    double stepPos = 0.0;
    std::vector<clipops::NoteData> noteClipboard;   // survives clip switches: cross-clip paste

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};

} // namespace dg
