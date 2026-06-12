#include "MainComponent.h"
#include "UI/Updater.h"

namespace dg
{

MainComponent::MainComponent()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName = "hikim";
    opts.folderName = "characterglitch";
    opts.filenameSuffix = ".settings";
    opts.osxLibrarySubFolder = "Application Support";
    appProps.setStorageParameters (opts);

    // restore design prefs before any view paints
    auto* props = appProps.getUserSettings();
    Look::get().setTheme (props->getBoolValue ("uiLight", false));
    juce::Desktop::getInstance().setGlobalScaleFactor ((float) props->getDoubleValue ("uiScale", 1.0));
    ui.timelineHeaderW = props->getIntValue ("timelineHeaderW", 220);
    ui.persistInt = [this] (const String& key, int v)
    {
        appProps.getUserSettings()->setValue (key, v);
    };

    pluginHost = std::make_unique<PluginHost> (appProps.getUserSettings());
    engine = std::make_unique<AudioEngine> (session, *pluginHost, appProps.getUserSettings());

    transportBar = std::make_unique<TransportBar> (*engine, session, ui);
    addAndMakeVisible (*transportBar);

    timeline = std::make_unique<TimelineView> (*engine, session, *pluginHost, ui);
    addAndMakeVisible (*timeline);

    sessionGrid = std::make_unique<SessionGrid> (*engine, session, ui);
    addChildComponent (*sessionGrid);          // Tab / V / transport button cycles views
    routingView = std::make_unique<RoutingView> (*engine, session, ui);
    addChildComponent (*routingView);
    routingView->showFxMenu = [this] (ValueTree track, juce::Component* target)
    {
        timeline->showTrackFxMenu (track, target);
    };
    transportBar->onToggleView = [this] { toggleView(); };
    transportBar->onSetView = [this] (int v) { setView (v); };
    transportBar->setViewLabel ("SESSION");
    transportBar->onHelp = [this]
    {
        helpOverlay.setVisible (! helpOverlay.isVisible());
        helpOverlay.toFront (false);
    };

    mixer = std::make_unique<MixerView> (*engine, session, ui);
    pianoRoll = std::make_unique<PianoRoll> (*engine, session, ui);
    fileBin = std::make_unique<FileBin> (*engine, appProps.getUserSettings());
    fxExplorer = std::make_unique<FxExplorer> (*pluginHost);
    chainPanel = std::make_unique<ChainPanel> (*engine, session, ui);
    patchView = std::make_unique<PatchView> (*engine, session, ui);
    sampleEditor = std::make_unique<SampleEditor> (*engine, session, ui);

    // modular zones: find things LEFT, the selected thing RIGHT, the playing
    // surface BOTTOM - every panel can be moved (right-click its chip),
    // collapsed (click its chip), and 0 clears the whole screen to focus
    dock = std::make_unique<Dock> (appProps.getUserSettings());
    dock->registerPanel ("FILES", fileBin.get(), Dock::zLeft);
    dock->registerPanel ("FX", fxExplorer.get(), Dock::zLeft);
    dock->registerPanel ("CHAIN", chainPanel.get(), Dock::zRight);
    dock->registerPanel ("SAMPLE", sampleEditor.get(), Dock::zRight);
    dock->registerPanel ("PATCH", patchView.get(), Dock::zRight);
    dock->registerPanel ("MIXER", mixer.get(), Dock::zBottom);
    dock->registerPanel ("PIANO ROLL", pianoRoll.get(), Dock::zBottom);
    dock->restore();
    dock->onLayoutChanged = [this] { resized(); };
    addAndMakeVisible (*dock);

    chainPanel->showFxMenu = [this] (ValueTree track, juce::Component* target)
    {
        timeline->showTrackFxMenu (track, target);
    };

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
        selectTab ("PIANO ROLL");
    };
    ui.openSampleEditor = [this] (ValueTree clip)
    {
        sampleEditor->setClip (clip);
        selectTab ("SAMPLE");
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
    engine->onInsertWillBeRemoved = [this] (const String& insertUid)
    {
        editorWindows.erase (insertUid);    // editor + window die before their processor does
    };

    session.onSessionReplaced = [this]
    {
        editorWindows.clear();
        engine->sessionReplaced();
        timeline->rebuild();
        mixer->rebuild();
        pianoRoll->setClip ({});
        refreshTitle();
    };

    addChildComponent (helpOverlay);

    startTimerHz (20);
    setSize (1500, 900);

    // silent daily update check, well after first paint
    juce::Timer::callAfterDelay (6000, [props] { Updater::checkAsync (props, false); });
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
    dock->setBounds (b);
    const auto center = dock->layoutAndGetCenter();
    timeline->setBounds (center);
    sessionGrid->setBounds (center);
    routingView->setBounds (center);
    timeline->setVisible (viewMode == 0);
    sessionGrid->setVisible (viewMode == 1);
    routingView->setVisible (viewMode == 2);
    dock->toFront (false);                  // zones overlay the views; center stays click-through
    helpOverlay.setBounds (getLocalBounds());
    helpOverlay.toFront (false);
}

void MainComponent::HelpOverlay::paint (juce::Graphics& g)
{
    g.fillAll (col::bg.withAlpha (0.92f));
    auto r = getLocalBounds().withSizeKeepingCentre (juce::jmin (940, getWidth() - 60),
                                                     juce::jmin (560, getHeight() - 60));
    g.setColour (col::panel);
    g.fillRoundedRectangle (r.toFloat(), 8.0f);
    g.setColour (col::accent);
    g.drawRoundedRectangle (r.toFloat(), 8.0f, 1.5f);

    auto inner = r.reduced (26);
    g.setColour (col::accent);
    g.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    g.drawText (String (names::appName) + " - how to drive it", inner.removeFromTop (34), juce::Justification::left);
    g.setColour (col::dim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("click anywhere to close  -  ? or F1 brings this back", inner.removeFromTop (22), juce::Justification::left);
    inner.removeFromTop (8);

    static const char* colA =
        "GETTING SOUND\n"
        "Drag audio files anywhere - they become clips\n"
        "SESSION button (or Tab): the loop grid - click cells to jam\n"
        "Double-click an Inst track: draws you a loop\n"
        "Space / K  play-stop      R  record\n"
        "J / L  jump a bar back / forward (Shift = 4 bars)\n"
        "Return  back to start      Shift+L  loop on-off\n"
        "\n"
        "EDITING\n"
        "Tools 1/2/3: arrow = move, razor = split, X = delete\n"
        "Drag clip edges to trim - corners for fades\n"
        "Arrows  nudge selected clips (Shift = fine)\n"
        "Double-click clips: SAMPLE editor / PIANO ROLL\n"
        "Ctrl+Z undo anything";
    static const char* colB =
        "BREAKING SOUND (the fun part)\n"
        "FX button on a track > Add TEETH - the corruption rack\n"
        "Try the GESTURES menu inside it\n"
        "PATCHER view: drag CHAOS onto a channel - pick a knob\n"
        "WIRES: build your own instrument from boxes + cables\n"
        "\n"
        "KEYS\n"
        "Ctrl+X/C/V  cut / copy / paste      Ctrl+D duplicate\n"
        "S split      Shift+Del ripple delete      M mute track\n"
        "Ctrl+L  loop around selection      Ctrl+A select all\n"
        "Ctrl+T / Ctrl+Shift+T  new audio / MIDI track\n"
        "Ctrl+wheel or + / -  zoom      Ctrl+S save  Ctrl+E export\n"
        "Ctrl+N new  Ctrl+O open      Options: theme + UI scale";

    g.setColour (col::text);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain)));
    auto half = inner.removeFromLeft (inner.getWidth() / 2);
    g.drawFittedText (colA, half.reduced (4), juce::Justification::topLeft, 30);
    g.drawFittedText (colB, inner.reduced (4), juce::Justification::topLeft, 30);
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
        m.addItem (mAddPatchTrack, "Add WIRES track (a channel that is a patch)");
        m.addItem (mAddBus, "Add bus");
        m.addItem (mAddVideo, "Add video track");
    }
    else if (index == 3)
    {
        m.addItem (mAudioSettings, "Audio device settings...");
        m.addItem (mPluginManager, "Plugin manager...");
        m.addItem (mVideoWindow, "Video window...");
        m.addSeparator();
        m.addItem (mThemeLight, "Light theme", true, Look::get().isLight());
        juce::PopupMenu scale;
        const double cur = appProps.getUserSettings()->getDoubleValue ("uiScale", 1.0);
        scale.addItem (mScale90, "90%", true, std::abs (cur - 0.9) < 0.01);
        scale.addItem (mScale100, "100%", true, std::abs (cur - 1.0) < 0.01);
        scale.addItem (mScale110, "110%", true, std::abs (cur - 1.1) < 0.01);
        scale.addItem (mScale125, "125%", true, std::abs (cur - 1.25) < 0.01);
        scale.addItem (mScale150, "150%", true, std::abs (cur - 1.5) < 0.01);
        m.addSubMenu ("UI scale", scale);
        m.addSeparator();
        m.addItem (mFocusMode, "Focus mode (hide all panels)\t0", true, dock->isFocus());
        m.addSeparator();
        m.addItem (mCheckUpdates, "Check for updates...");
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
        case mCheckUpdates: Updater::checkAsync (appProps.getUserSettings(), true); break;
        case mFocusMode: dock->toggleFocus(); break;
        case mUndo: session.undo.undo(); break;
        case mRedo: session.undo.redo(); break;
        case mAddAudio: session.undo.beginNewTransaction ("add track"); session.addTrack ("audio", "Audio " + String (session.tracks().getNumChildren())); break;
        case mAddMidi: session.undo.beginNewTransaction ("add track"); session.addTrack ("midi", "Inst " + String (session.tracks().getNumChildren())); break;
        case mAddBus: session.undo.beginNewTransaction ("add track"); session.addTrack ("bus", "Bus " + String (session.tracks().getNumChildren())); break;
        case mAddVideo: session.undo.beginNewTransaction ("add track"); session.addTrack ("video", "VIDEO"); break;
        case mAddPatchTrack:
        {
            session.undo.beginNewTransaction ("add wires track");
            auto t = session.addTrack ("midi", "WIRES " + String (session.tracks().getNumChildren()));
            auto ins = session.addInsert (t, "instrument");
            ins.setProperty (id::ident, "builtin:wires", &session.undo);
            ins.setProperty (id::name, names::patcherName, &session.undo);
            ui.selectedTrack = t[id::uid].toString();
            break;
        }
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
        case mThemeLight: applyTheme (! Look::get().isLight()); break;
        case mScale90:  applyScale (0.9); break;
        case mScale100: applyScale (1.0); break;
        case mScale110: applyScale (1.1); break;
        case mScale125: applyScale (1.25); break;
        case mScale150: applyScale (1.5); break;
        default: break;
    }
}

void MainComponent::applyTheme (bool light)
{
    Look::get().setTheme (light);
    appProps.getUserSettings()->setValue ("uiLight", light);
    juce::LookAndFeel::setDefaultLookAndFeel (&Look::get());   // re-broadcast to every component
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* c = desktop.getComponent (i))
        {
            c->sendLookAndFeelChange();
            c->repaint();
        }
}

void MainComponent::applyScale (double scale)
{
    appProps.getUserSettings()->setValue ("uiScale", scale);
    juce::Desktop::getInstance().setGlobalScaleFactor ((float) scale);
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
        if (File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;m4a;caf;wma;opus;aac;wv;mp2;amr;mka;mp4;mov;avi;mkv;webm")
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

void MainComponent::selectTab (const String& name)
{
    dock->showPanel (name);
}

void MainComponent::toggleView()
{
    setView ((viewMode + 1) % 3);           // arrange -> session -> patcher -> ...
}

void MainComponent::setView (int v)
{
    viewMode = juce::jlimit (0, 2, v);
    static const char* next[] = { "SESSION", "PATCHER", "ARRANGE" };
    transportBar->setViewLabel (next[viewMode]);    // the button names the destination
    resized();
}

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

    if (kc == juce::KeyPress::tabKey || is ('V'))
    {
        toggleView();
        return true;
    }
    if (kc == juce::KeyPress::F1Key || k.getTextCharacter() == '?')
    {
        helpOverlay.setVisible (! helpOverlay.isVisible());
        helpOverlay.toFront (false);
        return true;
    }
    if (k.getTextCharacter() == '+' || k.getTextCharacter() == '=')
    { timeline->zoomKey (true); return true; }
    if (k.getTextCharacter() == '-')
    { timeline->zoomKey (false); return true; }
    if (kc == juce::KeyPress::spaceKey) { engine->togglePlayStop(); return true; }
    if (kc == juce::KeyPress::homeKey) { engine->seekSeconds (0.0); return true; }
    if (k.getModifiers().isCommandDown())
    {
        if (is ('S')) { doSave (k.getModifiers().isShiftDown()); return true; }
        if (is ('E')) { showExportDialog (*engine, session); return true; }
        if (is ('Z')) { k.getModifiers().isShiftDown() ? session.undo.redo() : session.undo.undo(); return true; }
        if (is ('D')) { timeline->duplicateSelected(); return true; }
        if (is ('X')) { timeline->copySelected (true); return true; }
        if (is ('C')) { timeline->copySelected (false); return true; }
        if (is ('V')) { timeline->pasteAtPlayhead(); return true; }
        if (is ('A')) { timeline->selectAll(); return true; }
        if (is ('L')) { timeline->loopToSelection(); return true; }
        if (is ('N')) { doNew(); return true; }
        if (is ('O')) { doOpen(); return true; }
        if (is ('T'))
        {
            session.undo.beginNewTransaction ("add track");
            session.addTrack (k.getModifiers().isShiftDown() ? "midi" : "audio",
                              (k.getModifiers().isShiftDown() ? "Inst " : "Audio ")
                                  + String (session.tracks().getNumChildren()));
            return true;
        }
        return false;
    }
    if (kc == '1' || kc == '2' || kc == '3')
    {
        ui.tool = kc == '1' ? Tool::select : kc == '2' ? Tool::razor : Tool::erase;
        timeline->syncToolbar();
        return true;
    }
    if (kc == '0')                          // focus: clear the screen, again to bring panels back
    {
        dock->toggleFocus();
        return true;
    }

    // JKL shuttle, premiere-style: J = bar back, K = play/stop, L = bar forward (Shift = x4 / loop)
    auto barSeconds = [this]
    {
        auto map = engine->getTempoMap();
        auto bb = map->barBeatAt (map->samplesToBeats (engine->getPositionSamples()));
        return map->beatsToSeconds (bb.barStartBeat + bb.num * 4.0 / bb.den)
             - map->beatsToSeconds (bb.barStartBeat);
    };
    if (is ('J'))
    {
        const double step = barSeconds() * (k.getModifiers().isShiftDown() ? 4.0 : 1.0);
        engine->seekSeconds (juce::jmax (0.0, engine->getPositionSeconds() - step));
        return true;
    }
    if (is ('K')) { engine->togglePlayStop(); return true; }
    if (is ('L'))
    {
        if (k.getModifiers().isShiftDown())
        {
            auto tr = session.transport();
            tr.setProperty (id::loopOn, ! (bool) tr[id::loopOn], nullptr);
            return true;
        }
        engine->seekSeconds (engine->getPositionSeconds() + barSeconds());
        return true;
    }
    if (kc == juce::KeyPress::leftKey)  { timeline->nudgeSelected (-1, k.getModifiers().isShiftDown()); return true; }
    if (kc == juce::KeyPress::rightKey) { timeline->nudgeSelected (1, k.getModifiers().isShiftDown()); return true; }
    if (kc == juce::KeyPress::returnKey) { engine->seekSeconds (0.0); return true; }
    if (is ('M'))
    {
        auto t = session.findTrack (ui.selectedTrack);
        if (t.isValid())
            t.setProperty (id::mute, ! (bool) t[id::mute], nullptr);
        return true;
    }
    if (is ('R')) { engine->toggleRecord(); return true; }
    if (is ('S')) { timeline->splitSelectedAtPlayhead(); return true; }
    if (kc == juce::KeyPress::deleteKey || kc == juce::KeyPress::backspaceKey)
    {
        if (k.getModifiers().isShiftDown()) timeline->rippleDeleteSelected();
        else timeline->deleteSelected();
        return true;
    }
    if (kc == juce::KeyPress::escapeKey)
    {
        ui.selectedClips.clear();
        timeline->repaint();
        return true;
    }
    return false;
}

} // namespace dg
