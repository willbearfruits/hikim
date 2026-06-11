// RUIN headless test suite. Run: ./build/ruin_tests_artefacts/ruin-tests
// Covers the model, clip operations, comp crossfades, TEETH, and instruments.

#include <JuceHeader.h>
#include "../Source/Model/Session.h"
#include "../Source/Model/ClipOps.h"
#include "../Source/Engine/TempoMap.h"
#include "../Source/Engine/ClipPlayer.h"
#include "../Source/Engine/Processors.h"
#include "../Source/Engine/StretchCache.h"
#include "../Source/Engine/Analysis.h"
#include "../Source/Rack/RackProcessor.h"

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

// ===========================================================================

static TempoMapTests tempoMapTests;
static SessionTests sessionTests;
static ClipOpsTests clipOpsTests;
static CrossfadeTests crossfadeTests;
static RackTests rackTests;
static InstrumentTests instrumentTests;
static AnalysisTests analysisTests;
static StretchTests stretchTests;

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
