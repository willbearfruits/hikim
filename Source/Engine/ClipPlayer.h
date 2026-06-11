#pragma once
#include "Processors.h"

namespace dg
{

class AudioEngine;

// Immutable RT snapshot of one audio track's audible clips (lane 0 only).
struct AudioClipRT
{
    juce::int64 start = 0, length = 0;        // engine samples on the timeline
    double offset = 0;                         // source-file samples
    float gain = 1.0f;
    juce::int64 fadeIn = 0, fadeOut = 0;       // engine samples (manual, linear)
    juce::int64 xfadeIn = 0, xfadeOut = 0;     // auto comp crossfades (equal-power)
    double ratio = 1.0;                        // file samples consumed per engine sample
    std::shared_ptr<juce::AudioFormatReader> reader;
    int numFileChannels = 2;
    juce::int64 fileLength = 0;
};

struct AudioPlaylist { std::vector<AudioClipRT> clips; };

// One live recording pass (file shared across loop passes; takes sliced on stop).
struct RecordSession
{
    String trackUid;
    File file;
    double sampleRate = 48000.0;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    std::atomic<juce::int64> written { 0 };

    struct PassMark { juce::int64 fileOffset; double timelineStartSec; };
    std::vector<PassMark> passes;              // audio-thread push within reserved capacity
    std::atomic<bool> needPassMark { true };

    // live waveform for the timeline: audio thread fills, UI reads [0, peakCount)
    juce::HeapBlock<float> peaks;
    std::atomic<int> peakCount { 0 };
    int peakCapacity = 0, samplesPerPeak = 1024;
    float peakAccum = 0.0f;
    int peakAccumCount = 0;
};

// Audio-track source node: plays timeline clips, captures input for recording,
// and feeds input into the insert chain when monitoring "through chain".
class ClipPlayerProcessor : public BasicProcessor
{
public:
    ClipPlayerProcessor (AudioEngine& e, const String& trackUid);

    void prepareToPlay (double sr, int maxBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    void setPlaylist (std::shared_ptr<const AudioPlaylist> p);

    std::atomic<int>  monitorMode { 0 };       // 0 off, 1 direct (engine handles), 2 through chain
    std::atomic<bool> armed { false };
    std::atomic<RecordSession*> rec { nullptr };

private:
    void renderClip (const AudioClipRT& c, juce::AudioBuffer<float>& out, juce::int64 blockStart, int n);

    AudioEngine& engine;
    String trackUid;

    juce::SpinLock lock;
    std::shared_ptr<const AudioPlaylist> pending, rt;   // rt only touched on audio thread

    juce::AudioBuffer<float> fileTemp, inputCopy;
};

} // namespace dg
