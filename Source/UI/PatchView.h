#pragma once
#include "../Engine/AudioEngine.h"
#include "UIState.h"
#include "Look.h"

namespace dg
{

// The patch bay: everything modulates everything. Source nodes (LFO1-4,
// Lorenz chaos, envelope follower) on the left; any parameter in the session
// can be added as a target node; drag a cable from a source port onto a
// target to connect, click a cable to edit its amount, right-click to cut it.
// EXTEND: audio-signal node patching over the same canvas (the engine graph
// already supports arbitrary routing - this canvas is the modulation half).
class PatchView : public juce::Component, private juce::Timer
{
public:
    PatchView (AudioEngine&, SessionModel&, UIState&);
    ~PatchView() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void rebuild();

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void endCableDrag (juce::Point<float> dropPos);

    AudioEngine& engine;
    SessionModel& session;
    UIState& ui;

private:
    class SourceNode;
    class TargetNode;

    juce::Point<float> sourcePortPos (const String& srcId) const;
    juce::Point<float> targetPortPos (const String& targetUid) const;
    String srcLabel (const String& srcId) const;
    ValueTree modAt (juce::Point<float> p) const;       // cable hit test
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
    }

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
