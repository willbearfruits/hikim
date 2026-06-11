#pragma once
#include "../Model/Session.h"
#include "TempoMap.h"
#include "Processors.h"
#include "ClipPlayer.h"
#include "MidiSource.h"
#include "StretchCache.h"

namespace dg
{

class PluginHost;

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

    // ---- ui midi tap (step input, midi indicators) ----
    std::vector<juce::MidiMessage> drainUiMidi();

    // ---- automation write drain (UI flushes into the tree) ----
    struct AutoWrite { ValueTree lane; double tSec; float v; };
    std::vector<AutoWrite> drainAutomationWrites();

    // ---- live recording readout for the timeline (message thread) ----
    const RecordSession* getLiveRecording (const String& trackUid) const;
    double getRecordStartSeconds() const { return recStartSec; }

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
        int mode = 0;                                   // 0 off, 1 read, 2 touch, 3 write
        std::vector<std::pair<juce::int64, float>> pts; // (samples, normalised)
        std::shared_ptr<std::atomic<bool>> touching;
    };

    struct LaneListener : juce::AudioProcessorParameter::Listener
    {
        AudioEngine& e; int laneIdx; std::shared_ptr<std::atomic<bool>> touching;
        juce::AudioProcessorParameter* param;
        LaneListener (AudioEngine& en, int idx, juce::AudioProcessorParameter* p,
                      std::shared_ptr<std::atomic<bool>> t)
            : e (en), laneIdx (idx), touching (std::move (t)), param (p) { param->addListener (this); }
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
    void scheduleRebuild (int what);

    void connectChain (const std::vector<juce::AudioProcessorGraph::Node::Ptr>& nodes);
    juce::AudioProcessorGraph::Node::Ptr getBusHead (const String& busUid);
    void instantiateInsert (const ValueTree& insert, double sr, int blockSize);

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

    juce::PropertiesFile* appProps = nullptr;
    juce::TimeSliceThread diskThread { "dg disk" }, recordThread { "dg rec" };

    // graph bookkeeping (message thread)
    juce::AudioProcessorGraph::Node::Ptr audioInNode, audioOutNode, midiInNode;
    std::map<String, TrackNodes> trackNodes;
    std::map<String, juce::AudioProcessorGraph::Node::Ptr> insertNodes;     // by INSERT uid
    std::map<String, String> insertIdents;                                  // detect plugin swaps
    std::map<String, std::pair<String, std::shared_ptr<juce::AudioFormatReader>>> readerCache; // clipUid -> (path, reader)
    std::map<String, std::shared_ptr<const AudioPlaylist>> lastPlaylists;
    std::map<String, std::shared_ptr<const MidiPlaylist>> lastMidiPlaylists;
    std::vector<std::shared_ptr<const AudioPlaylist>> playlistGraveyard;
    std::vector<std::shared_ptr<const MidiPlaylist>> midiGraveyard;
    std::vector<std::shared_ptr<const TempoMap>> mapGraveyard;

    // tempo map (swapped, audio thread caches per callback)
    juce::SpinLock mapLock;
    std::shared_ptr<const TempoMap> pendingMap, rtMap;

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
    std::atomic<bool> applyingAutomation { false };
    std::atomic<bool> automationActive { false };   // any read/touch lane with points
    juce::CriticalSection writeLock;
    struct WriteEvt { int laneIdx; juce::int64 t; float v; };
    std::vector<WriteEvt> writeEvents;

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
    juce::StringArray playlistDirtyTracks;
    bool allPlaylistsDirty = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (AudioEngine)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace dg
