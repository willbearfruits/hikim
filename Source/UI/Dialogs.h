#pragma once
#include "../Engine/AudioEngine.h"
#include "../Engine/Renderer.h"
#include "../Plugins/PluginHost.h"
#include "Look.h"

namespace dg
{

// Generic floating tool window that reports close to its owner.
class FloatingWindow : public juce::DocumentWindow
{
public:
    FloatingWindow (const String& title, std::function<void()> onCloseFn)
        : DocumentWindow (title, col::panel, DocumentWindow::allButtons), onClose (std::move (onCloseFn))
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
    }
    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    std::function<void()> onClose;
};

juce::Component* createAudioSettingsComponent (AudioEngine&);
juce::Component* createPluginManagerComponent (PluginHost&);

void showExportDialog (AudioEngine&, SessionModel&);

// puts the file on the video track (creating it if needed); any open VideoView
// picks the change up from the tree
void loadVideoFile (SessionModel&, const File&);

// Video window: frame-accurate playback locked to the transport on mac/win;
// on Linux the decoder is stubbed (DG_HAVE_VIDEO=0) but the sync/offset logic
// runs identically and shows file/timecode/frame. // EXTEND: ffmpeg/libmpv decode on Linux.
class VideoView : public juce::Component, private juce::Timer
{
public:
    VideoView (AudioEngine&, SessionModel&);
    ~VideoView() override;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    void loadVideo (const File&);
    ValueTree videoClip() const;     // first clip on the first video track

    AudioEngine& engine;
    SessionModel& session;
    juce::TextButton loadBtn { "LOAD VIDEO..." };
    juce::ComboBox fpsBox;
    juce::Label info;

    String lastLoadedFile;
   #if DG_HAVE_VIDEO
    std::unique_ptr<juce::VideoComponent> video;
    bool videoPlaying = false;
   #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VideoView)
};

} // namespace dg
