#pragma once
#include "../Model/Ids.h"
#include "../Engine/Taps.h"

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
        oOscIn, oOscOut, oModOut, oNumber, oChan, oStrip, oClock, oMaster,
        oSample, oGrain, oUnknown
    };
    // NODES.md object families (palette sections + box/cable colours)
    enum Family { famSource, famEffect, famMath, famTime, famRouting };

    // port types: 's' signal  'n' number  'e' event (NODES.md cable table);
    // inTypes/outTypes are one char per port
    struct Spec { const char* name; Obj type; int ins, outs; const char* defaults; const char* desc;
                  Family fam; const char* inTypes; const char* outTypes; };
    static const std::vector<Spec>& specs();
    static Obj parseType (const String& name);
    static const Spec* specFor (const String& name);

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

    // ---- number boxes: live value per node uid (editor writes, DSP reads) ----
    std::shared_ptr<std::atomic<float>> numberValueFor (const String& nodeUid, float initial = 0.0f)
    {
        auto& slot = numberVals[nodeUid];
        if (slot == nullptr) slot = std::make_shared<std::atomic<float>> (initial);
        return slot;
    }

    // ---- modout taps: this patcher as a mod source in the PATCH bay ----
    static constexpr int kMaxModOuts = 8;
    int getNumModOuts() const                { return numModOuts.load(); }
    float modOut (int i) const               { return modOutVals[(size_t) juce::jlimit (0, kMaxModOuts - 1, i)].load(); }
    std::function<void()> onModOutsChanged;  // engine refreshes its mod-source list

    // ---- chan~: the engine resolves a typed track ref ("2", "DRUMS", "master")
    // to that channel strip's tap ring; tests inject fakes. Setting it recompiles
    // so refs re-resolve after every graph rebuild.
    using ChanTapProvider = std::function<std::shared_ptr<ChanTap> (const String& ref, bool pre)>;
    void setChanTapProvider (ChanTapProvider f) { chanTapProvider = std::move (f); compile(); }
    std::shared_ptr<ChanTap> chanTapForNode (const String& nodeUid) const   // editor meter faces
    {
        auto it = chanTaps.find (nodeUid);
        return it != chanTaps.end() ? it->second : nullptr;
    }

    // ---- strip: same resolution story, but for driving gain/pan/mute ----
    using StripCtlProvider = std::function<std::shared_ptr<StripControl> (const String& ref)>;
    void setStripCtlProvider (StripCtlProvider f) { stripCtlProvider = std::move (f); compile(); }

    // ---- sample~: the engine resolves an absolute path to a cached buffer
    // (always through AudioEngine::createAnyReader); tests inject fakes.
    using SampleProvider = std::function<std::shared_ptr<const SampleBuf> (const String& path)>;
    void setSampleProvider (SampleProvider f) { sampleProvider = std::move (f); compile(); }
    std::shared_ptr<const SampleBuf> sampleBufForNode (const String& nodeUid) const  // waveform faces
    {
        auto it = sampleBufs.find (nodeUid);
        return it != sampleBufs.end() ? it->second : nullptr;
    }

    // ---- master~: rings this patch injects into the master bus. Rings are
    // stable per node uid across recompiles, so the set only changes when
    // master~ objects are added/removed — that's when onInjectsChanged fires
    // (the engine re-gathers; firing per compile would loop the rebuild).
    std::vector<std::shared_ptr<InjectRing>> getInjectRings() const
    {
        std::vector<std::shared_ptr<InjectRing>> v;
        for (const auto& [uid, ring] : injectRings) v.push_back (ring);
        return v;
    }
    std::function<void()> onInjectsChanged;

private:
    struct PObj
    {
        int type = oUnknown;
        float a = 0, b = 0, c = 0;
        int in0 = -1, in1 = -1, in2 = -1;
        int out0 = -1, out1 = -1, out2 = -1, out3 = -1;
        double ph = 0;
        float z1 = 0, z2 = 0, held = 0, lastTrig = 0;
        std::vector<float> line;
        int wp = 0;
        juce::Random rng;
        std::shared_ptr<std::atomic<float>> ext;     // oscin value / oscout tap
        std::shared_ptr<ChanTap> tap;                // chan~ source ring
        std::shared_ptr<StripControl> ctl;           // strip target
        std::shared_ptr<InjectRing> inj;             // master~ ring
        std::shared_ptr<const SampleBuf> smp;        // sample~ / grain~ buffer
        bool sampleLoop = false, samplePlaying = false;
        struct Grain { double pos = 0, inc = 0; int remain = 0, dur = 1; float gl = 0, gr = 0; };
        std::vector<Grain> grains;                   // grain~ voice pool
        double spawnAcc = 0;
        std::atomic<float>* hostParam = nullptr;
        int modIdx = -1;                             // modout slot
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

    std::array<std::atomic<float>, kMaxModOuts> modOutVals {};
    std::atomic<int> numModOuts { 0 };
    std::map<String, std::shared_ptr<std::atomic<float>>> numberVals;   // message thread map
    ChanTapProvider chanTapProvider;
    StripCtlProvider stripCtlProvider;
    SampleProvider sampleProvider;
    std::map<String, std::shared_ptr<ChanTap>> chanTaps;                // node uid -> resolved ring
    std::map<String, std::shared_ptr<InjectRing>> injectRings;          // node uid -> master~ ring
    std::map<String, std::shared_ptr<const SampleBuf>> sampleBufs;      // node uid -> loaded buffer

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
