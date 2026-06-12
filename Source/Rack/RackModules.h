#pragma once
#include "../Common.h"

namespace dg
{

// Block context handed to every module: tempo-synced modules read ppq/bpm,
// and free-run on their own clocks when the transport is stopped.
struct ModuleContext
{
    double sr = 48000.0;
    double bpm = 120.0;
    double ppq = 0.0;               // quarter notes at block start
    double beatsPerBar = 4.0;
    double barStartPpq = 0.0;
    bool   playing = false;
};

class RackModule
{
public:
    explicit RackModule (juce::AudioProcessorValueTreeState& s) : apvts (s) {}
    virtual ~RackModule() = default;

    virtual void prepare (double sr, int maxBlock) = 0;
    virtual void reset() {}
    virtual void process (juce::AudioBuffer<float>&, const ModuleContext&) = 0;   // stereo, in place

protected:
    std::atomic<float>* raw (const String& paramID) const { return apvts.getRawParameterValue (paramID); }
    juce::AudioProcessorValueTreeState& apvts;
    juce::Random rng;
};

// 1 -- tempo-synced capture & retrigger: division, chance, ratchet, pitch-per-repeat, decay
class BeatRepeatModule : public RackModule
{
public:
    explicit BeatRepeatModule (juce::AudioProcessorValueTreeState&);
    void prepare (double, int) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const ModuleContext&) override;

private:
    std::atomic<float> *pDiv, *pChance, *pRatchet, *pPitch, *pDecay;
    juce::AudioBuffer<float> ring;
    int wp = 0, mask = 0;
    double divPhase = 0, freePpq = 0;
    bool repeating = false;
    int capStart = 0, repIndex = 0;
    double chunkPos = 0, readFrac = 0, readInc = 1.0;
    float repGain = 1.0f, repMix = 0.0f;       // engage/release crossfade (declick)
};

// 2 -- the breakcore core: bar-length buffer sliced on a grid, slices
// shuffled / reversed / dropped / pitch-sprayed by per-bar seeded pattern
class ScramblerModule : public RackModule
{
public:
    explicit ScramblerModule (juce::AudioProcessorValueTreeState&);
    void prepare (double, int) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const ModuleContext&) override;

private:
    std::atomic<float> *pGrid, *pShuffle, *pReverse, *pDrop, *pSpray, *pSeed;
    juce::AudioBuffer<float> ring;
    int ringLen = 0;
    double freePpq = 0;
    int curSlice = -1, srcSlice = 0;
    bool sliceRev = false, sliceDrop = false;
    double sliceRate = 1.0, readPos = 0;
};

// 3 -- granular shred over a rolling history buffer
class GranularModule : public RackModule
{
public:
    explicit GranularModule (juce::AudioProcessorValueTreeState&);
    void prepare (double, int) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const ModuleContext&) override;

private:
    std::atomic<float> *pSize, *pDensity, *pJitter, *pSpray, *pReverse, *pFreeze;
    juce::AudioBuffer<float> ring;
    int wp = 0, mask = 0;
    double spawnTimer = 0;
    struct Grain { double pos, inc; int age, len; float gain; bool active; };
    Grain grains[48] {};
};

// 4 -- bit depth, downsampling, drive, dither/anti-dither
class BitcrushModule : public RackModule
{
public:
    explicit BitcrushModule (juce::AudioProcessorValueTreeState&);
    void prepare (double, int) override {}
    void process (juce::AudioBuffer<float>&, const ModuleContext&) override;

private:
    std::atomic<float> *pBits, *pDown, *pDrive, *pDither;
    float held[2] {};
    int holdCount[2] {};
};

// 5 -- 3-line cross-coupled feedback delay; tunable, allowed to scream,
// tanh-clamped in the loop so it never destroys the master
class FeedbackNetModule : public RackModule
{
public:
    explicit FeedbackNetModule (juce::AudioProcessorValueTreeState&);
    void prepare (double, int) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const ModuleContext&) override;

private:
    std::atomic<float> *pTime, *pSpread, *pFeedback, *pDamp, *pTuned, *pPitch;
    juce::AudioBuffer<float> lines;     // 3 delay lines
    int wp = 0, mask = 0;
    float lpState[3] {};
    juce::SmoothedValue<float> delaySm[3];
};

// 6 -- step-sequenced rhythmic gate; step depths live in the rack state
class GateSeqModule : public RackModule
{
public:
    GateSeqModule (juce::AudioProcessorValueTreeState&, std::array<std::atomic<float>, 32>& stepValues);
    void prepare (double, int) override;
    void process (juce::AudioBuffer<float>&, const ModuleContext&) override;

private:
    std::atomic<float> *pRate, *pDepth, *pSmooth, *pSwing, *pSteps;
    std::array<std::atomic<float>, 32>& steps;
    double freePpq = 0;
    float env = 1.0f;
};

// 7 -- sample & hold, dropouts, glitch holds, bit-flips: data damage
class DataMangleModule : public RackModule
{
public:
    explicit DataMangleModule (juce::AudioProcessorValueTreeState&);
    void prepare (double, int) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const ModuleContext&) override;

private:
    std::atomic<float> *pShRate, *pShAmt, *pDrop, *pHold, *pFlip;
    float shVal[2] {};
    double shCount = 0;
    int winCount = 0, winLen = 480;
    bool winMuted = false;
    juce::AudioBuffer<float> holdBuf;
    int holdLen = 0, holdPos = 0, holdRepeats = 0;
};

// 8 -- the sentimental dirt: wow, flutter, saturation, hiss, age
class TapeModule : public RackModule
{
public:
    explicit TapeModule (juce::AudioProcessorValueTreeState&);
    void prepare (double, int) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const ModuleContext&) override;

private:
    std::atomic<float> *pWow, *pFlutter, *pSat, *pHiss, *pAge;
    juce::AudioBuffer<float> line;
    int wp = 0, mask = 0;
    double wowPhase = 0, flutPhase = 0, t = 0;
    float flutNoise = 0, lpState[2] {}, hissLp = 0;
    float dipGain = 1.0f, dipTarget = 1.0f;
    int dipCount = 0;
};

// 9 -- impulse abuse: any audio file as IR, optionally mangled before load
class ConvolutionJunkModule : public RackModule
{
public:
    explicit ConvolutionJunkModule (juce::AudioProcessorValueTreeState&);
    void prepare (double, int) override;
    void reset() override;
    void process (juce::AudioBuffer<float>&, const ModuleContext&) override;

    void loadIR (const File&, int mangleMode);   // message thread; safe vs processing
    // EXTEND: deeper IR abuse - spectral smear, IR feedback re-capture, live IR from the timeline

private:
    juce::dsp::Convolution conv { juce::dsp::Convolution::NonUniform { 512 } };
    juce::AudioFormatManager irFormats;
    double sr = 48000.0;
    std::atomic<bool> irLoaded { false };
};

} // namespace dg
