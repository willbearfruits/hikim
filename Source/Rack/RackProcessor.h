#pragma once
#include "RackModules.h"

namespace dg
{

// [RACK_NAME] - the corruption rack. A chain of 9 orderable, individually
// bypassable native destruction modules, each wet/dry mixable, with 4 macro
// knobs fanning out to any parameters and MIDI-CC mapping with learn.
// Insert-only and opt-in: with every module off, audio passes bit-identical.
class RackProcessor : public juce::AudioProcessor,
                      private juce::ValueTree::Listener,
                      private juce::AsyncUpdater
{
public:
    static constexpr int kNumModules = 9;
    static constexpr const char* kModuleIds[kNumModules]   = { "br", "sc", "gr", "bc", "fb", "gs", "dm", "tp", "cj" };
    static constexpr const char* kModuleNames[kNumModules] = {
        "BEAT REPEAT", "SCRAMBLER", "GRANULAR SHRED", "BITCRUSH",
        "FEEDBACK NET", "GATE SEQ", "DATA MANGLE", "TAPE", "CONVOLUTION JUNK"
    };

    RackProcessor();
    ~RackProcessor() override;

    // ---- AudioProcessor ----
    const String getName() const override { return names::rackName; }
    void prepareToPlay (double sr, int maxBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override        { return true; }
    bool acceptsMidi() const override      { return true; }
    bool producesMidi() const override     { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override          { return 1; }
    int getCurrentProgram() override       { return 0; }
    void setCurrentProgram (int) override  {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- rack-specific API (message thread unless noted) ----
    juce::AudioProcessorValueTreeState apvts;
    std::array<std::atomic<float>, 32> gateSteps;     // edited by UI, read by GateSeq

    juce::StringArray getOrder() const;               // module ids, processing order
    void moveModule (int fromSlot, int toSlot);

    void assignMacro (int macro, const String& paramID, float lo = 0.0f, float hi = 1.0f);
    void clearMacro (const String& paramID);
    struct MacroMap { int macro; String paramID; float lo, hi; };
    std::vector<MacroMap> getMacroMaps() const;

    void armMidiLearn (const String& paramID);        // next CC binds
    void clearMidiMap (const String& paramID);
    bool isLearning() const { return learnArmed.load(); }
    String getCCMapDescription (const String& paramID) const;

    void setIRFile (const File&, int mangleMode);
    File getIRFile() const;
    int getIRMangle() const;
    void markGateStepsDirty();                        // UI changed step values

    // factory gestures: moves, not fixers
    static juce::StringArray getFactoryGestureNames();
    void applyFactoryGesture (int index);
    bool saveUserRack (const File&);
    bool loadUserRack (const File&);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void rebuildRTState();                            // order + macro maps + cc maps -> RT copies
    void syncGateStepsToState();
    void loadGateStepsFromState();
    ValueTree extra() const;                          // EXTRA child of apvts.state
    void setReal (const String& paramID, float realValue);

    void valueTreePropertyChanged (ValueTree&, const Identifier&) override;
    void valueTreeChildAdded (ValueTree&, ValueTree&) override   { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (ValueTree&, ValueTree&, int) override { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (ValueTree&, int, int) override {}
    void valueTreeParentChanged (ValueTree&) override {}
    void handleAsyncUpdate() override;

    std::unique_ptr<RackModule> modules[kNumModules];
    std::atomic<float>* onParams[kNumModules] {};
    std::atomic<float>* mixParams[kNumModules] {};
    juce::SmoothedValue<float> mixSm[kNumModules];
    std::atomic<float>* macroParams[4] {};
    float lastMacro[4] { -1, -1, -1, -1 };

    struct RTMacro { int macro; juce::RangedAudioParameter* param; float lo, hi; };
    struct RTCC    { int cc; juce::RangedAudioParameter* param; };
    juce::SpinLock rtLock;
    std::vector<RTMacro> rtMacros;
    std::vector<RTCC> rtCCs;
    std::array<juce::int8, kNumModules> rtOrder {};

    std::atomic<bool> learnArmed { false };
    String learnParamID;                               // message thread sets before arming
    std::atomic<int> pendingLearnCC { -1 };

    juce::AudioBuffer<float> dry;
    double sr = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackProcessor)
};

} // namespace dg
