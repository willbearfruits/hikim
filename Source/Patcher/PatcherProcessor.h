#pragma once
#include "../Model/Ids.h"

namespace dg
{

// WIRES - the Max layer as a device. A patch of typed objects evaluated at
// audio rate, living as an insert in any chain. Objects:
//   adc~ dac~ osc~ phasor~ noise~ lfo~ *~ +~ lores~ hipass~ delay~ tanh~
//   sah~ env~ metro random scale sig param oscin oscout
// Feedback is legal through delay~ (its write happens after the whole pass,
// so loops cost at least one block - clamp lives in the object).
// P1..P8 are host parameters: automatable, MOD-patchable, surfaced by `param N`.
// EXTEND: midiin objects, abstractions (patch-in-patch), live object help.
class PatcherProcessor : public juce::AudioProcessor,
                         public juce::ValueTree::Listener,
                         private juce::AsyncUpdater,
                         private juce::Timer
{
public:
    enum Obj
    {
        oAdc, oDac, oOsc, oPhasor, oNoise, oLfo, oMul, oAdd, oLores, oHipass,
        oDelay, oTanh, oSah, oEnv, oMetro, oRandom, oScale, oSig, oParam,
        oOscIn, oOscOut, oUnknown
    };
    struct Spec { const char* name; Obj type; int ins, outs; const char* defaults; const char* desc; };
    static const std::vector<Spec>& specs();
    static Obj parseType (const String& name);

    PatcherProcessor();
    ~PatcherProcessor() override;

    // ---- AudioProcessor ----
    const String getName() const override { return names::patcherName; }
    void prepareToPlay (double sr, int maxBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override   { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- patch model (message thread; editor edits this directly) ----
    ValueTree patch { "WIRESPATCH" };
    ValueTree addNode (const String& typedText, int x, int y);   // "osc~ 220"
    void removeNode (const ValueTree& node);
    void addCable (const String& srcUid, int srcPort, const String& dstUid, int dstPort);

    juce::AudioParameterFloat* hostParams[8] {};

private:
    struct PObj
    {
        int type = oUnknown;
        float a = 0, b = 0, c = 0;
        int in0 = -1, in1 = -1;
        int out0 = -1, out1 = -1;
        double ph = 0;
        float z1 = 0, z2 = 0, held = 0, lastTrig = 0;
        std::vector<float> line;
        int wp = 0;
        juce::Random rng;
        std::shared_ptr<std::atomic<float>> ext;     // oscin value / oscout tap
        std::atomic<float>* hostParam = nullptr;
    };
    struct Program
    {
        std::vector<PObj> objs;
        juce::AudioBuffer<float> bufs;
        int numBufs = 0;
        bool hasDac = false;
        double sr = 48000.0;
    };

    void compile();
    void rebuildOsc (Program&);
    void timerCallback() override;                   // oscout sends
    void handleAsyncUpdate() override { compile(); }
    void valueTreePropertyChanged (ValueTree&, const Identifier&) override { triggerAsyncUpdate(); }
    void valueTreeChildAdded (ValueTree&, ValueTree&) override   { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (ValueTree&, ValueTree&, int) override { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (ValueTree&, int, int) override {}
    void valueTreeParentChanged (ValueTree&) override {}

    juce::SpinLock progLock;
    std::shared_ptr<Program> pendingProg, rtProg;
    std::vector<std::shared_ptr<Program>> graveyard;

    double sampleRate = 48000.0;
    int blockSize = 512;

    // OSC plumbing (message thread)
    struct OscInBinding : juce::OSCReceiver::ListenerWithOSCAddress<juce::OSCReceiver::MessageLoopCallback>
    {
        std::shared_ptr<std::atomic<float>> val;
        void oscMessageReceived (const juce::OSCMessage& m) override
        {
            if (m.size() > 0)
            {
                if (m[0].isFloat32())     val->store (m[0].getFloat32());
                else if (m[0].isInt32())  val->store ((float) m[0].getInt32());
            }
        }
    };
    struct OscOutBinding { std::unique_ptr<juce::OSCSender> sender; String addr; std::shared_ptr<std::atomic<float>> tap; float last = -1.0e9f; };
    std::map<int, std::unique_ptr<juce::OSCReceiver>> receivers;
    std::vector<std::unique_ptr<OscInBinding>> inBindings;
    std::vector<OscOutBinding> outBindings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatcherProcessor)
};

} // namespace dg
