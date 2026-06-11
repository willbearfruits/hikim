#pragma once
#include "PatcherProcessor.h"
#include "../UI/Look.h"

namespace dg
{

// Max-style canvas: inlets on top edges, outlets on bottom edges, double-click
// the canvas and type an object ("osc~ 220"), drag boxes around, drag a cable
// from an outlet to an inlet, right-click nodes/cables to delete.
class PatcherEditor : public juce::AudioProcessorEditor,
                      public juce::DragAndDropContainer,
                      public juce::DragAndDropTarget,
                      private juce::Timer
{
public:
    explicit PatcherEditor (PatcherProcessor&);
    ~PatcherEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    void rebuildNodes();
    void beginCable (const String& srcUid, int port, juce::Point<float> from);
    void dragCable (juce::Point<float> p);
    void endCable (juce::Point<float> p);
    void placeObject (const String& specName, juce::Point<int> pos);
    bool isCableDragging() const { return draggingCable; }

    // palette drags
    bool isInterestedInDragSource (const SourceDetails& d) override
    { return d.description.toString().startsWith ("obj:"); }
    void itemDropped (const SourceDetails& d) override
    {
        placeObject (d.description.toString().fromFirstOccurrenceOf ("obj:", false, false),
                     d.localPosition);
    }

    PatcherProcessor& patcher;
    static constexpr int kPaletteW = 168;

private:
    class NodeComp;
    class ObjPalette;

    juce::Point<float> outletPos (const String& uid, int port) const;
    juce::Point<float> inletPos (const String& uid, int port) const;
    ValueTree cableAt (juce::Point<float>) const;
    void timerCallback() override;

    juce::OwnedArray<NodeComp> nodeComps;
    std::unique_ptr<ObjPalette> palette;
    juce::Slider pKnobs[8];
    juce::Label pLabels[8];
    std::unique_ptr<juce::SliderParameterAttachment> pAtts[8];
    juce::TextEditor objEntry;
    juce::Label hint;
    int placeStagger = 0;

    String cableSrc;
    int cableSrcPort = 0;
    bool draggingCable = false;
    juce::Point<float> cableFrom, cableTo;

    int lastPatchHash = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatcherEditor)
};

} // namespace dg
