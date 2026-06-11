#include "Dialogs.h"

namespace dg
{

juce::Component* createAudioSettingsComponent (AudioEngine& engine)
{
    auto* c = new juce::AudioDeviceSelectorComponent (engine.deviceManager,
                                                      0, 64, 0, 64,
                                                      true,      // midi inputs
                                                      true,      // midi output
                                                      true,      // stereo pairs
                                                      false);
    c->setSize (520, 600);
    return c;
}

juce::Component* createPluginManagerComponent (PluginHost& host)
{
    auto* c = new juce::PluginListComponent (host.formatManager, host.knownList,
                                             host.deadMansPedal, host.props, true);
    c->setSize (720, 520);
    return c;
}

// =========================================================================== export

namespace
{
    class ExportComponent : public juce::Component
    {
    public:
        ExportComponent (AudioEngine& e, SessionModel& s) : engine (e), session (s)
        {
            auto addRow = [this] (juce::Label& l, const String& text, juce::Component& c)
            {
                l.setText (text, juce::dontSendNotification);
                addAndMakeVisible (l);
                addAndMakeVisible (c);
            };
            rangeBox.addItemList ({ "Whole session", "Loop region" }, 1);
            rangeBox.setSelectedItemIndex (0);
            fmtBox.addItemList ({ "WAV", "AIFF" }, 1);
            fmtBox.setSelectedItemIndex (0);
            bitsBox.addItemList ({ "16-bit", "24-bit", "32-bit float" }, 1);
            bitsBox.setSelectedItemIndex (1);
            srBox.addItemList ({ "44100", "48000", "88200", "96000" }, 1);
            srBox.setSelectedItemIndex (1);
            addRow (rangeL, "Range", rangeBox);
            addRow (fmtL, "Format", fmtBox);
            addRow (bitsL, "Bit depth", bitsBox);
            addRow (srL, "Sample rate", srBox);

            stemsToggle.setButtonText ("Export stems (one file per track)");
            addAndMakeVisible (stemsToggle);

            exportBtn.onClick = [this] { doExport(); };
            addAndMakeVisible (exportBtn);

            captureBtn.setButtonText (engine.isCapturing() ? "STOP CAPTURE" : "REALTIME CAPTURE...");
            captureBtn.onClick = [this] { toggleCapture(); };
            addAndMakeVisible (captureBtn);

            status.setColour (juce::Label::textColourId, col::dim);
            status.setText ("Offline bounce runs through the full graph (master chain applied).",
                            juce::dontSendNotification);
            addAndMakeVisible (status);

            setSize (430, 250);
        }

        void doExport()
        {
            const bool aiff = fmtBox.getSelectedItemIndex() == 1;
            juce::FileChooser fc ("Export audio", File::getSpecialLocation (File::userMusicDirectory),
                                  aiff ? "*.aiff" : "*.wav");
            if (! fc.browseForFileToSave (true)) return;

            RenderSpec spec;
            spec.file = fc.getResult().withFileExtension (aiff ? ".aiff" : ".wav");
            spec.aiff = aiff;
            spec.bits = bitsBox.getSelectedItemIndex() == 0 ? 16 : bitsBox.getSelectedItemIndex() == 1 ? 24 : 32;
            spec.sampleRate = srBox.getText().getDoubleValue();
            spec.stems = stemsToggle.getToggleState();
            if (rangeBox.getSelectedItemIndex() == 1)
            {
                auto tr = session.transport();
                spec.startSec = tr[id::loopStart];
                spec.endSec = tr[id::loopEnd];
            }
            Renderer renderer (engine, session, spec);
            const bool ok = renderer.runRender();
            status.setText (ok ? "Exported: " + spec.file.getFullPathName()
                               : "Export failed / cancelled.", juce::dontSendNotification);
        }

        void toggleCapture()
        {
            if (engine.isCapturing())
            {
                engine.stopMasterCapture();
                captureBtn.setButtonText ("REALTIME CAPTURE...");
                status.setText ("Capture stopped.", juce::dontSendNotification);
                return;
            }
            juce::FileChooser fc ("Capture master output to...", File::getSpecialLocation (File::userMusicDirectory), "*.wav");
            if (! fc.browseForFileToSave (true)) return;
            if (engine.startMasterCapture (fc.getResult().withFileExtension (".wav")))
            {
                captureBtn.setButtonText ("STOP CAPTURE");
                status.setText ("Capturing master output (32f WAV) until stopped...", juce::dontSendNotification);
            }
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (12);
            auto row = [&b] { return b.removeFromTop (28); };
            auto layout = [&] (juce::Label& l, juce::Component& c)
            {
                auto r = row();
                l.setBounds (r.removeFromLeft (110));
                c.setBounds (r.reduced (0, 2));
            };
            layout (rangeL, rangeBox);
            layout (fmtL, fmtBox);
            layout (bitsL, bitsBox);
            layout (srL, srBox);
            stemsToggle.setBounds (row());
            auto btns = row();
            exportBtn.setBounds (btns.removeFromLeft (130).reduced (2));
            captureBtn.setBounds (btns.reduced (2));
            status.setBounds (b);
        }

    private:
        AudioEngine& engine;
        SessionModel& session;
        juce::Label rangeL, fmtL, bitsL, srL, status;
        juce::ComboBox rangeBox, fmtBox, bitsBox, srBox;
        juce::ToggleButton stemsToggle;
        juce::TextButton exportBtn { "EXPORT..." }, captureBtn;
    };
}

void showExportDialog (AudioEngine& engine, SessionModel& session)
{
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (new ExportComponent (engine, session));
    opts.dialogTitle = "Export / Render";
    opts.dialogBackgroundColour = col::panel;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.launchAsync();
}

// =========================================================================== video

VideoView::VideoView (AudioEngine& e, SessionModel& s) : engine (e), session (s)
{
    loadBtn.onClick = [this]
    {
        juce::FileChooser fc ("Load video", {}, "*.mp4;*.mov;*.avi;*.mkv;*.webm");
        if (fc.browseForFileToOpen())
            loadVideo (fc.getResult());
    };
    addAndMakeVisible (loadBtn);

    fpsBox.addItemList ({ "24", "25", "29.97", "30", "60" }, 1);
    const double fps = session.video().getProperty (id::fps, 25.0);
    fpsBox.setSelectedItemIndex (fps < 24.5 ? 0 : fps < 27 ? 1 : fps < 29.99 ? 2 : fps < 31 ? 3 : 4,
                                 juce::dontSendNotification);
    fpsBox.onChange = [this]
    {
        const double vals[] = { 24.0, 25.0, 29.97, 30.0, 60.0 };
        session.video().setProperty (id::fps, vals[juce::jmax (0, fpsBox.getSelectedItemIndex())], nullptr);
    };
    addAndMakeVisible (fpsBox);

    info.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain)));
    info.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (info);

   #if DG_HAVE_VIDEO
    video = std::make_unique<juce::VideoComponent> (false);
    addAndMakeVisible (*video);
   #endif

    startTimerHz (30);
    setSize (640, 420);
}

VideoView::~VideoView() = default;

ValueTree VideoView::videoClip() const
{
    for (const auto& t : session.tracks())
        if (t[id::type].toString() == "video")
            for (const auto& c : t.getChildWithName (id::CLIPS))
                return c;
    return {};
}

void loadVideoFile (SessionModel& session, const File& f)
{
    session.undo.beginNewTransaction ("load video");

    ValueTree vtrack;
    for (auto t : session.tracks())
        if (t[id::type].toString() == "video") { vtrack = t; break; }
    if (! vtrack.isValid())
        vtrack = session.addTrack ("video", "VIDEO");

    auto clips = SessionModel::clipsOf (vtrack);
    for (int i = clips.getNumChildren(); --i >= 0;)
        clips.removeChild (i, &session.undo);

    ValueTree c (id::CLIP);
    c.setProperty (id::uid, SessionModel::newUID(), nullptr);
    c.setProperty (id::type, "video", nullptr);
    c.setProperty (id::name, f.getFileName(), nullptr);
    c.setProperty (id::file, f.getFullPathName(), nullptr);
    c.setProperty (id::start, 0.0, nullptr);
    c.setProperty (id::length, 60.0, nullptr);
    clips.appendChild (c, &session.undo);

    session.video().setProperty (id::file, f.getFullPathName(), nullptr);
}

void VideoView::loadVideo (const File& f)
{
    loadVideoFile (session, f);
}

void VideoView::timerCallback()
{
    auto clip = videoClip();
    if (! clip.isValid())
    {
        info.setText ("no video loaded", juce::dontSendNotification);
        return;
    }

    // decoder follows the tree, so video dropped anywhere in the app loads here
    if (clip[id::file].toString() != lastLoadedFile)
    {
        lastLoadedFile = clip[id::file].toString();
       #if DG_HAVE_VIDEO
        video->load (File (lastLoadedFile));
        const double dur = video->getVideoDuration();
        if (dur > 0 && std::abs ((double) clip[id::length] - dur) > 0.5)
            clip.setProperty (id::length, dur, nullptr);
       #endif
    }

    // A/V lock: video time = transport time - clip start (the clip on the
    // video track is the offset authority; it survives bounce untouched).
    const double offset = clip[id::start];
    const double t = engine.getPositionSeconds() - offset;
    const double fps = session.video().getProperty (id::fps, 25.0);
    const juce::int64 frame = (juce::int64) std::floor (juce::jmax (0.0, t) * fps);

    info.setText (clip[id::name].toString() + "   t=" + String (t, 3) + "s   frame " + String (frame)
                  + (t < 0 ? "  (before video start)" : ""),
                  juce::dontSendNotification);

   #if DG_HAVE_VIDEO
    if (video->isVideoOpen())
    {
        if (engine.isPlaying() && t >= 0)
        {
            if (! videoPlaying) { video->setPlayPosition (t); video->play(); videoPlaying = true; }
            else if (std::abs (video->getPlayPosition() - t) > 0.08)
                video->setPlayPosition (t);          // drift correction
        }
        else if (videoPlaying) { video->stop(); videoPlaying = false; }
        else
            video->setPlayPosition (juce::jmax (0.0, t));   // scrub follows playhead
    }
   #endif
}

void VideoView::resized()
{
    auto b = getLocalBounds();
    auto bar = b.removeFromTop (28).reduced (4, 2);
    loadBtn.setBounds (bar.removeFromLeft (120));
    bar.removeFromLeft (6);
    fpsBox.setBounds (bar.removeFromLeft (80));
    auto infoArea = b.removeFromBottom (24);
    info.setBounds (infoArea);
   #if DG_HAVE_VIDEO
    video->setBounds (b);
   #endif
}

void VideoView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
   #if ! DG_HAVE_VIDEO
    g.setColour (col::dim);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawFittedText ("video decode stubbed on Linux (DG_HAVE_VIDEO=0)\n"
                      "transport lock + frame counter below run the real sync logic",
                      getLocalBounds().reduced (20), juce::Justification::centred, 3);
   #endif
}

} // namespace dg
