#pragma once
#include "PatcherProcessor.h"
#include "../UI/Look.h"
#include "../UI/NodeCanvas.h"

namespace dg
{

// Max-style canvas: inlets on top edges, outlets on bottom edges, double-click
// the canvas and type an object ("osc~ 220"), drag boxes around, drag a cable
// from an outlet to an inlet, right-click nodes/cables to delete.
// Navigation/LOD machinery lives in the shared NodeCanvas (one canvas, three
// altitudes); this editor is its WIRES delegate. `number` boxes drag their
// value directly (Ctrl-drag moves the box).
class PatcherEditor : public juce::AudioProcessorEditor,
                      public juce::DragAndDropContainer,
                      public juce::DragAndDropTarget,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer,
                      private NodeCanvas::Delegate
{
public:
    explicit PatcherEditor (PatcherProcessor&);
    ~PatcherEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void rebuildNodes();
    void beginCable (const String& srcUid, int port, juce::Point<float> fromCanvas);
    void dragCable (juce::Point<float> pCanvas);
    void endCable (juce::Point<float> pCanvas);
    void placeObject (const String& specName, juce::Point<int> posInEditor);
    bool isCableDragging() const { return draggingCable; }
    float canvasZoom() const;                      // node faces LOD on this

    // palette drags + FILES bin drags (audio lands as a sample~)
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        const String desc = d.description.toString();
        return desc.startsWith ("obj:") || desc == "binfiles";
    }
    void itemDropped (const SourceDetails& d) override;

    // OS file drags: drop audio anywhere on the canvas
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        if (! files.isEmpty()) dropAudioFile (files[0], { x, y });
    }
    void dropAudioFile (const String& path, juce::Point<int> posInEditor);

    PatcherProcessor& patcher;
    static constexpr int kPaletteW = 168;

private:
    class NodeComp;
    class ObjPalette;

    juce::Point<float> outletPos (const String& uid, int port) const;   // canvas coords
    juce::Point<float> inletPos (const String& uid, int port) const;
    ValueTree cableAt (juce::Point<float> pCanvas) const;
    void timerCallback() override;

    // NodeCanvas::Delegate (the WIRES content behind the shared surface)
    void paintCables (juce::Graphics&) override;
    void canvasDoubleClicked (juce::Point<int>) override;
    void canvasMouseUp (juce::Point<float>) override;
    void canvasPopup (juce::Point<float>) override;

    std::unique_ptr<NodeCanvas> canvas;
    juce::OwnedArray<NodeComp> nodeComps;          // children of the canvas
    std::unique_ptr<ObjPalette> palette;
    juce::Slider pKnobs[8];
    juce::Label pLabels[8];
    std::unique_ptr<juce::SliderParameterAttachment> pAtts[8];
    juce::TextEditor objEntry;                     // child of the canvas
    juce::Label hint;
    int placeStagger = 0;

    String cableSrc;
    int cableSrcPort = 0;
    bool draggingCable = false;
    juce::Point<float> cableFrom, cableTo;         // canvas coords

    int lastPatchHash = 0;

    friend class NodeComp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatcherEditor)
};

} // namespace dg
