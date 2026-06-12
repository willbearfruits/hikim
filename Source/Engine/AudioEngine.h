#pragma once
#include "../Model/Session.h"
#include "TempoMap.h"
#include "Processors.h"
#include "ClipPlayer.h"
#include "MidiSource.h"
#include "StretchCache.h"
#include "Analysis.h"

namespace dg
{

class PluginHost;
class PatcherProcessor;

// The spine. Owns the device layer and the AudioProcessorGraph, drives the
// graph from its own audio callback so the transport can split blocks at loop
// and punch boundaries (sample-accurate wrap), implements the playhead for all
// hosted plugins, applies automation, records, and rebuilds itself from the
// session ValueTree (it is the tree's audio-side listener).
//
// Graph topology per track:
//   [audio in] -> ClipPlayer -> insert... -> ChannelStrip -> output bus head
//                                            ChannelStrip -> Send A/B -> bus head
//   [midi in]  -> MidiSource -> Instrument -> insert... -> ChannelStrip -> ...
//   bus:    insert... -> ChannelStrip -> master head
//   master: insert... -> ChannelStrip -> [audio out]
// PDC: juce::AudioProcessorGraph compensates parallel-path latency internally.
class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::AudioPlayHead,
                    public juce::MidiInputCallback,
                    public juce::ValueTree::Listener,
                    private juce::Timer,
                    private juce::AsyncUpdater,
                    private juce::ChangeListener
{
public:
    AudioEngine (SessionModel& s, PluginHost& ph, juce::PropertiesFile* appProps);
    ~AudioEngine() override;

    // ---- transport (message thread) ----
    void play();
    void stop();
    void togglePlayStop();
    void toggleRecord();
    void seekSeconds (double sec);
    bool isPlaying() const                  { return transportPlaying.load(); }
    bool isRecording() const                { return transportRecording.load(); }
    bool isRecordPending() const            { return recordPending.load(); }
    double getPositionSeconds() const       { return (double) transportPos.load() / currentSR; }
    juce::int64 getPositionSamples() const  { return transportPos.load(); }
    double getSampleRate() const            { return currentSR; }
    juce::int64 secToSamples (double s) const   { return (juce::int64) std::llround (s * currentSR); }
    double samplesToSec (juce::int64 sa) const  { return (double) sa / currentSR; }
    std::atomic<bool> overdubMidi { true };

    std::shared_ptr<const TempoMap> getTempoMap() const;

    // ---- model sync ----
    void sessionReplaced();
    void syncToTree();                      // strip/send params + plugin states -> tree (call before save)

    // ---- lookups for UI ----
    ChannelStripProcessor* getStrip (const String& trackUid) const;
    SendProcessor* getSend (const String& trackUid, int which) const;
    juce::AudioProcessor* getInsertProcessor (const String& insertUid) const;
    juce::AudioProcessor* getInstrumentFor (const String& trackUid) const;
    int getTotalLatencySamples() const;

    // ---- session view (launch grid) ----
    void launchSlot (ValueTree track, ValueTree clip, juce::int64 whenOverride = -1);   // quantized to the launch grid
    void stopTrackSession (const String& trackUid);
    void stopAllSession();

    // ---- session slot recording (Phase C) ----
    // punch in/out on launch-quantize boundaries; the finished take lands in the
    // slot and immediately loops with its phase anchored at the punch-out point
    void recordIntoSlot (ValueTree track, const String& sceneUid);
    void stopSlotRecord (const String& trackUid);
    int getSlotRecPhase (const String& trackUid, String& sceneOut) const;   // 0 none, 1 armed/pending, 2 rolling

    // ---- capture the jam (Phase C) ----
    // every slot launch/stop is logged; capture writes the log into the
    // arrangement as real clips at the transport positions where they played
    void captureSessionToArrangement();
    bool hasJamToCapture() const { return ! jamLog.empty(); }
    struct SlotState { String playing, pending; };
    SlotState getSessionState (const String& trackUid);     // message thread
    std::atomic<double> launchQuantizeBeats { 4.0 };
    juce::int64 getNextLaunchBoundary() { return nextLaunchBoundary(); }
    double getSessionLoopPhase (const String& trackUid) const;   // 0..1 while looping, else -1
    double estimateFileBpm (const File& f)
    {
        auto r = createAnyReader (f);
        return r != nullptr ? estimateBpmFromReader (*r) : 0.0;
    }

    // ---- modulation (PATCH view) ----
    // sources: 0..3 = LFO1..4, 4 = chaos (Lorenz), 5 = envelope follower,
    // 6.. = WIRES modout taps (dynamic; ids "wires:<insertUid>:<n>")
    static constexpr int kNumModSources = 6;
    struct ModSourceInfo { String id, label; };
    std::vector<ModSourceInfo> getModSources();                 // fixed six + live wires taps
    float getModSourceValueById (const String& id);             // for cable glow / port LEDs
    // owner (optional out) receives the graph node the parameter lives on, so
    // callers can keep the processor alive for as long as they hold the pointer
    juce::AudioProcessorParameter* resolveParamTarget (const String& trackUid, const String& target,
                                                       juce::AudioProcessorGraph::Node::Ptr* owner = nullptr) const;
    float getModSourceValue (int idx) const { return modSrcValues[(size_t) juce::jlimit (0, 5, idx)].load(); }

    // ---- ui midi tap (step input, midi indicators) ----
    std::vector<juce::MidiMessage> drainUiMidi();

    // ---- automation write drain (UI flushes into the tree) ----
    struct AutoWrite { ValueTree lane; double tSec; float v; };
    std::vector<AutoWrite> drainAutomationWrites();

    // ---- live recording readout for the timeline (message thread) ----
    const RecordSession* getLiveRecording (const String& trackUid) const;
    double getRecordStartSeconds() const { return recStartSec; }

    // ---- super multi-format media access ----
    // Returns a JUCE-readable file for any media: the file itself when a
    // built-in decoder opens it, otherwise a cached one-time ffmpeg transcode.
    File mediaFileFor (const File& source);
    std::unique_ptr<juce::AudioFormatReader> createAnyReader (const File& source);
    bool mediaReadyNow (const File& source);        // false = would block on an ffmpeg transcode

    // ---- file preview (FILES bin) ----
    void startPreview (const File&);
    void stopPreview();
    File getPreviewFile() const { return previewFile; }

    // ---- realtime master capture ----
    bool startMasterCapture (const File& f);
    void stopMasterCapture();
    bool isCapturing() const { return masterCapture.load() != nullptr; }

    // ---- offline render support ----
    void beginOffline (double sr, int blockSize);
    void processOffline (juce::AudioBuffer<float>& buf, juce::int64 posSamples);
    void endOffline();
    void setStemSolo (const String& trackUidOrEmpty);

    // ---- segment context, read by source nodes on the audio thread ----
    juce::int64 segStartSample() const { return segPos.load(); }
    bool segIsPlaying() const          { return segPlaying.load(); }
    bool segRecordEnabled() const      { return segRecEnabled.load(); }
    bool shouldFlushMidi() const       { return flushMidiCurrent.load(); }

    // ---- juce::AudioPlayHead ----
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override;

    // ---- juce::AudioIODeviceCallback ----
    void audioDeviceIOCallbackWithContext (const float* const* input, int numIn,
                                           float* const* output, int numOut, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    // ---- juce::MidiInputCallback ----
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    // ---- juce::ValueTree::Listener ----
    void valueTreePropertyChanged (ValueTree&, const Identifier&) override;
    void valueTreeChildAdded (ValueTree&, ValueTree&) override;
    void valueTreeChildRemoved (ValueTree&, ValueTree&, int) override;
    void valueTreeChildOrderChanged (ValueTree&, int, int) override;
    void valueTreeParentChanged (ValueTree&) override {}

    std::function<void()> onRecordingFinished;
    std::function<void()> onTransportStateChanged;
    std::function<void (const String& insertUid)> onInsertWillBeRemoved;   // close editors BEFORE the node dies

    SessionModel& session;
    PluginHost& pluginHost;
    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;        // audio file formats
    juce::AudioThumbnailCache thumbCache { 256 };
    juce::AudioProcessorGraph graph;
    std::unique_ptr<StretchCache> stretchCache;

private:
    struct TrackNodes
    {
        juce::AudioProcessorGraph::Node::Ptr source, instrument, builtinSynth, strip, sendA, sendB;
        std::vector<juce::AudioProcessorGraph::Node::Ptr> inserts;
    };

    struct LaneRT
    {
        juce::AudioProcessorParameter* param = nullptr;
        juce::AudioProcessorGraph::Node::Ptr owner;     // keeps the processor alive while the snapshot lives
        int mode = 0;                                   // 0 off, 1 read, 2 touch, 3 write
        std::vector<std::pair<juce::int64, float>> pts; // (samples, normalised)
        std::shared_ptr<std::atomic<bool>> touching;
        bool hosted = false;                            // hosted plugin: param applied by the message-thread pump
        std::shared_ptr<std::atomic<float>> hostedVal, hostedApplied;
    };

    struct LaneListener : juce::AudioProcessorParameter::Listener
    {
        AudioEngine& e; int laneIdx; std::shared_ptr<std::atomic<bool>> touching;
        juce::AudioProcessorParameter* param;
        juce::AudioProcessorGraph::Node::Ptr owner;     // param must outlive removeListener below
        LaneListener (AudioEngine& en, int idx, juce::AudioProcessorParameter* p,
                      std::shared_ptr<std::atomic<bool>> t, juce::AudioProcessorGraph::Node::Ptr o)
            : e (en), laneIdx (idx), touching (std::move (t)), param (p), owner (std::move (o)) { param->addListener (this); }
        ~LaneListener() override { param->removeListener (this); }
        void parameterValueChanged (int, float v) override { e.automationParamChanged (laneIdx, v); }
        void parameterGestureChanged (int, bool starting) override { touching->store (starting); }
    };

    struct ClickVoice { double phase = 0, inc = 0; float env = 0; int active = 0; };
    struct DirectMonitor { int chan; bool stereo; };

    // ---- internal: model -> engine ----
    void rebuildGraph();
    void rebuildTempoMap();
    void updateAllPlaylists();
    void updateTrackPlaylists (const ValueTree& track);
    void updateTrackFlags();
    void updateTransportFromTree();
    void rebuildAutomation();
    void detachAutomation();
    void rebuildMods();
    void applyMods (juce::int64 pos, int numSamples);
    void scheduleRebuild (int what);

    void connectChain (const std::vector<juce::AudioProcessorGraph::Node::Ptr>& nodes);
    juce::AudioProcessorGraph::Node::Ptr getBusHead (const String& busUid);
    void instantiateInsert (const ValueTree& insert, double sr, int blockSize);

    // ---- session view internals ----
    struct SessionAction
    {
        ClipPlayerProcessor* a = nullptr;
        MidiSourceProcessor* m = nullptr;
        juce::int64 when = 0, loopLen = 0;
        bool stop = false;
        bool recBegin = false, recEnd = false;          // slot-record punch in/out
        RecordSession* recSess = nullptr;               // audio slot record target
        bool isRec() const { return recBegin || recEnd; }
    };
    struct SessUIState { String playing, pending; bool pendingStop = false; juce::int64 when = 0; };
    juce::int64 nextLaunchBoundary();
    void applySessionActions (juce::int64 pos);             // audio thread
    void scheduleSessionAction (SessionAction);
    void bridgeSlotAsync (ValueTree track, ValueTree clip); // ffmpeg in background, then relaunch
    static String bridgeKeyFor (const File& src);
    static File transcodeCacheFor (const String& key);
    static bool runFfmpegTranscode (const File& src, const File& dest);
    juce::SpinLock sessActLock;
    std::vector<SessionAction> sessActions;
    std::map<String, SessUIState> sessUI;                   // message thread only
    std::set<String> pendingBridges;                        // message thread only

    // ---- slot recording state (message thread owns; audio thread sees only
    //      the RecordSession via the source's rec atomic + punch actions) ----
    struct SlotRec
    {
        String trackUid, sceneUid;
        bool midi = false;
        std::unique_ptr<RecordSession> rs;                  // audio takes only
        juce::int64 startWhen = 0, stopWhen = -1;
    };
    std::map<String, std::unique_ptr<SlotRec>> slotRecs;    // by track uid
    std::atomic<int> slotMidiRec { 0 };                     // gates midi capture without global record
    void pollSlotRecs();                                    // hostedPump tick: finalize past punch-outs
    void finalizeSlotRec (SlotRec&);

    // ---- jam log (message thread): what played where, for capture ----
    struct JamEntry { String trackUid; ValueTree clip; juce::int64 start = 0, stop = -1; };
    std::vector<JamEntry> jamLog;
    void jamLogLaunch (const String& uid, ValueTree clip, juce::int64 when);
    void jamLogStop (const String& uid, juce::int64 when);

    // ---- recording ----
    void createRecordSessions();
    void finalizeRecordings();
    void finalizeAudioSession (RecordSession&);
    void finalizeMidiRecording();

    // ---- audio-thread helpers ----
    void applyAutomation (juce::int64 pos);
    void renderMetronome (juce::AudioBuffer<float>& buf, const TempoMap& map, juce::int64 pos);
    void automationParamChanged (int laneIdx, float v);
    void setPlayheadAtomics (juce::int64 pos, bool playing);

    void timerCallback() override;
    void handleAsyncUpdate() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    // message thread: run fn once the device callback has provably finished any
    // block that could still hold pointers we just retired (epoch handshake; a
    // wall-clock deadline covers a stalled/stopped device)
    void runAfterAudioDrain (std::function<void()> fn);
    void drainPoll (juce::uint64 targetEpoch, juce::uint32 deadlineMs, std::function<void()> fn);

    juce::PropertiesFile* appProps = nullptr;
    juce::TimeSliceThread diskThread { "dg disk" }, recordThread { "dg rec" };

    // graph bookkeeping (message thread)
    juce::AudioProcessorGraph::Node::Ptr audioInNode, audioOutNode, midiInNode;
    std::map<String, TrackNodes> trackNodes;
    std::map<String, juce::AudioProcessorGraph::Node::Ptr> insertNodes;     // by INSERT uid
    std::map<String, String> insertIdents;                                  // detect plugin swaps
    std::map<String, std::pair<String, std::shared_ptr<juce::AudioFormatReader>>> readerCache; // clipUid -> (path, reader)
    std::map<String, String> bridgeCache;                                   // src key -> readable path
    std::map<String, std::shared_ptr<const AudioPlaylist>> lastPlaylists;
    std::map<String, std::shared_ptr<const MidiPlaylist>> lastMidiPlaylists;
    std::vector<std::shared_ptr<const AudioPlaylist>> playlistGraveyard;
    std::vector<std::shared_ptr<const MidiPlaylist>> midiGraveyard;
    std::vector<std::shared_ptr<const TempoMap>> mapGraveyard;

    // tempo map (swapped, audio thread caches per callback)
    juce::SpinLock mapLock;
    std::shared_ptr<const TempoMap> pendingMap, rtMap;

    // bumped at entry of every device callback; serial callbacks mean observing
    // epoch E+2 proves any block that began before E+1 has returned
    std::atomic<juce::uint64> audioEpoch { 0 };

    // transport
    std::atomic<juce::int64> transportPos { 0 };
    std::atomic<bool> transportPlaying { false }, transportRecording { false }, recordPending { false };
    std::atomic<juce::int64> loopStartS { 0 }, loopEndS { 0 }, punchInS { 0 }, punchOutS { 0 };
    std::atomic<bool> loopOn { false }, punchOn { false }, metroOn { false };

    // segment context
    std::atomic<juce::int64> segPos { 0 };
    std::atomic<bool> segPlaying { false }, segRecEnabled { false };
    std::atomic<bool> flushMidiCurrent { false }, flushMidiPending { false };

    double currentSR = 48000.0;
    int currentBlock = 512;
    std::atomic<int> ioLatency { 0 };
    bool offline = false;

    juce::AudioBuffer<float> scratch;
    juce::HeapBlock<float*> segPtrs;
    juce::MidiBuffer midiIn, midiSeg;
    juce::MidiMessageCollector midiCollector;

    ClickVoice clicks[4];
    int nextClick = 0;

    // direct monitoring
    juce::SpinLock dmLock;
    std::vector<DirectMonitor> dmPending, dmRT;

    // recording
    std::vector<std::unique_ptr<RecordSession>> recordSessions;
    struct TimedMidi { juce::int64 pos; juce::MidiMessage msg; };
    juce::SpinLock midiRecLock;
    std::vector<TimedMidi> midiRecEvents;
    double recStartSec = 0.0;
    bool anyArmedMidiAtRecord = false;

    // automation
    juce::SpinLock laneLock;
    std::shared_ptr<const std::vector<LaneRT>> pendingLanes, rtLanes;
    std::vector<std::shared_ptr<const std::vector<LaneRT>>> laneGraveyard;
    std::vector<std::unique_ptr<LaneListener>> laneListeners;
    std::vector<ValueTree> laneTrees;
    std::atomic<int> applyingAutomation { 0 };      // counter: raised by the audio thread AND the hosted pump
    std::atomic<bool> automationActive { false };   // any read/touch lane with points
    juce::CriticalSection writeLock;                // producers only tryEnter (audio-thread reachable)
    struct WriteEvt { int laneIdx; juce::int64 t; float v; };
    std::vector<WriteEvt> writeEvents;              // capacity reserved up front; never grown by producers

    // modulation: everything modulates everything
    struct ModConn
    {
        juce::AudioProcessorParameter* param = nullptr;
        juce::AudioProcessorGraph::Node::Ptr owner;     // keeps the processor alive while the snapshot lives
        int src = 0;
        float amount = 0;
        std::shared_ptr<std::atomic<float>> base;
        String uid;
        bool hosted = false;                            // hosted plugin: param applied by the message-thread pump
        std::shared_ptr<std::atomic<float>> hostedVal, hostedApplied;
    };
    struct PatchSrc
    {
        PatcherProcessor* proc = nullptr;
        int idx = 0;
        juce::AudioProcessorGraph::Node::Ptr owner;     // keeps the patcher alive while the snapshot lives
        String id, label;
    };
    struct ModRTState
    {
        std::vector<ModConn> conns;
        std::vector<PatchSrc> patchSrcs;                // conn src >= 6 indexes here (src - 6)
        float lfoRate[4] { 1.0f, 0.5f, 2.0f, 4.0f };
        int lfoShape[4] {};
        float chaosRate = 1.0f;
        ChannelStripProcessor* follower = nullptr;
    };
    struct ModListener : juce::AudioProcessorParameter::Listener
    {
        AudioEngine& e;
        juce::AudioProcessorParameter* param;
        std::shared_ptr<std::atomic<float>> base;
        juce::AudioProcessorGraph::Node::Ptr owner;     // param must outlive removeListener below
        ModListener (AudioEngine& en, juce::AudioProcessorParameter* p, std::shared_ptr<std::atomic<float>> b,
                     juce::AudioProcessorGraph::Node::Ptr o)
            : e (en), param (p), base (std::move (b)), owner (std::move (o)) { param->addListener (this); }
        ~ModListener() override { param->removeListener (this); }
        void parameterValueChanged (int, float v) override
        {
            if (e.applyingAutomation.load() == 0) base->store (v);   // user moved the knob: new centre
        }
        void parameterGestureChanged (int, bool) override {}
    };

    juce::SpinLock modLock;
    std::shared_ptr<const ModRTState> pendingMods, rtMods;
    std::vector<std::shared_ptr<const ModRTState>> modGraveyard;
    std::vector<std::unique_ptr<ModListener>> modListeners;

    // hosted-plugin params: the VST3 controller side must not be poked from the
    // audio thread, so automation/mod values land in per-entry atomics and this
    // UI-rate pump delivers them on the message thread
    void flushHostedParams();
    struct HostedPump : juce::Timer
    {
        AudioEngine& e;
        explicit HostedPump (AudioEngine& en) : e (en) {}
        void timerCallback() override { e.flushHostedParams(); e.pollSlotRecs(); }
    };
    HostedPump hostedPump { *this };
    std::array<std::atomic<float>, 6> modSrcValues {};
    double lorenz[3] { 0.1, 0.0, 0.0 };
    float followEnv = 0.0f;

    // file preview: a second device callback that the device manager mixes in
    juce::AudioSourcePlayer previewPlayer;
    juce::AudioTransportSource previewTransport;
    std::unique_ptr<juce::AudioFormatReaderSource> previewSource;
    File previewFile;

    // ui midi + capture
    juce::SpinLock uiMidiLock;
    std::vector<juce::MidiMessage> uiMidi;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> masterCapture { nullptr };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> masterCaptureOwned;

    int rebuildFlags = 0;
    juce::StringArray playlistDirtyTracks, slotDirtyTracks;
    bool allPlaylistsDirty = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (AudioEngine)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace dg
