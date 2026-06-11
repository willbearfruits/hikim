#include "MainComponent.h"

namespace dg
{

MainComponent::MainComponent()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName = "dawglitch";
    opts.folderName = "characterglitch";
    opts.filenameSuffix = ".settings";
    opts.osxLibrarySubFolder = "Application Support";
    appProps.setStorageParameters (opts);

    pluginHost = std::make_unique<PluginHost> (appProps.getUserSettings());
    engine = std::make_unique<AudioEngine> (session, *pluginHost, appProps.getUserSettings());

    transportBar = std::make_unique<TransportBar> (*engine, session, ui);
    addAndMakeVisible (*transportBar);

    timeline = std::make_unique<TimelineView> (*engine, session, *pluginHost, ui);
    addAndMakeVisible (*timeline);

    mixer = std::make_unique<MixerView> (*engine, session, ui);
    pianoRoll = std::make_unique<PianoRoll> (*engine, session, ui);
    fileBin = std::make_unique<FileBin> (*engine, appProps.getUserSettings());
    fxExplorer = std::make_unique<FxExplorer> (*pluginHost);
    bottomTabs.addTab ("MIXER", col::panel, mixer.get(), false);
    bottomTabs.addTab ("PIANO ROLL", col::panel, pianoRoll.get(), false);
    bottomTabs.addTab ("FILES", col::panel, fileBin.get(), false);
    bottomTabs.addTab ("FX", col::panel, fxExplorer.get(), false);
    addAndMakeVisible (bottomTabs);

    fxExplorer->onApply = [this] (const String& fxId)
    {
        auto t = session.findTrack (ui.selectedTrack);
        if (! t.isValid())
            for (auto tt : session.tracks())
                if (tt[id::type].toString() == "audio") { t = tt; break; }
        timeline->applyFxToTrack (t, fxId);
    };

    ui.openPianoRoll = [this] (ValueTree clip)
    {
        pianoRoll->setClip (clip);
        bottomTabs.setCurrentTabIndex (1);
    };
    ui.openInsertEditor = [this] (const String& trackUid, const String& insertUid)
    {
        openInsertEditor (trackUid, insertUid);
    };
    mixer->showFxMenu = [this] (ValueTree track, juce::Component* target)
    {
        timeline->showTrackFxMenu (track, target);
    };

    engine->onRecordingFinished = [this] { timeline->rebuild(); };

    session.onSessionReplaced = [this]
    {
        editorWindows.clear();
        engine->sessionReplaced();
        timeline->rebuild();
        mixer->rebuild();
        pianoRoll->setClip ({});
        refreshTitle();
    };

    startTimerHz (20);
    setSize (1500, 900);
}

MainComponent::~MainComponent()
{
    editorWindows.clear();
    settingsWin.reset();
    pluginWin.reset();
    videoWin.reset();
}

void MainComponent::resized()
{
    auto b = getLocalBounds();
    transportBar->setBounds (b.removeFromTop (42));
    bottomTabs.setBounds (b.removeFromBottom (juce::jmax (180, getHeight() / 4)));
    timeline->setBounds (b);
}

// ---------------------------------------------------------------- timer: automation write drain

void MainComponent::timerCallback()
{
    for (const auto& w : engine->drainAutomationWrites())
    {
        ValueTree lane = w.lane;
        if (! lane.isValid()) continue;
        // thin: reuse a point if one sits within 40 ms
        ValueTree hit;
        for (auto pt : lane)
            if (pt.hasType (id::PT) && std::abs ((double) pt[id::t] - w.tSec) < 0.04)
            { hit = pt; break; }
        if (hit.isValid())
            hit.setProperty (id::v, w.v, nullptr);
        else
        {
            ValueTree pt (id::PT);
            pt.setProperty (id::t, w.tSec, nullptr);
            pt.setProperty (id::v, w.v, nullptr);
            lane.appendChild (pt, nullptr);
        }
    }
}

// ---------------------------------------------------------------- menus

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "Track", "Options" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const String&)
{
    juce::PopupMenu m;
    if (index == 0)
    {
        m.addItem (mNew, "New session");
        m.addItem (mOpen, "Open... (" + String (names::projectExtension) + ")");
        m.addItem (mSave, "Save\tCtrl+S");
        m.addItem (mSaveAs, "Save as...");
        m.addSeparator();
        m.addItem (mExport, "Export / render...\tCtrl+E");
        m.addSeparator();
        m.addItem (mQuit, "Quit");
    }
    else if (index == 1)
    {
        m.addItem (mUndo, "Undo\tCtrl+Z", session.undo.canUndo());
        m.addItem (mRedo, "Redo\tCtrl+Shift+Z", session.undo.canRedo());
    }
    else if (index == 2)
    {
        m.addItem (mAddAudio, "Add audio track");
        m.addItem (mAddMidi, "Add MIDI/instrument track");
        m.addItem (mAddBus, "Add bus");
        m.addItem (mAddVideo, "Add video track");
    }
    else if (index == 3)
    {
        m.addItem (mAudioSettings, "Audio device settings...");
        m.addItem (mPluginManager, "Plugin manager...");
        m.addItem (mVideoWindow, "Video window...");
    }
    return m;
}

void MainComponent::menuItemSelected (int itemID, int)
{
    switch (itemID)
    {
        case mNew: doNew(); break;
        case mOpen: doOpen(); break;
        case mSave: doSave (false); break;
        case mSaveAs: doSave (true); break;
        case mExport: showExportDialog (*engine, session); break;
        case mQuit: juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
        case mUndo: session.undo.undo(); break;
        case mRedo: session.undo.redo(); break;
        case mAddAudio: session.undo.beginNewTransaction ("add track"); session.addTrack ("audio", "Audio " + String (session.tracks().getNumChildren())); break;
        case mAddMidi: session.undo.beginNewTransaction ("add track"); session.addTrack ("midi", "Inst " + String (session.tracks().getNumChildren())); break;
        case mAddBus: session.undo.beginNewTransaction ("add track"); session.addTrack ("bus", "Bus " + String (session.tracks().getNumChildren())); break;
        case mAddVideo: session.undo.beginNewTransaction ("add track"); session.addTrack ("video", "VIDEO"); break;
        case mAudioSettings:
            if (settingsWin == nullptr)
            {
                settingsWin = std::make_unique<FloatingWindow> ("Audio settings", [this] { settingsWin.reset(); });
                settingsWin->setContentOwned (createAudioSettingsComponent (*engine), true);
            }
            settingsWin->setVisible (true);
            settingsWin->toFront (true);
            break;
        case mPluginManager:
            if (pluginWin == nullptr)
            {
                pluginWin = std::make_unique<FloatingWindow> ("Plugins", [this] { pluginWin.reset(); });
                pluginWin->setContentOwned (createPluginManagerComponent (*pluginHost), true);
            }
            pluginWin->setVisible (true);
            pluginWin->toFront (true);
            break;
        case mVideoWindow:
            showVideoWindow();
            break;
        default: break;
    }
}

void MainComponent::showVideoWindow()
{
    if (videoWin == nullptr)
    {
        videoWin = std::make_unique<FloatingWindow> ("Video", [this] { videoWin.reset(); });
        videoWin->setContentOwned (new VideoView (*engine, session), true);
    }
    videoWin->setVisible (true);
    videoWin->toFront (true);
}

// ---------------------------------------------------------------- window-wide drops

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;m4a;caf;wma;mp4;mov;avi;mkv;webm")
            || f.endsWith (names::projectExtension))
            return true;
    return false;
}

void MainComponent::filesDropped (const juce::StringArray& files, int x, int y)
{
    juce::StringArray audio;
    File project, video;
    for (const auto& fpath : files)
    {
        const File f (fpath);
        if (f.hasFileExtension (String (names::projectExtension).substring (1)))      project = f;
        else if (f.hasFileExtension ("mp4;mov;avi;mkv;webm"))                          video = f;
        else                                                                           audio.add (fpath);
    }

    if (project != File())
    {
        engine->stop();
        String err;
        if (! session.load (project, err))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Open failed", err);
        return;
    }
    if (video != File())
    {
        loadVideoFile (session, video);
        showVideoWindow();
    }
    if (! audio.isEmpty())
        timeline->importFiles (audio, timeline->getLocalPoint (this, juce::Point<int> (x, y)));
}

// ---------------------------------------------------------------- file ops

void MainComponent::doNew()
{
    engine->stop();
    session.newSession();
}

void MainComponent::doOpen()
{
    engine->stop();
    juce::FileChooser fc ("Open session", File::getSpecialLocation (File::userDocumentsDirectory),
                          "*" + String (names::projectExtension));
    if (! fc.browseForFileToOpen()) return;
    String err;
    if (! session.load (fc.getResult(), err))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Open failed", err);
}

void MainComponent::doSave (bool saveAs)
{
    engine->syncToTree();
    if (saveAs || session.projectFile == File())
    {
        juce::FileChooser fc ("Save session", File::getSpecialLocation (File::userDocumentsDirectory),
                              "*" + String (names::projectExtension));
        if (! fc.browseForFileToSave (true)) return;
        session.saveAs (fc.getResult().withFileExtension (names::projectExtension));
    }
    else
        session.save();
    refreshTitle();
}

void MainComponent::refreshTitle()
{
    if (updateTitle)
        updateTitle (String (names::appName)
                     + (session.projectFile != File() ? " - " + session.projectFile.getFileNameWithoutExtension() : ""));
}

// ---------------------------------------------------------------- insert editors

void MainComponent::openInsertEditor (const String&, const String& insertUid)
{
    auto existing = editorWindows.find (insertUid);
    if (existing != editorWindows.end() && existing->second != nullptr)
    {
        existing->second->setVisible (true);
        existing->second->toFront (true);
        return;
    }

    auto* proc = engine->getInsertProcessor (insertUid);
    if (proc == nullptr) return;

    juce::AudioProcessorEditor* ed = proc->hasEditor() ? proc->createEditorIfNeeded() : nullptr;
    juce::Component* content = ed != nullptr ? static_cast<juce::Component*> (ed)
                                             : new juce::GenericAudioProcessorEditor (*proc);

    auto win = std::make_unique<FloatingWindow> (proc->getName(), [this, insertUid]
    {
        // defer destruction - we're inside the window's own close callback
        juce::MessageManager::callAsync ([this, insertUid] { editorWindows.erase (insertUid); });
    });
    win->setContentOwned (content, true);
    win->setVisible (true);
    win->toFront (true);
    editorWindows[insertUid] = std::move (win);
}

bool MainComponent::keyPressed (const juce::KeyPress& k, juce::Component* origin)
{
    // don't steal keys from text editing
    if (dynamic_cast<juce::TextEditor*> (origin) != nullptr)
        return false;

    const int kc = k.getKeyCode();
    auto is = [kc] (char c) { return kc == c || kc == c + 32; };   // letter, either case

    if (kc == juce::KeyPress::spaceKey) { engine->togglePlayStop(); return true; }
    if (kc == juce::KeyPress::homeKey) { engine->seekSeconds (0.0); return true; }
    if (k.getModifiers().isCommandDown())
    {
        if (is ('S')) { doSave (k.getModifiers().isShiftDown()); return true; }
        if (is ('E')) { showExportDialog (*engine, session); return true; }
        if (is ('Z')) { k.getModifiers().isShiftDown() ? session.undo.redo() : session.undo.undo(); return true; }
        if (is ('D')) { timeline->duplicateSelected(); return true; }
        return false;
    }
    if (is ('R')) { engine->toggleRecord(); return true; }
    if (is ('L'))
    {
        auto tr = session.transport();
        tr.setProperty (id::loopOn, ! (bool) tr[id::loopOn], nullptr);
        return true;
    }
    if (is ('S')) { timeline->splitSelectedAtPlayhead(); return true; }
    if (kc == juce::KeyPress::deleteKey || kc == juce::KeyPress::backspaceKey)
    {
        timeline->deleteSelected();
        return true;
    }
    return false;
}

} // namespace dg
