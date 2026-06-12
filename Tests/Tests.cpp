// RUIN headless test suite. Run: ./build/ruin_tests_artefacts/ruin-tests
// Covers the model, clip operations, comp crossfades, TEETH, and instruments.

#include <JuceHeader.h>
#include "../Source/Model/Session.h"
#include "../Source/Model/ClipOps.h"
#include "../Source/Engine/TempoMap.h"
#include "../Source/Engine/ClipPlayer.h"
#include "../Source/Engine/MidiSource.h"
#include "../Source/Engine/Processors.h"
#include "../Source/Engine/StretchCache.h"
#include "../Source/Engine/Analysis.h"
#include "../Source/Rack/RackProcessor.h"
#include "../Source/Patcher/PatcherProcessor.h"
#include "../Source/UI/Updater.h"

using namespace dg;

static ValueTree addTestClip (SessionModel& s, ValueTree track, double start, double len)
{
    return s.addAudioClip (track, File ("/tmp/nonexistent.wav"), start, len, 48000.0);
}

// =========================================================================== model

struct TempoMapTests : juce::UnitTest
{
    TempoMapTests() : UnitTest ("TempoMap") {}
    void runTest() override
    {
        SessionModel s;
        beginTest ("constant tempo conversions");
        TempoMap map (s.tempoMap(), 48000.0);                       // 120 bpm 4/4
        expectWithinAbsoluteError (map.beatsToSeconds (4.0), 2.0, 1.0e-9);
        expectWithinAbsoluteError (map.secondsToBeats (1.0), 2.0, 1.0e-9);
        expectEquals ((int) map.beatsToSamples (2.0), 48000);

        beginTest ("tempo change integration");
        ValueTree t2 (id::TEMPO);
        t2.setProperty (id::beat, 4.0, nullptr);
        t2.setProperty (id::bpm, 60.0, nullptr);
        s.tempoMap().appendChild (t2, nullptr);
        TempoMap map2 (s.tempoMap(), 48000.0);
        expectWithinAbsoluteError (map2.beatsToSeconds (6.0), 2.0 + 2.0, 1.0e-9);
        expectWithinAbsoluteError (map2.secondsToBeats (4.0), 6.0, 1.0e-9);

        beginTest ("7/8 bar math");
        ValueTree sig (id::TIMESIG);
        sig.setProperty (id::beat, 0.0, nullptr);
        sig.setProperty (id::num, 7, nullptr);
        sig.setProperty (id::den, 8, nullptr);
        SessionModel s2;
        s2.tempoMap().removeChild (s2.tempoMap().getChildWithName (id::TIMESIG), nullptr);
        s2.tempoMap().appendChild (sig, nullptr);
        TempoMap map3 (s2.tempoMap(), 48000.0);
        auto bb = map3.barBeatAt (3.5 + 1.75);                      // bar length = 3.5 quarters
        expectEquals (bb.bar, 1);
        expectWithinAbsoluteError (bb.beatInBar, 1.75, 1.0e-9);
    }
};

struct SessionTests : juce::UnitTest
{
    SessionTests() : UnitTest ("Session") {}
    void runTest() override
    {
        beginTest ("save/load roundtrip");
        SessionModel s;
        auto track = s.addTrack ("audio", "T1");
        addTestClip (s, track, 1.5, 3.25);
        const File f = File::getSpecialLocation (File::tempDirectory).getChildFile ("ruin-test.dgproj");
        expect (s.saveAs (f));

        SessionModel s2;
        String err;
        expect (s2.load (f, err), err);
        auto t2 = s2.findTrack (track[id::uid].toString());
        expect (t2.isValid());
        expectWithinAbsoluteError ((double) SessionModel::clipsOf (t2).getChild (0)[id::start], 1.5, 1.0e-12);
        f.deleteFile();

        beginTest ("master stays last");
        SessionModel s3;
        s3.addTrack ("audio", "X");
        auto tracks = s3.tracks();
        expectEquals (tracks.getChild (tracks.getNumChildren() - 1)[id::type].toString(), String ("master"));

        beginTest ("session view: scenes + slots");
        SessionModel s4;
        expectEquals (s4.scenes().getNumChildren(), 8);          // default scene rows
        auto track4 = s4.addTrack ("midi", "T");
        const String sceneUid = s4.scenes().getChild (2)[id::uid].toString();
        ValueTree clip4 (id::CLIP);
        clip4.setProperty (id::uid, SessionModel::newUID(), nullptr);
        clip4.setProperty (id::type, "midi", nullptr);
        clip4.setProperty (id::loopBeats, 8.0, nullptr);
        s4.setSlotClip (track4, sceneUid, clip4);
        auto got = s4.getSlotClip (track4, sceneUid);
        expect (got.isValid());
        expectWithinAbsoluteError ((double) got[id::loopBeats], 8.0, 1.0e-12);
        expect (! s4.getSlotClip (track4, s4.scenes().getChild (0)[id::uid].toString()).isValid());
        s4.setSlotClip (track4, sceneUid, {});                   // clear slot
        expect (! s4.getSlotClip (track4, sceneUid).isValid());
    }
};

// =========================================================================== clip ops

struct ClipOpsTests : juce::UnitTest
{
    ClipOpsTests() : UnitTest ("ClipOps") {}
    void runTest() override
    {
        beginTest ("split stays on the same track");        // regression: 'split sends rest to a new track'
        SessionModel s;
        TempoMap map (s.tempoMap(), 48000.0);
        auto track = s.addTrack ("audio", "T1");
        auto clip = addTestClip (s, track, 0.0, 4.0);
        const int trackCountBefore = s.tracks().getNumChildren();

        auto newUids = clipops::splitAt (s, map, { clip[id::uid].toString() }, 2.5);
        expectEquals (newUids.size(), 1);
        expectEquals (s.tracks().getNumChildren(), trackCountBefore);           // no new tracks
        auto clips = SessionModel::clipsOf (track);
        expectEquals (clips.getNumChildren(), 2);                               // both halves HERE
        expectWithinAbsoluteError ((double) clips.getChild (0)[id::length], 2.5, 1.0e-9);
        auto right = clips.getChild (1);
        expectWithinAbsoluteError ((double) right[id::start], 2.5, 1.0e-9);
        expectWithinAbsoluteError ((double) right[id::length], 1.5, 1.0e-9);
        expectWithinAbsoluteError ((double) right[id::offset], 2.5 * 48000.0, 1.0);
        expect (right[id::uid].toString() != clip[id::uid].toString());
        expectEquals ((int) right.getProperty (id::lane, 0), 0);                // audible, same lane

        beginTest ("ripple delete pulls later clips left");
        SessionModel s2;
        auto tr2 = s2.addTrack ("audio", "T");
        auto a = addTestClip (s2, tr2, 0.0, 1.0);
        auto b = addTestClip (s2, tr2, 2.0, 1.0);
        auto c = addTestClip (s2, tr2, 4.0, 1.0);
        clipops::rippleDelete (s2, { b[id::uid].toString() });
        auto cl2 = SessionModel::clipsOf (tr2);
        expectEquals (cl2.getNumChildren(), 2);
        expectWithinAbsoluteError ((double) a[id::start], 0.0, 1.0e-9);         // before: untouched
        expectWithinAbsoluteError ((double) c[id::start], 3.0, 1.0e-9);         // after: pulled left

        beginTest ("copy/paste keeps relative spacing");
        SessionModel s3;
        auto tr3 = s3.addTrack ("audio", "T");
        auto p = addTestClip (s3, tr3, 1.0, 1.0);
        auto q = addTestClip (s3, tr3, 3.5, 1.0);
        auto items = clipops::copyClips (s3, { p[id::uid].toString(), q[id::uid].toString() });
        expectEquals ((int) items.size(), 2);
        auto pasted = clipops::paste (s3, items, 10.0);
        expectEquals (pasted.size(), 2);
        std::vector<double> starts;
        for (auto clip3 : SessionModel::clipsOf (tr3))
            if (pasted.contains (clip3[id::uid].toString()))
                starts.push_back ((double) clip3[id::start]);
        std::sort (starts.begin(), starts.end());
        expectWithinAbsoluteError (starts[0], 10.0, 1.0e-9);
        expectWithinAbsoluteError (starts[1], 12.5, 1.0e-9);

        beginTest ("undo restores split");
        s.undo.undo();
        expectEquals (SessionModel::clipsOf (track).getNumChildren(), 1);
        expectWithinAbsoluteError ((double) clip[id::length], 4.0, 1.0e-9);
    }
};

struct CrossfadeTests : juce::UnitTest
{
    CrossfadeTests() : UnitTest ("CompCrossfades") {}
    void runTest() override
    {
        auto mk = [] (juce::int64 start, juce::int64 len)
        {
            AudioClipRT c;
            c.start = start; c.length = len;
            return c;
        };

        beginTest ("partial tail/head overlap crossfades");
        std::vector<AudioClipRT> v { mk (0, 1000), mk (800, 1000) };
        applyCompCrossfades (v);
        expectEquals ((int) v[0].xfadeOut, 200);
        expectEquals ((int) v[1].xfadeIn, 200);

        beginTest ("containment does NOT silence the host clip");   // regression: dragged clip muted the stretched one
        std::vector<AudioClipRT> w { mk (0, 10000), mk (2000, 1000) };
        applyCompCrossfades (w);
        expectEquals ((int) w[0].xfadeOut, 0);
        expectEquals ((int) w[0].xfadeIn, 0);
        expectEquals ((int) w[1].xfadeIn, 0);
        expectEquals ((int) w[1].xfadeOut, 0);

        beginTest ("same start sums, no fades");
        std::vector<AudioClipRT> u { mk (0, 1000), mk (0, 2000) };
        applyCompCrossfades (u);
        expectEquals ((int) u[0].xfadeIn + (int) u[0].xfadeOut
                      + (int) u[1].xfadeIn + (int) u[1].xfadeOut, 0);
    }
};

// =========================================================================== rack

static bool allFinite (const juce::AudioBuffer<float>& b, float maxAbs)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = b.getSample (ch, i);
            if (! std::isfinite (v) || std::abs (v) > maxAbs) return false;
        }
    return true;
}

struct RackTests : juce::UnitTest
{
    RackTests() : UnitTest ("TEETH") {}

    void runTest() override
    {
        juce::Random rng (42);

        beginTest ("bit-transparent when all modules are off");     // the design law
        RackProcessor rack;
        rack.setPlayConfigDetails (2, 2, 48000.0, 512);
        rack.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buf (2, 512), ref (2, 512);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buf.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);
        ref.makeCopyOf (buf);
        juce::MidiBuffer midi;
        for (int n = 0; n < 16; ++n)
            rack.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                expectEquals (buf.getSample (ch, i), ref.getSample (ch, i));

        beginTest ("factory gesture arms modules");
        rack.applyFactoryGesture (1);                                // Amen Shredder
        expect (rack.apvts.getRawParameterValue ("sc_on")->load() > 0.5f);
        expect (rack.apvts.getRawParameterValue ("bc_on")->load() > 0.5f);

        beginTest ("every module survives noise without blowing up");
        for (int m = 0; m < RackProcessor::kNumModules; ++m)
        {
            RackProcessor r2;
            r2.setPlayConfigDetails (2, 2, 48000.0, 512);
            r2.prepareToPlay (48000.0, 512);
            if (auto* p = r2.apvts.getParameter (String (RackProcessor::kModuleIds[m]) + "_on"))
                p->setValueNotifyingHost (1.0f);
            juce::AudioBuffer<float> b2 (2, 512);
            juce::MidiBuffer mb2;
            for (int n = 0; n < 80; ++n)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        b2.setSample (ch, i, rng.nextFloat() * 1.6f - 0.8f);
                r2.processBlock (b2, mb2);
                if (! allFinite (b2, 4.0f))
                    break;
            }
            expect (allFinite (b2, 4.0f), String ("module ") + RackProcessor::kModuleIds[m]);
        }

        beginTest ("feedback net screams but stays clamped");
        RackProcessor r3;
        r3.setPlayConfigDetails (2, 2, 48000.0, 512);
        r3.prepareToPlay (48000.0, 512);
        r3.apvts.getParameter ("fb_on")->setValueNotifyingHost (1.0f);
        r3.apvts.getParameter ("fb_fb")->setValueNotifyingHost (1.0f);     // max feedback
        juce::AudioBuffer<float> b3 (2, 512);
        juce::MidiBuffer mb3;
        for (int n = 0; n < 300; ++n)
        {
            if (n < 4)
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        b3.setSample (ch, i, rng.nextFloat() - 0.5f);
            else
                b3.clear();
            r3.processBlock (b3, mb3);
        }
        expect (allFinite (b3, 2.5f));

        beginTest ("rack state roundtrip (order + gate steps)");
        RackProcessor r4;
        r4.moveModule (0, 3);
        r4.gateSteps[5].store (0.123f);
        r4.markGateStepsDirty();
        juce::MemoryBlock state;
        r4.getStateInformation (state);
        RackProcessor r5;
        r5.setStateInformation (state.getData(), (int) state.getSize());
        expectEquals (r5.getOrder().joinIntoString (","), r4.getOrder().joinIntoString (","));
        expectWithinAbsoluteError (r5.gateSteps[5].load(), 0.123f, 1.0e-4f);
    }
};

struct InstrumentTests : juce::UnitTest
{
    InstrumentTests() : UnitTest ("BuiltinInstruments") {}
    void runTest() override
    {
        for (auto* name : { "rust", "gravel", "hymn", "rubble" })
        {
            beginTest (String ("instrument: ") + name);
            auto inst = BuiltinInstrument::create (name);
            expect (inst != nullptr);
            inst->setPlayConfigDetails (2, 2, 48000.0, 512);
            inst->prepareToPlay (48000.0, 512);

            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            float energy = 0;
            for (int n = 0; n < 20; ++n)
            {
                inst->processBlock (buf, midi);
                midi.clear();
                energy += buf.getRMSLevel (0, 0, 512);
                expect (allFinite (buf, 4.0f));
            }
            expect (energy > 0.001f, "produces sound");

            midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            float tailEnergy = 1.0f;
            for (int n = 0; n < 800 && tailEnergy > 1.0e-5f; ++n)
            {
                inst->processBlock (buf, midi);
                midi.clear();
                tailEnergy = buf.getRMSLevel (0, 0, 512);
            }
            expect (tailEnergy <= 1.0e-5f, "decays to silence");
        }
    }
};

// =========================================================================== patcher

struct PatcherTests : juce::UnitTest
{
    PatcherTests() : UnitTest ("WIRES") {}
    void runTest() override
    {
        beginTest ("default patch (adc~ -> dac~) is passthrough");
        PatcherProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 256);
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buf (2, 256), ref (2, 256);
        juce::Random rng (7);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
                buf.setSample (ch, i, rng.nextFloat() - 0.5f);
        ref.makeCopyOf (buf);
        juce::MidiBuffer midi;
        p.processBlock (buf, midi);
        for (int i = 0; i < 256; ++i)
            if (std::abs (buf.getSample (0, i) - ref.getSample (0, i)) > 1.0e-6f)
            { expect (false, "not passthrough"); break; }
        expect (true);

        beginTest ("osc~ -> dac~ makes a sine");
        PatcherProcessor p2;
        p2.setPlayConfigDetails (2, 2, 48000.0, 256);
        auto oscNode = p2.addNode ("osc~ 220", 10, 10);
        ValueTree dacNode;
        for (const auto& n : p2.patch)
            if (n[id::type].toString() == "dac~") dacNode = n;
        p2.addCable (oscNode[id::uid].toString(), 0, dacNode[id::uid].toString(), 0);
        p2.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> b2 (2, 256);
        float peak = 0;
        for (int k = 0; k < 8; ++k)
        {
            b2.clear();
            p2.processBlock (b2, midi);
            peak = juce::jmax (peak, b2.getMagnitude (0, 0, 256));
        }
        expect (peak > 0.5f && peak <= 1.01f, "sine present: " + String (peak));

        beginTest ("delay~ feedback stays bounded");
        PatcherProcessor p3;
        p3.setPlayConfigDetails (2, 2, 48000.0, 256);
        auto noiseN = p3.addNode ("noise~", 0, 0);
        auto delayN = p3.addNode ("delay~ 80 0.9", 0, 0);
        ValueTree dac3;
        for (const auto& n : p3.patch)
            if (n[id::type].toString() == "dac~") dac3 = n;
        p3.addCable (noiseN[id::uid].toString(), 0, delayN[id::uid].toString(), 0);
        p3.addCable (delayN[id::uid].toString(), 0, dac3[id::uid].toString(), 0);
        p3.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> b3 (2, 256);
        bool ok = true;
        for (int k = 0; k < 400 && ok; ++k)
        {
            b3.clear();
            p3.processBlock (b3, midi);
            for (int i = 0; i < 256 && ok; ++i)
                ok = std::isfinite (b3.getSample (0, i)) && std::abs (b3.getSample (0, i)) < 30.0f;
        }
        expect (ok, "feedback bounded");

        beginTest ("state roundtrip preserves the patch");
        juce::MemoryBlock state;
        p2.getStateInformation (state);
        PatcherProcessor p4;
        p4.setStateInformation (state.getData(), (int) state.getSize());
        int oscCount = 0;
        for (const auto& n : p4.patch)
            if (n[id::type].toString() == "osc~") ++oscCount;
        expectEquals (oscCount, 1);

        beginTest ("number box feeds the signal graph live");
        PatcherProcessor p6;
        p6.setPlayConfigDetails (2, 2, 48000.0, 256);
        auto numN = p6.addNode ("number 0.25", 0, 0);
        auto tapN = p6.addNode ("modout", 0, 0);
        p6.addCable (numN[id::uid].toString(), 0, tapN[id::uid].toString(), 0);
        p6.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> b6 (2, 256);
        for (int k = 0; k < 24; ++k) { b6.clear(); p6.processBlock (b6, midi); }
        expect (std::abs (p6.modOut (0) - 0.25f) < 0.02f, "arg value flows");
        p6.numberValueFor (numN[id::uid].toString())->store (0.9f);    // an editor drag
        for (int k = 0; k < 24; ++k) { b6.clear(); p6.processBlock (b6, midi); }
        expect (std::abs (p6.modOut (0) - 0.9f) < 0.02f, "live value follows");

        beginTest ("modout taps the signal as a mod source");
        PatcherProcessor p5;
        p5.setPlayConfigDetails (2, 2, 48000.0, 256);
        auto sigN = p5.addNode ("sig 0.8", 0, 0);
        auto modN = p5.addNode ("modout", 0, 0);
        p5.addCable (sigN[id::uid].toString(), 0, modN[id::uid].toString(), 0);
        p5.prepareToPlay (48000.0, 256);
        expectEquals (p5.getNumModOuts(), 1);
        juce::AudioBuffer<float> b5 (2, 256);
        for (int k = 0; k < 24; ++k) { b5.clear(); p5.processBlock (b5, midi); }
        expect (std::abs (p5.modOut (0) - 0.8f) < 0.05f,
                "tap follows the signal: " + String (p5.modOut (0)));

        beginTest ("spec table: port types match port counts");
        for (const auto& s : PatcherProcessor::specs())
        {
            expectEquals ((int) String (s.inTypes).length(), s.ins, String (s.name) + " inTypes");
            expectEquals ((int) String (s.outTypes).length(), s.outs, String (s.name) + " outTypes");
            for (const char* t = s.inTypes;  *t != 0; ++t)
                expect (*t == 's' || *t == 'n' || *t == 'e', String (s.name) + " inlet type");
            for (const char* t = s.outTypes; *t != 0; ++t)
                expect (*t == 's' || *t == 'n' || *t == 'e', String (s.name) + " outlet type");
            expect (PatcherProcessor::specFor (s.name) == &s, String (s.name) + " specFor");
        }
        expect (PatcherProcessor::specFor ("nonsense~") == nullptr, "specFor rejects junk");

        beginTest ("chan~ pulls the tapped channel ring into the patch");
        PatcherProcessor p7;
        p7.setPlayConfigDetails (2, 2, 48000.0, 256);
        auto ring = std::make_shared<ChanTap>();
        std::vector<float> tl (256), tr (256);
        for (int i = 0; i < 256; ++i) { tl[(size_t) i] = (float) i / 256.0f; tr[(size_t) i] = -tl[(size_t) i]; }
        ring->write (tl.data(), tr.data(), 256);
        p7.setChanTapProvider ([ring] (const String& ref, bool pre)
                               { return (ref == "2" && ! pre) ? ring : nullptr; });
        auto chanN = p7.addNode ("chan~ 2", 0, 0);
        ValueTree dac7;
        for (const auto& n : p7.patch)
            if (n[id::type].toString() == "dac~") dac7 = n;
        p7.addCable (chanN[id::uid].toString(), 0, dac7[id::uid].toString(), 0);
        p7.addCable (chanN[id::uid].toString(), 1, dac7[id::uid].toString(), 1);
        p7.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> b7 (2, 256);
        b7.clear();
        p7.processBlock (b7, midi);
        bool tapMatch = true;
        for (int i = 0; i < 256 && tapMatch; ++i)
            tapMatch = std::abs (b7.getSample (0, i) - tl[(size_t) i]) < 1.0e-6f
                    && std::abs (b7.getSample (1, i) - tr[(size_t) i]) < 1.0e-6f;
        expect (tapMatch, "ring content reaches dac~");

        beginTest ("unresolved chan~ is silent");
        PatcherProcessor p8;
        p8.setPlayConfigDetails (2, 2, 48000.0, 256);
        p8.setChanTapProvider ([] (const String&, bool) { return nullptr; });
        auto chanX = p8.addNode ("chan~ 99", 0, 0);
        ValueTree dac8;
        for (const auto& n : p8.patch)
            if (n[id::type].toString() == "dac~") dac8 = n;
        p8.addCable (chanX[id::uid].toString(), 0, dac8[id::uid].toString(), 0);
        p8.addCable (chanX[id::uid].toString(), 1, dac8[id::uid].toString(), 1);
        p8.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> b8 (2, 256);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
                b8.setSample (ch, i, 0.7f);                 // junk in: silence out
        p8.processBlock (b8, midi);
        expect (b8.getMagnitude (0, 0, 256) < 1.0e-9f
                && b8.getMagnitude (1, 0, 256) < 1.0e-9f, "no tap, no sound");
    }
};

// =========================================================================== updater

struct UpdaterTests : juce::UnitTest
{
    UpdaterTests() : UnitTest ("Updater") {}
    void runTest() override
    {
        beginTest ("semver compare");
        expect (Updater::isNewer ("0.3.0", "0.2.0"));
        expect (Updater::isNewer ("v0.3.0", "0.2.9"));
        expect (Updater::isNewer ("0.10.0", "0.9.9"));      // numeric, not lexicographic
        expect (Updater::isNewer ("1.0.0", "0.99.99"));
        expect (! Updater::isNewer ("0.2.0", "0.2.0"));
        expect (! Updater::isNewer ("v0.2.0", "0.2.0"));
        expect (! Updater::isNewer ("0.2.0", "0.10.0"));
        expect (! Updater::isNewer ("", "0.2.0"));
    }
};

// =========================================================================== analysis

struct AnalysisTests : juce::UnitTest
{
    AnalysisTests() : UnitTest ("BpmEstimate") {}
    void runTest() override
    {
        beginTest ("140 bpm click track detects within 2 bpm");
        const double sr = 44100.0, bpm = 140.0;
        const int len = (int) (sr * 16.0);
        juce::AudioBuffer<float> b (1, len);
        b.clear();
        const double beat = sr * 60.0 / bpm;
        for (double p = 0; p < len - 600; p += beat)            // decaying click per beat
            for (int i = 0; i < 600; ++i)
                b.setSample (0, (int) p + i, (float) (std::sin (i * 0.45) * std::exp (-i / 120.0)));

        const File f = File::getSpecialLocation (File::tempDirectory).getChildFile ("ruin-bpm-test.wav");
        f.deleteFile();
        {
            juce::WavAudioFormat wav;
            auto out = f.createOutputStream();
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (out.get(), sr, 1, 16, {}, 0));
            out.release();
            w->writeFromAudioSampleBuffer (b, 0, len);
        }
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        expect (r != nullptr);
        const double est = estimateBpmFromReader (*r);
        expectWithinAbsoluteError (est, bpm, 2.0);
        f.deleteFile();
    }
};

// =========================================================================== stretch

struct StretchTests : juce::UnitTest
{
    StretchTests() : UnitTest ("StretchCache") {}
    void runTest() override
    {
        if (! StretchCache::available())
        {
            beginTest ("rubberband unavailable - skipped");
            expect (true);
            return;
        }
        beginTest ("2x render is twice as long");
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();

        const File src = File::getSpecialLocation (File::tempDirectory).getChildFile ("ruin-stretch-src.wav");
        src.deleteFile();
        {
            juce::WavAudioFormat wav;
            auto out = src.createOutputStream();
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (out.get(), 48000.0, 1, 16, {}, 0));
            out.release();
            juce::AudioBuffer<float> b (1, 24000);
            for (int i = 0; i < 24000; ++i)
                b.setSample (0, i, std::sin (i * 0.05f) * 0.5f);
            w->writeFromAudioSampleBuffer (b, 0, 24000);
        }

        StretchCache cache (fm);
        File result = cache.get (src, 2.0, {});
        for (int waited = 0; result == File() && waited < 200; ++waited)   // poll up to ~20s
        {
            juce::Thread::sleep (100);
            result = cache.get (src, 2.0, {});
        }
        expect (result != File(), "render completed");
        if (result != File())
        {
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (result));
            expect (r != nullptr);
            if (r != nullptr)
                expectWithinAbsoluteError ((double) r->lengthInSamples, 48000.0, 4800.0);   // 2x +-10%
        }
        src.deleteFile();
    }
};

// =========================================================================== clip loop + slip

struct ClipLoopSlipTests : juce::UnitTest
{
    ClipLoopSlipTests() : UnitTest ("ClipLoopSlip") {}
    void runTest() override
    {
        beginTest ("setLoop seeds the audio pass from the content length");
        SessionModel s;
        TempoMap map (s.tempoMap(), 48000.0);
        auto track = s.addTrack ("audio", "T");
        auto clip = addTestClip (s, track, 0.0, 8.0);
        const String uid = clip[id::uid].toString();
        clipops::setLoop (s, map, { uid }, true, [] (const ValueTree&) { return 2.0; });
        expect ((bool) clip[id::loop]);
        expectWithinAbsoluteError ((double) clip[id::loopLen], 2.0, 1.0e-12);

        beginTest ("disable keeps the stored pass for re-enable");
        clipops::setLoop (s, map, { uid }, false);
        expect (! (bool) clip[id::loop]);
        expectWithinAbsoluteError ((double) clip[id::loopLen], 2.0, 1.0e-12);
        clipops::setLoop (s, map, { uid }, true, [] (const ValueTree&) { return 999.0; });
        expectWithinAbsoluteError ((double) clip[id::loopLen], 2.0, 1.0e-12);    // not re-seeded

        beginTest ("audio slip slides the source window and clamps at the file start");
        SessionModel s2;
        TempoMap map2 (s2.tempoMap(), 48000.0);
        auto tr2 = s2.addTrack ("audio", "T");
        auto c2 = addTestClip (s2, tr2, 0.0, 4.0);                   // offset starts 0
        const String uid2 = c2[id::uid].toString();
        clipops::slip (s2, map2, { uid2 }, -0.5);                    // content earlier: later source
        expectWithinAbsoluteError ((double) c2[id::offset], 24000.0, 1.0e-6);
        clipops::slip (s2, map2, { uid2 }, 0.25);
        expectWithinAbsoluteError ((double) c2[id::offset], 12000.0, 1.0e-6);
        clipops::slip (s2, map2, { uid2 }, 2.0);                     // past the file start: clamp
        expectWithinAbsoluteError ((double) c2[id::offset], 0.0, 1.0e-12);

        beginTest ("slip undoes in one step");
        SessionModel s3;
        TempoMap map3 (s3.tempoMap(), 48000.0);
        auto tr3 = s3.addTrack ("audio", "T");
        auto c3 = addTestClip (s3, tr3, 0.0, 4.0);
        clipops::slip (s3, map3, { c3[id::uid].toString() }, -0.5);
        expectWithinAbsoluteError ((double) c3[id::offset], 24000.0, 1.0e-6);
        s3.undo.undo();
        expectWithinAbsoluteError ((double) c3[id::offset], 0.0, 1.0e-12);

        beginTest ("midi setLoop seeds whole beats and slip rotates the pass");
        SessionModel s4;
        TempoMap map4 (s4.tempoMap(), 48000.0);                      // 120 bpm: 1 beat = 0.5 s
        auto tr4 = s4.addTrack ("midi", "T");
        auto c4 = s4.addMidiClip (tr4, 0.0, 2.0);                    // 4 beats
        ValueTree note (id::NOTE);
        note.setProperty (id::beat, 3.0, nullptr);
        note.setProperty (id::len, 0.5, nullptr);
        note.setProperty (id::pitch, 60, nullptr);
        note.setProperty (id::vel, 100, nullptr);
        c4.getChildWithName (id::NOTES).appendChild (note, nullptr);
        clipops::setLoop (s4, map4, { c4[id::uid].toString() }, true);
        expectWithinAbsoluteError ((double) c4[id::loopBeats], 4.0, 1.0e-12);
        clipops::slip (s4, map4, { c4[id::uid].toString() }, 0.5);   // +1 beat: 3 wraps to 0
        expectWithinAbsoluteError ((double) note[id::beat], 0.0, 1.0e-9);
        clipops::slip (s4, map4, { c4[id::uid].toString() }, -1.0);  // -2 beats: 0 wraps to 2
        expectWithinAbsoluteError ((double) note[id::beat], 2.0, 1.0e-9);

        beginTest ("split keeps a looped audio clip's pass phase");
        SessionModel s5;
        TempoMap map5 (s5.tempoMap(), 48000.0);
        auto tr5 = s5.addTrack ("audio", "T");
        auto c5 = addTestClip (s5, tr5, 0.0, 8.0);
        const String uid5 = c5[id::uid].toString();
        clipops::setLoop (s5, map5, { uid5 }, true, [] (const ValueTree&) { return 2.0; });
        clipops::splitAt (s5, map5, { uid5 }, 5.0);                  // 5 s = 2 passes + 1 s phase
        auto clips5 = SessionModel::clipsOf (tr5);
        expectEquals (clips5.getNumChildren(), 2);
        auto right5 = clips5.getChild (1);
        expect ((bool) right5[id::loop]);
        expectWithinAbsoluteError ((double) right5[id::loopLen], 2.0, 1.0e-12);
        expectWithinAbsoluteError ((double) right5[id::offset], 48000.0, 1.0);   // 1 s into the pass

        beginTest ("split rotates a looped midi clip's notes");
        SessionModel s6;
        TempoMap map6 (s6.tempoMap(), 48000.0);
        auto tr6 = s6.addTrack ("midi", "T");
        auto c6 = s6.addMidiClip (tr6, 0.0, 4.0);                    // 8 beats
        ValueTree n6 (id::NOTE);
        n6.setProperty (id::beat, 1.0, nullptr);
        n6.setProperty (id::len, 0.5, nullptr);
        n6.setProperty (id::pitch, 64, nullptr);
        n6.setProperty (id::vel, 90, nullptr);
        c6.getChildWithName (id::NOTES).appendChild (n6, nullptr);
        clipops::setLoop (s6, map6, { c6[id::uid].toString() }, true);   // seeds loopBeats = 8
        c6.setProperty (id::loopBeats, 4.0, nullptr);                    // tighten to a 4-beat pass
        clipops::splitAt (s6, map6, { c6[id::uid].toString() }, 1.5);    // beat 3: phase 3
        auto clips6 = SessionModel::clipsOf (tr6);
        expectEquals (clips6.getNumChildren(), 2);
        expectEquals (c6.getChildWithName (id::NOTES).getNumChildren(), 1);   // left keeps looping content
        auto rn6 = clips6.getChild (1).getChildWithName (id::NOTES).getChild (0);
        expectWithinAbsoluteError ((double) rn6[id::beat], 2.0, 1.0e-9);      // (1 - 3) mod 4
    }
};

// =========================================================================== looped playback

namespace
{
    // Deterministic fake source: sample value == sample index, both channels.
    struct RampReader : juce::AudioFormatReader
    {
        RampReader() : juce::AudioFormatReader (nullptr, "ramp")
        {
            sampleRate = 48000.0;
            bitsPerSample = 32;
            lengthInSamples = 48000;
            numChannels = 2;
            usesFloatingPointData = true;
        }
        bool readSamples (int* const* dest, int numDestChannels, int startOffsetInDestBuffer,
                          juce::int64 startSampleInFile, int numSamples) override
        {
            for (int ch = 0; ch < numDestChannels; ++ch)
            {
                if (dest[ch] == nullptr) continue;
                auto* d = reinterpret_cast<float*> (dest[ch]) + startOffsetInDestBuffer;
                for (int i = 0; i < numSamples; ++i)
                {
                    const juce::int64 p = startSampleInFile + i;
                    d[i] = (p >= 0 && p < lengthInSamples) ? (float) p : 0.0f;
                }
            }
            return true;
        }
    };
}

struct LoopedRenderTests : juce::UnitTest
{
    LoopedRenderTests() : UnitTest ("LoopedRender") {}

    std::vector<float> render (const AudioClipRT& c, int total, int blockSize)
    {
        juce::AudioBuffer<float> out (2, blockSize), scratch (2, 4096);
        std::vector<float> got ((size_t) total, 0.0f);
        for (juce::int64 bs = 0; bs < total; bs += blockSize)
        {
            const int n = (int) juce::jmin ((juce::int64) blockSize, (juce::int64) total - bs);
            out.clear();
            renderClipSpan (c, out, bs, n, scratch);
            for (int i = 0; i < n; ++i)
                got[(size_t) (bs + i)] = out.getSample (0, i);
        }
        return got;
    }

    void runTest() override
    {
        AudioClipRT c;
        c.start = 100;
        c.length = 600;
        c.offset = 0;
        c.ratio = 1.0;
        c.reader = std::make_shared<RampReader>();
        c.numFileChannels = 2;
        c.fileLength = c.reader->lengthInSamples;

        beginTest ("unlooped clip plays the source linearly");
        auto lin = render (c, 900, 256);
        bool ok = true;
        for (int i = 0; i < 900 && ok; ++i)
        {
            const float want = (i >= 100 && i < 700) ? (float) (i - 100) : 0.0f;
            ok = std::abs (lin[(size_t) i] - want) < 1.0e-4f;
        }
        expect (ok, "linear content");

        beginTest ("looped clip repeats its pass to fill the length");
        c.loopLen = 200;
        for (const int blockSize : { 256, 480, 37 })            // boundaries mid-block and aligned
        {
            auto got = render (c, 900, blockSize);
            ok = true;
            for (int i = 0; i < 900 && ok; ++i)
            {
                const float want = (i >= 100 && i < 700) ? (float) ((i - 100) % 200) : 0.0f;
                ok = std::abs (got[(size_t) i] - want) < 1.0e-4f;
            }
            expect (ok, "block size " + String (blockSize));
        }

        beginTest ("looped pass honours the source offset");
        c.offset = 1000;
        auto offGot = render (c, 900, 256);
        ok = true;
        for (int i = 100; i < 700 && ok; ++i)
            ok = std::abs (offGot[(size_t) i] - (float) (1000 + (i - 100) % 200)) < 1.0e-4f;
        expect (ok, "offset + pass phase");

        beginTest ("loop pass running past EOF goes silent, then wraps");
        AudioClipRT e;
        e.start = 0;
        e.length = 400;
        e.offset = 0;
        e.ratio = 1.0;
        e.loopLen = 200;
        auto ramp = std::make_shared<RampReader>();
        ramp->lengthInSamples = 150;                            // pass is 200, source only 150
        e.reader = ramp;
        e.numFileChannels = 2;
        e.fileLength = 150;
        auto eofGot = render (e, 400, 256);
        ok = true;
        for (int i = 0; i < 400 && ok; ++i)
        {
            const int ip = i % 200;
            const float want = ip < 150 ? (float) ip : 0.0f;    // tail of each pass is silent
            ok = std::abs (eofGot[(size_t) i] - want) < 1.0e-4f;
        }
        expect (ok, "EOF inside the pass");
    }
};

// =========================================================================== midi loop expansion

struct MidiLoopExpandTests : juce::UnitTest
{
    MidiLoopExpandTests() : UnitTest ("MidiLoopExpand") {}
    void runTest() override
    {
        SessionModel s;
        TempoMap map (s.tempoMap(), 48000.0);                   // 120 bpm: beat = 24000 samples
        auto tr = s.addTrack ("midi", "T");
        auto c = s.addMidiClip (tr, 0.0, 4.0);                  // 8 beats
        c.setProperty (id::loop, true, nullptr);
        c.setProperty (id::loopBeats, 4.0, nullptr);
        auto notes = c.getChildWithName (id::NOTES);

        auto addNote = [&notes] (double beat, double len, int pitch)
        {
            ValueTree n (id::NOTE);
            n.setProperty (id::beat, beat, nullptr);
            n.setProperty (id::len, len, nullptr);
            n.setProperty (id::pitch, pitch, nullptr);
            n.setProperty (id::vel, 100, nullptr);
            notes.appendChild (n, nullptr);
        };

        beginTest ("notes repeat once per pass across the clip");
        addNote (1.0, 0.5, 60);
        MidiPlaylist pl;
        appendClipNotes (pl, c, map, 48000.0);
        expectEquals ((int) pl.notes.size(), 2);
        expectEquals ((int) pl.notes[0].on, 24000);             // beat 1
        expectEquals ((int) pl.notes[1].on, 120000);            // beat 5
        expectEquals ((int) pl.notes[0].note, 60);

        beginTest ("a held note is clamped at its pass end");
        notes.removeAllChildren (nullptr);
        addNote (3.0, 4.0, 61);                                 // would ring into its own repeat
        MidiPlaylist pl2;
        appendClipNotes (pl2, c, map, 48000.0);
        expectEquals ((int) pl2.notes.size(), 2);
        expectEquals ((int) pl2.notes[0].off, 96000);           // pass end at beat 4
        expectEquals ((int) pl2.notes[1].off, 192000);          // clip end at beat 8

        beginTest ("notes beyond the pass stay silent");
        notes.removeAllChildren (nullptr);
        addNote (5.0, 0.5, 62);                                 // pass is 4 beats
        MidiPlaylist pl3;
        appendClipNotes (pl3, c, map, 48000.0);
        expectEquals ((int) pl3.notes.size(), 0);

        beginTest ("unlooped expansion matches the old single-pass behaviour");
        c.setProperty (id::loop, false, nullptr);
        notes.removeAllChildren (nullptr);
        addNote (1.0, 0.5, 60);
        addNote (9.0, 0.5, 63);                                 // past the 8-beat clip: culled
        MidiPlaylist pl4;
        appendClipNotes (pl4, c, map, 48000.0);
        expectEquals ((int) pl4.notes.size(), 1);
        expectEquals ((int) pl4.notes[0].on, 24000);
    }
};

// =========================================================================== crossfade handles

struct CrossfadeHandleTests : juce::UnitTest
{
    CrossfadeHandleTests() : UnitTest ("CrossfadeHandles") {}
    void runTest() override
    {
        beginTest ("overlapAt finds the partial tail/head overlap only");
        SessionModel s;
        auto track = s.addTrack ("audio", "T");
        auto a = addTestClip (s, track, 0.0, 2.0);
        auto b = addTestClip (s, track, 1.5, 2.0);              // overlap [1.5, 2.0]
        auto ov = clipops::overlapAt (track, 1.75);
        expect (ov.isValid());
        expect (ov.left == a && ov.right == b);
        expectWithinAbsoluteError (ov.start, 1.5, 1.0e-12);
        expectWithinAbsoluteError (ov.end, 2.0, 1.0e-12);
        expect (! clipops::overlapAt (track, 0.5).isValid());   // outside the overlap

        beginTest ("containment is not a crossfade");
        SessionModel s2;
        auto tr2 = s2.addTrack ("audio", "T");
        addTestClip (s2, tr2, 0.0, 10.0);
        addTestClip (s2, tr2, 2.0, 1.0);                        // contained: layered, not faded
        expect (! clipops::overlapAt (tr2, 2.5).isValid());

        beginTest ("overlapsOf reports both sides");
        expectEquals ((int) clipops::overlapsOf (a).size(), 1);
        expectEquals ((int) clipops::overlapsOf (b).size(), 1);
        expect (clipops::overlapsOf (b)[0].left == a);

        beginTest ("rollBoundary slides the edit point, content stays anchored");
        const double applied = clipops::rollBoundary (s, a, b, 0.25);
        expectWithinAbsoluteError (applied, 0.25, 1.0e-12);
        expectWithinAbsoluteError ((double) a[id::length], 2.25, 1.0e-9);
        expectWithinAbsoluteError ((double) b[id::start], 1.75, 1.0e-9);
        expectWithinAbsoluteError ((double) b[id::length], 1.75, 1.0e-9);   // far end fixed at 3.5
        expectWithinAbsoluteError ((double) b[id::offset], 0.25 * 48000.0, 1.0);
        auto ov2 = clipops::overlapAt (track, 1.9);
        expect (ov2.isValid());
        expectWithinAbsoluteError (ov2.end - ov2.start, 0.5, 1.0e-9);       // overlap preserved

        beginTest ("rollBoundary clamps where the right clip's source starts");
        const double back = clipops::rollBoundary (s, a, b, -2.0);          // offset is 0.25 s worth
        expectWithinAbsoluteError (back, -0.25, 1.0e-9);
        expectWithinAbsoluteError ((double) b[id::offset], 0.0, 1.0e-6);

        beginTest ("rollBoundary undoes in one step");
        const double beforeLen = a[id::length];
        clipops::rollBoundary (s, a, b, 0.1);
        s.undo.undo();
        expectWithinAbsoluteError ((double) a[id::length], beforeLen, 1.0e-12);

        beginTest ("resizeOverlap grows symmetrically about the centre");
        SessionModel s3;
        auto tr3 = s3.addTrack ("audio", "T");
        auto a3 = addTestClip (s3, tr3, 0.0, 2.0);
        auto b3 = addTestClip (s3, tr3, 1.5, 2.0);
        b3.setProperty (id::offset, 48000.0, nullptr);          // room to reveal earlier source
        const double got = clipops::resizeOverlap (s3, a3, b3, 1.0);   // centre 1.75
        expectWithinAbsoluteError (got, 1.0, 1.0e-12);
        expectWithinAbsoluteError ((double) a3[id::length], 2.25, 1.0e-9);  // ends at 2.25
        expectWithinAbsoluteError ((double) b3[id::start], 1.25, 1.0e-9);
        expectWithinAbsoluteError ((double) b3[id::length], 2.25, 1.0e-9);  // far end fixed at 3.5
        expectWithinAbsoluteError ((double) b3[id::offset], 36000.0, 1.0);  // revealed 0.25 s

        beginTest ("resizeOverlap to zero butts the clips");
        const double zero = clipops::resizeOverlap (s3, a3, b3, 0.0);
        expectWithinAbsoluteError (zero, 0.0, 1.0e-12);
        expectWithinAbsoluteError ((double) a3[id::start] + (double) a3[id::length],
                                   (double) b3[id::start], 1.0e-9);
    }
};

// ===========================================================================

static TempoMapTests tempoMapTests;
static SessionTests sessionTests;
static ClipOpsTests clipOpsTests;
static CrossfadeTests crossfadeTests;
static RackTests rackTests;
static InstrumentTests instrumentTests;
static PatcherTests patcherTests;
static AnalysisTests analysisTests;
static UpdaterTests updaterTests;
static StretchTests stretchTests;
static ClipLoopSlipTests clipLoopSlipTests;
static LoopedRenderTests loopedRenderTests;
static MidiLoopExpandTests midiLoopExpandTests;
static CrossfadeHandleTests crossfadeHandleTests;

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::cout << (failures == 0 ? "\nALL TESTS PASSED\n"
                                : "\n" + juce::String (failures) + " FAILURE(S)\n");
    return failures == 0 ? 0 : 1;
}
