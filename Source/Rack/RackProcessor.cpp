#include "RackProcessor.h"
#include "RackEditor.h"

namespace dg
{

static const Identifier kExtraId ("EXTRA");
static const Identifier kOrderProp ("order");
static const Identifier kStepsProp ("gatesteps");
static const Identifier kIRFileProp ("irfile");
static const Identifier kIRMangleProp ("irmangle");
static const Identifier kMacroMapId ("MACROMAP");
static const Identifier kCCMapId ("CCMAP");
static const Identifier kMacroProp ("macro");
static const Identifier kParamProp ("paramid");
static const Identifier kLoProp ("lo");
static const Identifier kHiProp ("hi");
static const Identifier kCCProp ("cc");

juce::AudioProcessorValueTreeState::ParameterLayout RackProcessor::createLayout()
{
    using P = juce::AudioProcessorValueTreeState::ParameterLayout;
    using F = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    using C = juce::AudioParameterChoice;
    P layout;
    auto add = [&] (auto* p) { layout.add (std::unique_ptr<juce::RangedAudioParameter> (p)); };
    auto range = [] (float lo, float hi, float step = 0.0f, float skew = 1.0f)
    {
        juce::NormalisableRange<float> r (lo, hi, step);
        r.setSkewForCentre (lo + (hi - lo) * (skew == 1.0f ? 0.5f : skew));
        return r;
    };

    for (int m = 0; m < kNumModules; ++m)
    {
        const String p (kModuleIds[m]);
        add (new B ({ p + "_on", 1 }, String (kModuleNames[m]) + " On", false));
        add (new F ({ p + "_mix", 1 }, String (kModuleNames[m]) + " Mix", range (0.0f, 1.0f), 1.0f));
    }

    add (new C ({ "br_div", 1 }, "Division", { "1/4", "1/4T", "1/8", "1/8T", "1/8.", "1/16", "1/16T", "1/32", "1/64" }, 5));
    add (new F ({ "br_chance", 1 }, "Chance", range (0.0f, 1.0f), 0.4f));
    add (new F ({ "br_ratchet", 1 }, "Ratchet", range (1.0f, 4.0f, 1.0f), 1.0f));
    add (new F ({ "br_pitch", 1 }, "Pitch/Rep", range (-12.0f, 12.0f, 0.1f), 0.0f));
    add (new F ({ "br_decay", 1 }, "Decay", range (0.3f, 1.0f), 0.85f));

    add (new C ({ "sc_grid", 1 }, "Grid", { "4", "8", "16", "32" }, 2));
    add (new F ({ "sc_shuffle", 1 }, "Shuffle", range (0.0f, 1.0f), 0.5f));
    add (new F ({ "sc_reverse", 1 }, "Reverse", range (0.0f, 1.0f), 0.2f));
    add (new F ({ "sc_drop", 1 }, "Drop", range (0.0f, 1.0f), 0.1f));
    add (new F ({ "sc_spray", 1 }, "Pitch Spray", range (0.0f, 12.0f), 0.0f));
    add (new F ({ "sc_seed", 1 }, "Pattern", range (0.0f, 32.0f, 1.0f), 0.0f));

    add (new F ({ "gr_size", 1 }, "Size ms", range (5.0f, 400.0f, 0.1f, 0.3f), 80.0f));
    add (new F ({ "gr_density", 1 }, "Density", range (0.5f, 80.0f, 0.1f, 0.3f), 18.0f));
    add (new F ({ "gr_jitter", 1 }, "Jitter", range (0.0f, 1.0f), 0.3f));
    add (new F ({ "gr_spray", 1 }, "Pitch Spray", range (0.0f, 24.0f), 4.0f));
    add (new F ({ "gr_reverse", 1 }, "Reverse", range (0.0f, 1.0f), 0.25f));
    add (new B ({ "gr_freeze", 1 }, "Freeze", false));

    add (new F ({ "bc_bits", 1 }, "Bits", range (1.0f, 16.0f, 0.1f), 8.0f));
    add (new F ({ "bc_down", 1 }, "Downsample", range (1.0f, 64.0f, 1.0f, 0.25f), 4.0f));
    add (new F ({ "bc_drive", 1 }, "Drive dB", range (0.0f, 36.0f), 0.0f));
    add (new F ({ "bc_dither", 1 }, "Dither", range (-1.0f, 1.0f), 0.0f));

    add (new F ({ "fb_time", 1 }, "Time ms", range (1.0f, 500.0f, 0.1f, 0.3f), 90.0f));
    add (new F ({ "fb_spread", 1 }, "Spread", range (1.0f, 2.0f), 1.31f));
    add (new F ({ "fb_fb", 1 }, "Feedback", range (0.0f, 1.15f), 0.5f));
    add (new F ({ "fb_damp", 1 }, "Damp Hz", range (200.0f, 18000.0f, 1.0f, 0.3f), 4000.0f));
    add (new B ({ "fb_tuned", 1 }, "Tuned", false));
    add (new F ({ "fb_pitch", 1 }, "Pitch", range (24.0f, 84.0f, 1.0f), 48.0f));

    add (new C ({ "gs_rate", 1 }, "Rate", { "1/4", "1/8", "1/16", "1/32" }, 2));
    add (new F ({ "gs_depth", 1 }, "Depth", range (0.0f, 1.0f), 1.0f));
    add (new F ({ "gs_smooth", 1 }, "Smooth ms", range (0.5f, 50.0f, 0.1f, 0.3f), 4.0f));
    add (new F ({ "gs_swing", 1 }, "Swing", range (0.0f, 0.75f), 0.0f));
    add (new F ({ "gs_steps", 1 }, "Steps", range (2.0f, 32.0f, 1.0f), 16.0f));

    add (new F ({ "dm_shrate", 1 }, "S&H Hz", range (50.0f, 15000.0f, 1.0f, 0.3f), 2000.0f));
    add (new F ({ "dm_shamt", 1 }, "S&H Amt", range (0.0f, 1.0f), 0.5f));
    add (new F ({ "dm_drop", 1 }, "Dropout", range (0.0f, 1.0f), 0.15f));
    add (new F ({ "dm_hold", 1 }, "Glitch Hold", range (0.0f, 1.0f), 0.1f));
    add (new F ({ "dm_flip", 1 }, "Bit Flip", range (0.0f, 1.0f), 0.0f));

    add (new F ({ "tp_wow", 1 }, "Wow", range (0.0f, 1.0f), 0.25f));
    add (new F ({ "tp_flutter", 1 }, "Flutter", range (0.0f, 1.0f), 0.15f));
    add (new F ({ "tp_sat", 1 }, "Saturation", range (0.0f, 24.0f), 4.0f));
    add (new F ({ "tp_hiss", 1 }, "Hiss", range (0.0f, 1.0f), 0.1f));
    add (new F ({ "tp_age", 1 }, "Age", range (0.0f, 1.0f), 0.3f));

    add (new C ({ "cj_mangle", 1 }, "IR Mangle", { "Clean", "Reverse", "Crush", "Chop" }, 0));

    for (int i = 1; i <= 4; ++i)
        add (new F ({ "macro" + String (i), 1 }, "Macro " + String (i), range (0.0f, 1.0f), 0.0f));

    return layout;
}

RackProcessor::RackProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("In", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto& s : gateSteps) s.store (1.0f);
    for (int i = 0; i < 32; i += 4) gateSteps[(size_t) i].store (1.0f);
    for (int i = 2; i < 32; i += 4) gateSteps[(size_t) i].store (0.35f);

    modules[0] = std::make_unique<BeatRepeatModule> (apvts);
    modules[1] = std::make_unique<ScramblerModule> (apvts);
    modules[2] = std::make_unique<GranularModule> (apvts);
    modules[3] = std::make_unique<BitcrushModule> (apvts);
    modules[4] = std::make_unique<FeedbackNetModule> (apvts);
    modules[5] = std::make_unique<GateSeqModule> (apvts, gateSteps);
    modules[6] = std::make_unique<DataMangleModule> (apvts);
    modules[7] = std::make_unique<TapeModule> (apvts);
    modules[8] = std::make_unique<ConvolutionJunkModule> (apvts);

    for (int m = 0; m < kNumModules; ++m)
    {
        onParams[m]  = apvts.getRawParameterValue (String (kModuleIds[m]) + "_on");
        mixParams[m] = apvts.getRawParameterValue (String (kModuleIds[m]) + "_mix");
        rtOrder[(size_t) m] = (juce::int8) m;
    }
    for (int i = 0; i < 4; ++i)
        macroParams[i] = apvts.getRawParameterValue ("macro" + String (i + 1));

    apvts.state.addListener (this);
    rebuildRTState();
}

RackProcessor::~RackProcessor()
{
    apvts.state.removeListener (this);
}

ValueTree RackProcessor::extra() const
{
    ValueTree state = apvts.state;          // ValueTree is ref-counted; safe to mutate via copy
    return state.getOrCreateChildWithName (kExtraId, nullptr);
}

void RackProcessor::prepareToPlay (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    dry.setSize (2, maxBlock);
    for (auto& m : modules) m->prepare (sampleRate, maxBlock);
    for (auto& s : mixSm) s.reset (sampleRate, 0.03);

    auto ex = extra();
    if (ex.hasProperty (kIRFileProp))
        static_cast<ConvolutionJunkModule*> (modules[8].get())
            ->loadIR (File (ex[kIRFileProp].toString()), getIRMangle());
}

void RackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    ModuleContext ctx;
    ctx.sr = sr;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (pos->getBpm())          ctx.bpm = *pos->getBpm();
            if (pos->getPpqPosition())  ctx.ppq = *pos->getPpqPosition();
            if (pos->getPpqPositionOfLastBarStart()) ctx.barStartPpq = *pos->getPpqPositionOfLastBarStart();
            if (auto ts = pos->getTimeSignature())   ctx.beatsPerBar = ts->numerator * 4.0 / juce::jmax (1, ts->denominator);
            ctx.playing = pos->getIsPlaying();
        }

    // MIDI: learn + CC maps (corruption is performable from a controller)
    {
        juce::SpinLock::ScopedTryLockType tl (rtLock);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (! msg.isController()) continue;
            const int cc = msg.getControllerNumber();
            const float v = (float) msg.getControllerValue() / 127.0f;
            if (learnArmed.load())
            {
                pendingLearnCC = cc;
                learnArmed = false;
                triggerAsyncUpdate();
            }
            if (tl.isLocked())
                for (const auto& m : rtCCs)
                    if (m.cc == cc && m.param != nullptr)
                        m.param->setValueNotifyingHost (v);
        }
    }

    // macros fan out to mapped params
    {
        juce::SpinLock::ScopedTryLockType tl (rtLock);
        if (tl.isLocked())
            for (int i = 0; i < 4; ++i)
            {
                const float v = macroParams[i]->load();
                if (std::abs (v - lastMacro[i]) < 1.0e-4f) continue;
                lastMacro[i] = v;
                for (const auto& m : rtMacros)
                    if (m.macro == i && m.param != nullptr)
                        m.param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, juce::jmap (v, m.lo, m.hi)));
            }
    }

    std::array<juce::int8, kNumModules> order;
    {
        juce::SpinLock::ScopedTryLockType tl (rtLock);
        order = rtOrder;
    }

    bool anyActive = false;
    for (int slot = 0; slot < kNumModules; ++slot)
    {
        const int m = order[(size_t) slot];
        const float targetMix = onParams[m]->load() > 0.5f ? mixParams[m]->load() : 0.0f;
        mixSm[m].setTargetValue (targetMix);

        if (targetMix < 1.0e-4f && mixSm[m].getCurrentValue() < 1.0e-4f)
        {
            mixSm[m].skip (n);
            continue;
        }
        anyActive = true;

        for (int ch = 0; ch < 2; ++ch)
            dry.copyFrom (ch, 0, buffer, juce::jmin (ch, buffer.getNumChannels() - 1), 0, n);

        modules[m]->process (buffer, ctx);

        float* l = buffer.getWritePointer (0);
        float* r = buffer.getWritePointer (juce::jmin (1, buffer.getNumChannels() - 1));
        const float* dl = dry.getReadPointer (0);
        const float* dr = dry.getReadPointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float mx = mixSm[m].getNextValue();
            l[i] = l[i] * mx + dl[i] * (1.0f - mx);
            r[i] = r[i] * mx + dr[i] * (1.0f - mx);
        }
    }

    // final safety stage: transparent at sane levels, soft-knees the screams.
    // bypassed rack (no module active) touches nothing - the clean path stays clean.
    if (anyActive)
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                if (std::abs (d[i]) > 1.0f)
                    d[i] = std::tanh (d[i] * 0.5f) * 2.0f;
        }
}

// ============================================================ order / macros / midi

juce::StringArray RackProcessor::getOrder() const
{
    auto ex = extra();
    juce::StringArray order;
    order.addTokens (ex.getProperty (kOrderProp, "br,sc,gr,bc,fb,gs,dm,tp,cj").toString(), ",", "");
    juce::StringArray valid;
    for (const auto& id : order)
        for (int m = 0; m < kNumModules; ++m)
            if (id == kModuleIds[m]) { valid.add (id); break; }
    for (int m = 0; m < kNumModules; ++m)
        if (! valid.contains (kModuleIds[m])) valid.add (kModuleIds[m]);
    return valid;
}

void RackProcessor::moveModule (int from, int to)
{
    auto order = getOrder();
    if (from < 0 || from >= order.size() || to < 0 || to >= order.size()) return;
    order.move (from, to);
    extra().setProperty (kOrderProp, order.joinIntoString (","), nullptr);
}

void RackProcessor::assignMacro (int macro, const String& paramID, float lo, float hi)
{
    clearMacro (paramID);
    ValueTree m (kMacroMapId);
    m.setProperty (kMacroProp, macro, nullptr);
    m.setProperty (kParamProp, paramID, nullptr);
    m.setProperty (kLoProp, lo, nullptr);
    m.setProperty (kHiProp, hi, nullptr);
    extra().appendChild (m, nullptr);
}

void RackProcessor::clearMacro (const String& paramID)
{
    auto ex = extra();
    for (int i = ex.getNumChildren(); --i >= 0;)
        if (ex.getChild (i).hasType (kMacroMapId) && ex.getChild (i)[kParamProp].toString() == paramID)
            ex.removeChild (i, nullptr);
}

std::vector<RackProcessor::MacroMap> RackProcessor::getMacroMaps() const
{
    std::vector<MacroMap> out;
    for (const auto& c : extra())
        if (c.hasType (kMacroMapId))
            out.push_back ({ (int) c[kMacroProp], c[kParamProp].toString(), (float) (double) c[kLoProp], (float) (double) c[kHiProp] });
    return out;
}

void RackProcessor::armMidiLearn (const String& paramID)
{
    learnParamID = paramID;
    learnArmed = true;
}

void RackProcessor::clearMidiMap (const String& paramID)
{
    auto ex = extra();
    for (int i = ex.getNumChildren(); --i >= 0;)
        if (ex.getChild (i).hasType (kCCMapId) && ex.getChild (i)[kParamProp].toString() == paramID)
            ex.removeChild (i, nullptr);
}

String RackProcessor::getCCMapDescription (const String& paramID) const
{
    for (const auto& c : extra())
        if (c.hasType (kCCMapId) && c[kParamProp].toString() == paramID)
            return "CC " + c[kCCProp].toString();
    return {};
}

void RackProcessor::handleAsyncUpdate()
{
    const int cc = pendingLearnCC.exchange (-1);
    if (cc >= 0 && learnParamID.isNotEmpty())
    {
        clearMidiMap (learnParamID);
        ValueTree m (kCCMapId);
        m.setProperty (kCCProp, cc, nullptr);
        m.setProperty (kParamProp, learnParamID, nullptr);
        extra().appendChild (m, nullptr);
        learnParamID.clear();
    }
    rebuildRTState();
}

void RackProcessor::valueTreePropertyChanged (ValueTree& tree, const Identifier& prop)
{
    if (prop == kOrderProp || tree.hasType (kMacroMapId) || tree.hasType (kCCMapId))
        triggerAsyncUpdate();
    if (prop == kIRFileProp || prop == kIRMangleProp)
        triggerAsyncUpdate();
}

void RackProcessor::rebuildRTState()
{
    std::vector<RTMacro> macros;
    std::vector<RTCC> ccs;
    for (const auto& c : extra())
    {
        if (c.hasType (kMacroMapId))
            macros.push_back ({ (int) c[kMacroProp], apvts.getParameter (c[kParamProp].toString()),
                                (float) (double) c[kLoProp], (float) (double) c[kHiProp] });
        else if (c.hasType (kCCMapId))
            ccs.push_back ({ (int) c[kCCProp], apvts.getParameter (c[kParamProp].toString()) });
    }

    auto order = getOrder();
    std::array<juce::int8, kNumModules> ord {};
    for (int i = 0; i < kNumModules; ++i)
        for (int m = 0; m < kNumModules; ++m)
            if (order[i] == kModuleIds[m]) ord[(size_t) i] = (juce::int8) m;

    juce::SpinLock::ScopedLockType sl (rtLock);
    rtMacros = std::move (macros);
    rtCCs = std::move (ccs);
    rtOrder = ord;
}

// ============================================================ gate steps / IR

void RackProcessor::markGateStepsDirty() { syncGateStepsToState(); }

void RackProcessor::syncGateStepsToState()
{
    juce::StringArray vals;
    for (auto& s : gateSteps) vals.add (String (s.load(), 3));
    extra().setProperty (kStepsProp, vals.joinIntoString (","), nullptr);
}

void RackProcessor::loadGateStepsFromState()
{
    juce::StringArray vals;
    vals.addTokens (extra().getProperty (kStepsProp, "").toString(), ",", "");
    for (int i = 0; i < juce::jmin (32, vals.size()); ++i)
        gateSteps[(size_t) i].store (juce::jlimit (0.0f, 1.0f, vals[i].getFloatValue()));
}

void RackProcessor::setIRFile (const File& f, int mangleMode)
{
    auto ex = extra();
    ex.setProperty (kIRFileProp, f.getFullPathName(), nullptr);
    ex.setProperty (kIRMangleProp, mangleMode, nullptr);
    static_cast<ConvolutionJunkModule*> (modules[8].get())->loadIR (f, mangleMode);
}

File RackProcessor::getIRFile() const   { return File (extra()[kIRFileProp].toString()); }
int RackProcessor::getIRMangle() const  { return (int) extra().getProperty (kIRMangleProp, 0); }

// ============================================================ state

void RackProcessor::getStateInformation (juce::MemoryBlock& mb)
{
    syncGateStepsToState();
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, mb);
}

void RackProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        apvts.state.removeListener (this);
        apvts.replaceState (ValueTree::fromXml (*xml));
        apvts.state.addListener (this);
        loadGateStepsFromState();
        rebuildRTState();
        auto ex = extra();
        if (ex.hasProperty (kIRFileProp))
            static_cast<ConvolutionJunkModule*> (modules[8].get())
                ->loadIR (File (ex[kIRFileProp].toString()), getIRMangle());
    }
}

// ============================================================ gestures

void RackProcessor::setReal (const String& paramID, float v)
{
    if (auto* p = apvts.getParameter (paramID))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

juce::StringArray RackProcessor::getFactoryGestureNames()
{
    return { "Init (all off)", "Amen Shredder", "Stutter Ratchet", "Granule Storm",
             "Tape Decay Wash", "Dead Console", "Choir of Screams" };
}

void RackProcessor::applyFactoryGesture (int index)
{
    for (int m = 0; m < kNumModules; ++m)
    {
        setReal (String (kModuleIds[m]) + "_on", 0.0f);
        setReal (String (kModuleIds[m]) + "_mix", 1.0f);
    }

    switch (index)
    {
        case 1:     // Amen Shredder: slice the bar to ribbons, crunch the remains
            setReal ("sc_on", 1); setReal ("sc_grid", 2); setReal ("sc_shuffle", 0.8f);
            setReal ("sc_reverse", 0.4f); setReal ("sc_drop", 0.15f); setReal ("sc_spray", 5.0f);
            setReal ("bc_on", 1); setReal ("bc_bits", 8); setReal ("bc_down", 3); setReal ("bc_mix", 0.5f);
            break;
        case 2:     // Stutter Ratchet
            setReal ("br_on", 1); setReal ("br_div", 5); setReal ("br_chance", 0.45f);
            setReal ("br_ratchet", 4); setReal ("br_pitch", 3.0f); setReal ("br_decay", 0.8f);
            break;
        case 3:     // Granule Storm
            setReal ("gr_on", 1); setReal ("gr_size", 60); setReal ("gr_density", 45);
            setReal ("gr_jitter", 0.8f); setReal ("gr_spray", 12); setReal ("gr_reverse", 0.5f);
            break;
        case 4:     // Tape Decay Wash: the sentimental one
            setReal ("tp_on", 1); setReal ("tp_wow", 0.35f); setReal ("tp_flutter", 0.2f);
            setReal ("tp_sat", 6); setReal ("tp_hiss", 0.25f); setReal ("tp_age", 0.6f);
            setReal ("fb_on", 1); setReal ("fb_time", 180); setReal ("fb_spread", 1.6f);
            setReal ("fb_fb", 0.55f); setReal ("fb_damp", 2500); setReal ("fb_mix", 0.35f);
            break;
        case 5:     // Dead Console
            setReal ("dm_on", 1); setReal ("dm_shrate", 2000); setReal ("dm_shamt", 0.4f);
            setReal ("dm_drop", 0.2f); setReal ("dm_flip", 0.1f);
            setReal ("bc_on", 1); setReal ("bc_bits", 10); setReal ("bc_down", 2); setReal ("bc_mix", 0.35f);
            break;
        case 6:     // Choir of Screams
            setReal ("fb_on", 1); setReal ("fb_tuned", 1); setReal ("fb_pitch", 67);
            setReal ("fb_fb", 1.05f); setReal ("fb_damp", 6000); setReal ("fb_mix", 0.6f);
            setReal ("gr_on", 1); setReal ("gr_size", 200); setReal ("gr_density", 8);
            setReal ("gr_spray", 7); setReal ("gr_mix", 0.4f);
            break;
        default: break;
    }
}

bool RackProcessor::saveUserRack (const File& f)
{
    syncGateStepsToState();
    if (auto xml = apvts.copyState().createXml())
        return xml->writeTo (f);
    return false;
}

bool RackProcessor::loadUserRack (const File& f)
{
    auto xml = juce::parseXML (f);
    if (xml == nullptr) return false;
    juce::MemoryBlock mb;
    copyXmlToBinary (*xml, mb);
    setStateInformation (mb.getData(), (int) mb.getSize());
    return true;
}

juce::AudioProcessorEditor* RackProcessor::createEditor()
{
    return new RackEditor (*this);
}

} // namespace dg
