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
        mAddAudio, mAddMidi, mAddBus, mAddVideo,
        mAudioSettings, mPluginManager, mVideoWindow
    };

    void doNew();
    void doOpen();
    void doSave (bool saveAs);
    void refreshTitle();
    void openInsertEditor (const String& trackUid, const String& insertUid);
    void showVideoWindow();
    void timerCallback() override;

    std::unique_ptr<TransportBar> transportBar;
    std::unique_ptr<TimelineView> timeline;
    juce::TabbedComponent bottomTabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<MixerView> mixer;
    std::unique_ptr<PianoRoll> pianoRoll;
    std::unique_ptr<FileBin> fileBin;
    std::unique_ptr<FxExplorer> fxExplorer;

    std::unique_ptr<FloatingWindow> settingsWin, pluginWin, videoWin;
    std::map<String, std::unique_ptr<FloatingWindow>> editorWindows;   // by insert uid

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace dg
