#pragma once
#include "../Model/Ids.h"
#include "Taps.h"

namespace dg
{

// Boilerplate base for the engine's internal graph nodes.
class BasicProcessor : public juce::AudioProcessor
{
public:
    BasicProcessor (const String& name, bool wantsMidiIn = false, bool givesMidiOut = false)
        : AudioProcessor (BusesProperties()
                              .withInput ("In", juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
          procName (name), midiIn (wantsMidiIn), midiOut (givesMidiOut) {}

    const String getName() const override               { return procName; }
    bool acceptsMidi() const override                   { return midiIn; }
    bool producesMidi() const override                  { return midiOut; }
    double getTailLengthSeconds() const override        { return 0.0; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override                     { return false; }
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const String getProgramName (int) override          { return {}; }
    void changeProgramName (int, const String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override   {}
    void prepareToPlay (double, int) override           {}
    void releaseResources() override                    {}

private:
    String procName;
    bool midiIn, midiOut;
};

// ---------------------------------------------------------------------------
// Per-track channel strip: gain / pan / mute as real automatable parameters,
// solo-mute decided globally by the engine, peak meters for the mixer.
class ChannelStripProcessor : public BasicProcessor
{
public:
    ChannelStripProcessor (const String& trackName);

    void prepareToPlay (double sr, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioParameterFloat* gainDb = nullptr;
    juce::AudioParameterFloat* pan    = nullptr;
    juce::AudioParameterBool*  mute   = nullptr;

    std::atomic<bool>  soloMuted { false };     // set by engine from global solo state
    std::atomic<bool>  forceMute { false };     // used by the stem renderer
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };

    // published every block for WIRES chan~ (shared so patch programs outlive rebuilds)
    std::shared_ptr<ChanTap> tapPre  = std::make_shared<ChanTap>();
    std::shared_ptr<ChanTap> tapPost = std::make_shared<ChanTap>();
    // WIRES `strip` drives gain/pan/mute through here while its stamps stay fresh
    std::shared_ptr<StripControl> control = std::make_shared<StripControl>();

    // WIRES master~ rings, consumed pre-fader (the master strip only). The
    // message thread swaps the list and parks the old ones in injKeep so the
    // audio thread only ever dereferences — it never copies or frees.
    using InjectList = std::vector<std::shared_ptr<InjectRing>>;
    void setInjects (std::shared_ptr<const InjectList> v)
    {
        injKeep[(size_t) (injKeepIdx++ & 31)] = v;
        injects.store (v != nullptr && v->empty() ? nullptr : v.get());
    }

private:
    std::atomic<const InjectList*> injects { nullptr };
    std::shared_ptr<const InjectList> injKeep[32];
    int injKeepIdx = 0;

    juce::SmoothedValue<float> smGainL, smGainR;
};

// Post-fader send with one level parameter.
class SendProcessor : public BasicProcessor
{
public:
    SendProcessor (const String& name);
    void prepareToPlay (double sr, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioParameterFloat* levelDb = nullptr;

private:
    juce::SmoothedValue<float> sm;
};

// ---------------------------------------------------------------------------
// Built-in fallback instrument so MIDI tracks make sound before a VSTi is set.
class SimpleSynthProcessor : public BasicProcessor
{
public:
    SimpleSynthProcessor();
    void prepareToPlay (double sr, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    juce::Synthesiser synth;
};

// Native instruments so MIDI tracks have voices without any plugins installed:
//   RUST   - 2-op FM bell/metal     GRAVEL - noise percussion with pitch thump
//   HYMN   - detuned-saw pad        RUBBLE - drum kit (kick/snare/clap/hats)
class BuiltinInstrument : public BasicProcessor
{
public:
    enum class Kind { rust, gravel, hymn, kit };
    explicit BuiltinInstrument (Kind k);
    static std::unique_ptr<juce::AudioProcessor> create (const String& name);  // "rust" | "gravel" | "hymn"

    void prepareToPlay (double sr, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    Kind kind;
    juce::AudioParameterFloat *p1 = nullptr, *p2 = nullptr, *p3 = nullptr, *p4 = nullptr;

private:
    juce::Synthesiser synth;
};

// Placeholder that keeps the saved state of a plugin that failed to load,
// passing audio through untouched. // EXTEND: offer "relink plugin" UI.
class MissingPluginProcessor : public BasicProcessor
{
public:
    MissingPluginProcessor (const String& missingName, juce::MemoryBlock savedState)
        : BasicProcessor ("MISSING: " + missingName, true, true), state (std::move (savedState)) {}

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    void getStateInformation (juce::MemoryBlock& mb) override { mb = state; }

    juce::MemoryBlock state;
};

} // namespace dg
