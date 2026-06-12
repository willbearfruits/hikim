#pragma once
#include "../Engine/AudioEngine.h"
#include "UIState.h"
#include "Look.h"
#include "NodeCanvas.h"

namespace dg
{

// The patch bay: everything modulates everything. Source nodes (LFO1-4,
// Lorenz chaos, envelope follower) on the left; any parameter in the session
// can be added as a target node; drag a cable from a source port onto a
// target to connect, click a cable to edit its amount, right-click to cut it.
// Rides the shared NodeCanvas (zoom/pan like WIRES); this view is its
// modulation delegate. EXTEND: audio-signal node patching over the same
// canvas (the engine graph already supports arbitrary routing - this canvas
// is the modulation half).
class PatchView : public juce::Component, private juce::Timer,
                  private NodeCanvas::Delegate
{
public:
    PatchView (AudioEngine&, SessionModel&, UIState&);
    ~PatchView() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void rebuild();

    void endCableDrag (juce::Point<float> dropPos);     // canvas coords

    AudioEngine& engine;
    SessionModel& session;
    UIState& ui;

private:
    class SourceNode;
    class TargetNode;

    juce::Point<float> sourcePortPos (const String& srcId) const;
    juce::Point<float> targetPortPos (const String& targetUid) const;
    String srcLabel (const String& srcId) const;
    ValueTree modAt (juce::Point<float> p) const;       // cable hit test, canvas coords
    void addTargetMenu();
    void timerCallback() override
    {
        int n = 0;
        for (const auto& t : session.mods())
            if (t.hasType (id::MODTARGET)) ++n;
        if (n != targets.size()) rebuild();
        else if ((int) engine.getModSources().size() != knownSources)
            rebuild();                              // a wires modout tap appeared / vanished
        repaint();
        canvas->repaint();
    }

    // NodeCanvas::Delegate (the modulation content behind the shared surface)
    void paintCables (juce::Graphics&) override;
    void canvasDoubleClicked (juce::Point<int>) override;
    void canvasMouseUp (juce::Point<float>) override;
    void canvasPopup (juce::Point<float>) override;
    bool canvasClicked (juce::Point<float>) override;

    std::unique_ptr<NodeCanvas> canvas;
    juce::OwnedArray<SourceNode> sources;
    juce::OwnedArray<TargetNode> targets;
    juce::TextButton addTargetBtn { "+ TARGET" };

    // cable drag state
    String dragFromSrc;                 // source id, empty = not dragging
    juce::Point<float> dragPos;
    int knownSources = -1;              // refresh when wires taps appear/vanish

    // selected cable inspector
    ValueTree selectedMod;
    juce::Slider amountSl;
    juce::TextButton deleteCableBtn { "CUT CABLE" };
    juce::Label inspectorLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatchView)
};

} // namespace dg
