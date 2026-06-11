#pragma once
#include "../Engine/AudioEngine.h"
#include "../Plugins/PluginHost.h"
#include "../Model/ClipOps.h"
#include "UIState.h"
#include "Look.h"

namespace dg
{

// Arrangement view: ruler (seek/loop/markers/tempo), track headers
// (mute/solo/arm/monitor/FX/automation), clip area with drag/trim/split/fades/
// take lanes, per-parameter automation lanes, zoom, drag-and-drop import.
class TimelineView : public juce::Component,
                     public juce::ValueTree::Listener,
                     private juce::Timer
{
public:
    TimelineView (AudioEngine&, SessionModel&, PluginHost&, UIState&);
    ~TimelineView() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    void rebuild();                          // recreate rows/clips from the model
    void splitSelectedAtPlayhead();
    void deleteSelected();
    void duplicateSelected();
    void copySelected (bool cut);
    void pasteAtPlayhead();
    void rippleDeleteSelected();             // delete + pull later clips left (per track)
    void selectAll();
    void showTrackFxMenu (ValueTree track, juce::Component* target);
    void showAutomationMenu (ValueTree track, juce::Component* target);

    // import audio files; pos is in TimelineView coordinates (over the canvas =
    // exact row + snapped time, anywhere else = playhead on a sensible track)
    void importFiles (const juce::StringArray& files, juce::Point<int> posInView);

    // "fx:rack" | "fx:builtin:<name>" | "fx:plug:<identifier>" -> insert/instrument
    void applyFxToTrack (ValueTree track, const String& fxId);

    void syncToolbar();                      // reflect ui.tool after keyboard switch

    double timeToX (double sec) const  { return sec * pps; }
    double xToTime (double x) const    { return x / pps; }
    double snap (double sec) const;

    AudioEngine& engine;
    SessionModel& session;
    PluginHost& plugins;
    UIState& ui;
    double pps = 60.0;                       // pixels per second

private:
    class Ruler;
    class Canvas;
    class ClipComp;
    class AutoLaneComp;
    class TrackHeader;
    class LaneHeader;

    struct Row
    {
        ValueTree track, lane;               // lane invalid => track row
        int y = 0, h = 0;
    };
    std::vector<Row> rows;

    void layoutRows();
    void layoutCanvasChildren();
    int rowIndexAtY (int canvasY) const;
    double contentLengthSec() const;
    void zoomAround (double factor, int pivotX);
    juce::PopupMenu buildParamTargetMenu (ValueTree track, int& nextId, juce::StringArray& targets);

    void timerCallback() override;

    void valueTreePropertyChanged (ValueTree&, const Identifier&) override;
    void valueTreeChildAdded (ValueTree&, ValueTree&) override;
    void valueTreeChildRemoved (ValueTree&, ValueTree&, int) override;
    void valueTreeChildOrderChanged (ValueTree&, int, int) override;
    void valueTreeParentChanged (ValueTree&) override {}

    std::unique_ptr<Ruler> ruler;
    std::unique_ptr<Canvas> canvas;
    juce::Viewport vp;
    juce::Component headerHolder;            // clips the scrolled header strip
    juce::Component headerStrip;             // container scrolled in y
    juce::OwnedArray<juce::Component> headers;
    juce::OwnedArray<ClipComp> clipComps;
    juce::OwnedArray<AutoLaneComp> laneComps;

    bool rebuildPending = false, layoutPending = false;
    int lastViewY = -1, lastViewX = -1;
    std::vector<clipops::ClipboardItem> clipboard;
    std::unique_ptr<juce::Component> toolbar;

    int headerW() const { return juce::jlimit (150, 420, ui.timelineHeaderW); }

    struct HeaderDivider : juce::Component
    {
        TimelineView& tv;
        int startW = 0;
        explicit HeaderDivider (TimelineView& t) : tv (t)
        { setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); }
        void mouseDown (const juce::MouseEvent&) override { startW = tv.headerW(); }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            tv.ui.timelineHeaderW = juce::jlimit (150, 420, startW + e.getDistanceFromDragStartX());
            if (tv.ui.persistInt) tv.ui.persistInt ("timelineHeaderW", tv.ui.timelineHeaderW);
            tv.resized();
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (col::line);
            g.fillRect (getLocalBounds().withSizeKeepingCentre (2, juce::jmin (60, getHeight())));
        }
    };
    std::unique_ptr<HeaderDivider> headerDivider;

    static constexpr int kRulerH = 30;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineView)
};

} // namespace dg
