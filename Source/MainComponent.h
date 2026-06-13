#pragma once
#include "Model/Session.h"
#include "Engine/AudioEngine.h"
#include "Plugins/PluginHost.h"
#include "UI/TransportBar.h"
#include "UI/TimelineView.h"
#include "UI/MixerView.h"
#include "UI/PianoRoll.h"
#include "UI/Dialogs.h"
#include "UI/BrowserPanel.h"
#include "UI/ChainPanel.h"
#include "UI/SessionGrid.h"
#include "UI/SampleEditor.h"
#include "UI/Dock.h"
#include "UI/StatusBar.h"
#include "UI/NodeView.h"

namespace dg
{

class MainComponent : public juce::Component,
                      public juce::MenuBarModel,
                      public juce::KeyListener,
                      public juce::FileDragAndDropTarget,
                      public juce::DragAndDropContainer,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (col::bg); }

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const String&) override;
    void menuItemSelected (int itemID, int topLevelIndex) override;

    // KeyListener (attached to the top-level window)
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    bool keyStateChanged (bool isKeyDown, juce::Component*) override;   // hold-to-temp tool revert

    // window-wide drops: audio anywhere, .dgproj opens, video loads the video track
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    std::function<void (const String&)> updateTitle;

    juce::ApplicationProperties appProps;
    std::unique_ptr<PluginHost> pluginHost;
    SessionModel session;
    std::unique_ptr<AudioEngine> engine;
    UIState ui;

private:
    enum MenuIds
    {
        mNew = 1, mOpen, mSave, mSaveAs, mExport, mQuit,
        mUndo, mRedo,
        mAddAudio, mAddMidi, mAddBus, mAddVideo, mAddPatchTrack,
        mAudioSettings, mPluginManager, mVideoWindow, mMixerWindow,
        mThemeLight, mScale90, mScale100, mScale110, mScale125, mScale150,
        mCheckUpdates, mFocusMode
    };

    void toggleView();
    void setView (int v);
    void applyTheme (bool light);
    void applyScale (double scale);
    void doNew();
    void doOpen();
    void doSave (bool saveAs);
    void refreshTitle();
    void openInsertEditor (const String& trackUid, const String& insertUid);
    void showVideoWindow();
    void showMixerWindow();                 // mixer is a detachable window (v2: separated)
    void timerCallback() override;

    std::unique_ptr<TransportBar> transportBar;
    std::unique_ptr<TimelineView> timeline;
    std::unique_ptr<SessionGrid> sessionGrid;
    std::unique_ptr<NodeView> patcherView;  // PATCHER = the session graph + dive/breadcrumb
    int viewMode = 0;                       // 0 arrange, 1 session, 2 patcher
    std::unique_ptr<Dock> dock;             // modular LEFT/RIGHT/BOTTOM panel zones
    std::unique_ptr<StatusBar> statusBar;   // v2 footer hint bar
    void updateViewHint();                  // footer text per view
    int bottomBandKind = 0;                 // 0 none, 1 track (DEVICES), 2 clip (PIANO ROLL/SAMPLE)
    int heldToolKey = 0;                    // hold-to-temp: a tool key held > 250ms reverts on release
    Tool toolBeforeHold = Tool::select;
    double toolHoldStartMs = 0.0;
    std::unique_ptr<MixerView> mixer;
    std::unique_ptr<PianoRoll> pianoRoll;
    std::unique_ptr<FileBin> fileBin;
    std::unique_ptr<FxExplorer> fxExplorer;
    std::unique_ptr<ChainPanel> chainPanel;
    std::unique_ptr<SampleEditor> sampleEditor;
    void selectTab (const String& name);

    std::unique_ptr<FloatingWindow> settingsWin, pluginWin, videoWin, mixerWin;
    std::map<String, std::unique_ptr<FloatingWindow>> editorWindows;   // by insert uid

    juce::TooltipWindow tooltips { nullptr, 600 };
    TapTempo tapTempo;                      // T key feeds it; writes the tempo event

    // dismissable cheatsheet for newcomers (? button / F1)
    struct HelpOverlay : juce::Component
    {
        HelpOverlay() { setInterceptsMouseClicks (true, false); }
        void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
        void paint (juce::Graphics&) override;
    };
    HelpOverlay helpOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace dg
