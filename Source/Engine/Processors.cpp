#include "Processors.h"

namespace dg
{

ChannelStripProcessor::ChannelStripProcessor (const String& trackName)
    : BasicProcessor ("Strip: " + trackName)
{
    addParameter (gainDb = new juce::AudioParameterFloat ({ "gain", 1 }, "Gain",
                       juce::NormalisableRange<float> (-60.0f, 12.0f, 0.01f), 0.0f));
    addParameter (pan = new juce::AudioParameterFloat ({ "pan", 1 }, "Pan",
                       juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    addParameter (mute = new juce::AudioParameterBool ({ "mute", 1 }, "Mute", false));
}

void ChannelStripProcessor::prepareToPlay (double sr, int)
{
    smGainL.reset (sr, 0.02);
    smGainR.reset (sr, 0.02);
    tapPre->clear();
    tapPost->clear();
}

void ChannelStripProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int n = buffer.getNumSamples();

    // master~ rings land pre-fader, pre-tap (so the master fader rides them)
    if (const auto* inj = injects.load())
        for (const auto& ring : *inj)
            ring->consumeAdd (buffer.getWritePointer (0),
                              buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr,
                              n);

    // WIRES `strip` seizure: fresh stamps override the params, stale ones release
    const int stamp = control->blockStamp.fetch_add (1) + 1;
    auto fresh = [stamp] (const std::atomic<int>& s) { return stamp - s.load() <= 1; };
    const bool muted = (fresh (control->muteStamp) ? control->mute.load() > 0.5f : mute->get())
                       || soloMuted.load() || forceMute.load();
    const float gdb = fresh (control->gainStamp)
                          ? juce::jlimit (-60.0f, 12.0f, control->gainDb.load()) : gainDb->get();
    const float g = muted ? 0.0f : juce::Decibels::decibelsToGain (gdb, -60.0f);
    const float p = fresh (control->panStamp)
                        ? juce::jlimit (-1.0f, 1.0f, control->pan.load()) : pan->get();
    control->curGainDb.store (gdb);
    control->curPan.store (p);
    control->curMute.store (muted ? 1.0f : 0.0f);

    smGainL.setTargetValue (g * (p <= 0.0f ? 1.0f : 1.0f - p));
    smGainR.setTargetValue (g * (p >= 0.0f ? 1.0f : 1.0f + p));

    float* l = buffer.getWritePointer (0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    float pkL = 0.0f, pkR = 0.0f;

    tapPre->write (l, r, n);                    // chan~ pre-fader point

    for (int i = 0; i < n; ++i)
    {
        l[i] *= smGainL.getNextValue();
        pkL = juce::jmax (pkL, std::abs (l[i]));
        if (r != nullptr)
        {
            r[i] *= smGainR.getNextValue();
            pkR = juce::jmax (pkR, std::abs (r[i]));
        }
    }
    peakL.store (juce::jmax (peakL.load() * 0.75f, pkL));
    peakR.store (juce::jmax (peakR.load() * 0.75f, r != nullptr ? pkR : pkL));

    tapPost->write (l, r, n);                   // chan~ post-fader point
}

SendProcessor::SendProcessor (const String& name) : BasicProcessor (name)
{
    addParameter (levelDb = new juce::AudioParameterFloat ({ "send", 1 }, "Send",
                      juce::NormalisableRange<float> (-60.0f, 6.0f, 0.01f), -60.0f));
}

void SendProcessor::prepareToPlay (double sr, int)
{
    sm.reset (sr, 0.02);
}

void SendProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const float db = levelDb->get();
    sm.setTargetValue (db <= -59.5f ? 0.0f : juce::Decibels::decibelsToGain (db));
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float g = sm.getNextValue();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }
}

// ---------------------------------------------------------------------------

namespace
{
    struct ToneSound : public juce::SynthesiserSound
    {
        bool appliesToNote (int) override    { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    struct ToneVoice : public juce::SynthesiserVoice
    {
        bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<ToneSound*> (s) != nullptr; }

        void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
        {
            phase = 0.0;
            inc = juce::MidiMessage::getMidiNoteInHertz (note) / getSampleRate();
            level = velocity * 0.25f;
            adsr.setSampleRate (getSampleRate());
            adsr.setParameters ({ 0.004f, 0.25f, 0.55f, 0.12f });
            adsr.noteOn();
            lp = 0.0f;
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff) adsr.noteOff();
            else { adsr.reset(); clearCurrentNote(); }
        }

        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}

        void renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples) override
        {
            if (! adsr.isActive()) { clearCurrentNote(); return; }
            const float k = 0.18f;
            for (int i = 0; i < numSamples; ++i)
            {
                const float saw = (float) (2.0 * phase - 1.0);
                phase += inc; if (phase >= 1.0) phase -= 1.0;
                lp += k * (saw - lp);
                const float s = lp * level * adsr.getNextSample();
                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    out.addSample (ch, startSample + i, s);
                if (! adsr.isActive()) { clearCurrentNote(); break; }
            }
        }

        double phase = 0.0, inc = 0.0;
        float level = 0.0f, lp = 0.0f;
        juce::ADSR adsr;
    };
}

// ---------------------------------------------------------------------------

namespace
{
    struct AnySound : public juce::SynthesiserSound
    {
        bool appliesToNote (int) override    { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    struct RustVoice : public juce::SynthesiserVoice
    {
        explicit RustVoice (BuiltinInstrument& o) : owner (o) {}
        bool canPlaySound (juce::SynthesiserSound*) override { return true; }
        void startNote (int note, float vel, juce::SynthesiserSound*, int) override
        {
            freq = juce::MidiMessage::getMidiNoteInHertz (note);
            cp = mp = 0; level = vel * 0.4f; env = 1.0f;
            decayK = (float) std::exp (-1.0 / (owner.p3->get() * getSampleRate()));
        }
        void stopNote (float, bool tail) override { if (! tail) { env = 0; clearCurrentNote(); } released = true; }
        void pitchWheelMoved (int) override {} void controllerMoved (int, int) override {}
        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int n) override
        {
            if (env < 1.0e-4f) { if (getCurrentlyPlayingNote() != 0) clearCurrentNote(); return; }
            const double ratio = owner.p1->get(), index = owner.p2->get();
            const double ci = freq / getSampleRate(), mi = ci * ratio;
            for (int i = 0; i < n; ++i)
            {
                const float s = (float) std::sin (cp * juce::MathConstants<double>::twoPi
                                                  + index * env * std::sin (mp * juce::MathConstants<double>::twoPi))
                                * level * env;
                cp += ci; mp += mi; env *= decayK;
                if (released) env *= 0.9995f;
                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    out.addSample (ch, start + i, s);
            }
            if (env < 1.0e-4f) clearCurrentNote();
        }
        BuiltinInstrument& owner;
        double freq = 440, cp = 0, mp = 0;
        float level = 0, env = 0, decayK = 0.999f;
        bool released = false;
    };

    struct GravelVoice : public juce::SynthesiserVoice
    {
        explicit GravelVoice (BuiltinInstrument& o) : owner (o) {}
        bool canPlaySound (juce::SynthesiserSound*) override { return true; }
        void startNote (int note, float vel, juce::SynthesiserSound*, int) override
        {
            freq = juce::MidiMessage::getMidiNoteInHertz (note);
            level = vel * 0.6f; env = 1.0f; phase = 0; lp = 0; thumpEnv = 1.0f;
            decayK = (float) std::exp (-1.0 / (owner.p2->get() * getSampleRate()));
        }
        void stopNote (float, bool tail) override { if (! tail) { env = 0; clearCurrentNote(); } }
        void pitchWheelMoved (int) override {} void controllerMoved (int, int) override {}
        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int n) override
        {
            if (env < 1.0e-4f) { if (getCurrentlyPlayingNote() != 0) clearCurrentNote(); return; }
            const float k = juce::jlimit (0.01f, 0.99f,
                (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * owner.p1->get() / getSampleRate())));
            const double thumpSemis = owner.p3->get();
            for (int i = 0; i < n; ++i)
            {
                lp += k * ((rng.nextFloat() * 2 - 1) - lp);
                const double f = freq * std::pow (2.0, thumpSemis * thumpEnv / 12.0);
                phase += f / getSampleRate();
                const float thump = (float) std::sin (phase * juce::MathConstants<double>::twoPi) * thumpEnv;
                thumpEnv *= 0.9992f;
                const float s = (lp * 0.9f + thump * 0.8f) * level * env;
                env *= decayK;
                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    out.addSample (ch, start + i, s);
            }
            if (env < 1.0e-4f) clearCurrentNote();
        }
        BuiltinInstrument& owner;
        juce::Random rng;
        double freq = 100, phase = 0;
        float level = 0, env = 0, decayK = 0.99f, lp = 0, thumpEnv = 0;
    };

    struct HymnVoice : public juce::SynthesiserVoice
    {
        explicit HymnVoice (BuiltinInstrument& o) : owner (o) {}
        bool canPlaySound (juce::SynthesiserSound*) override { return true; }
        void startNote (int note, float vel, juce::SynthesiserSound*, int) override
        {
            const double f = juce::MidiMessage::getMidiNoteInHertz (note);
            const double cents = owner.p4->get();
            inc1 = f * std::pow (2.0, cents / 1200.0) / getSampleRate();
            inc2 = f * std::pow (2.0, -cents / 1200.0) / getSampleRate();
            ph1 = 0; ph2 = 0.37; level = vel * 0.22f; lp = 0;
            adsr.setSampleRate (getSampleRate());
            adsr.setParameters ({ owner.p2->get(), 0.4f, 0.8f, owner.p3->get() });
            adsr.noteOn();
        }
        void stopNote (float, bool tail) override
        {
            if (tail) adsr.noteOff();
            else { adsr.reset(); clearCurrentNote(); }
        }
        void pitchWheelMoved (int) override {} void controllerMoved (int, int) override {}
        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int n) override
        {
            if (! adsr.isActive()) { if (getCurrentlyPlayingNote() != 0) clearCurrentNote(); return; }
            const float k = juce::jlimit (0.005f, 0.9f,
                (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * owner.p1->get() / getSampleRate())));
            for (int i = 0; i < n; ++i)
            {
                const float saws = (float) (2.0 * ph1 - 1.0) * 0.5f + (float) (2.0 * ph2 - 1.0) * 0.5f;
                ph1 += inc1; if (ph1 >= 1) ph1 -= 1;
                ph2 += inc2; if (ph2 >= 1) ph2 -= 1;
                lp += k * (saws - lp);
                const float s = lp * level * adsr.getNextSample();
                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    out.addSample (ch, start + i, s);
                if (! adsr.isActive()) { clearCurrentNote(); break; }
            }
        }
        BuiltinInstrument& owner;
        double ph1 = 0, ph2 = 0, inc1 = 0, inc2 = 0;
        float level = 0, lp = 0;
        juce::ADSR adsr;
    };
}

namespace
{
    // RUBBLE: five synthesized drums, note-mapped GM-ish (36 kick, 38/40 snare,
    // 39 clap, 42/44 closed hat, 46 open hat; anything else cycles all five)
    struct KitVoice : public juce::SynthesiserVoice
    {
        explicit KitVoice (BuiltinInstrument& o) : owner (o) {}
        enum class Drum { kick, snare, clap, chh, ohh };
        bool canPlaySound (juce::SynthesiserSound*) override { return true; }

        void startNote (int note, float vel, juce::SynthesiserSound*, int) override
        {
            switch (note)
            {
                case 35: case 36: drum = Drum::kick; break;
                case 38: case 40: drum = Drum::snare; break;
                case 39:          drum = Drum::clap; break;
                case 42: case 44: drum = Drum::chh; break;
                case 46:          drum = Drum::ohh; break;
                default:          drum = (Drum) (note % 5); break;
            }
            level = vel;
            t = 0; phase = 0; env = 1.0f; lp = 0; hp = 0;
            const double tune = std::pow (2.0, owner.p1->get() / 12.0);
            const float decayMul = owner.p2->get();
            switch (drum)
            {
                case Drum::kick:  f0 = 120.0 * tune; f1 = 44.0 * tune; decay = 0.28f * decayMul; break;
                case Drum::snare: f0 = 190.0 * tune; f1 = 160.0 * tune; decay = 0.16f * decayMul; break;
                case Drum::clap:  f0 = 0; f1 = 0; decay = 0.22f * decayMul; break;
                case Drum::chh:   f0 = 0; f1 = 0; decay = 0.05f * decayMul; break;
                case Drum::ohh:   f0 = 0; f1 = 0; decay = 0.4f * decayMul; break;
            }
            envK = (float) std::exp (-1.0 / (decay * getSampleRate()));
        }
        void stopNote (float, bool) override {}     // drums always ring out
        void pitchWheelMoved (int) override {} void controllerMoved (int, int) override {}

        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int n) override
        {
            if (env < 1.0e-4f) { if (getCurrentlyPlayingNote() != 0) clearCurrentNote(); return; }
            const double sr = getSampleRate();
            const float tone = owner.p3->get();
            const float drive = juce::Decibels::decibelsToGain (owner.p4->get());

            for (int i = 0; i < n; ++i)
            {
                float s = 0;
                const float noise = rng.nextFloat() * 2.0f - 1.0f;
                switch (drum)
                {
                    case Drum::kick:
                    {
                        const double f = f1 + (f0 - f1) * std::exp (-t * 28.0);
                        phase += f / sr;
                        s = (float) std::sin (phase * juce::MathConstants<double>::twoPi) * env
                            + (t < 0.004 ? noise * 0.4f * env : 0.0f);
                        break;
                    }
                    case Drum::snare:
                    {
                        phase += f0 / sr;
                        lp += (0.12f + tone * 0.5f) * (noise - lp);
                        s = ((float) std::sin (phase * juce::MathConstants<double>::twoPi) * 0.4f
                             + lp * (0.7f + tone * 0.6f)) * env;
                        break;
                    }
                    case Drum::clap:
                    {
                        // three retriggered bursts then the tail
                        const float burst = (t < 0.010 || (t > 0.012 && t < 0.022) || (t > 0.025 && t < 0.035))
                                                ? 1.0f : env;
                        lp += (0.2f + tone * 0.4f) * (noise - lp);
                        s = (noise - lp) * burst * 0.9f;
                        break;
                    }
                    case Drum::chh:
                    case Drum::ohh:
                    {
                        hp += (0.5f + tone * 0.35f) * (noise - hp);
                        s = (noise - hp) * env;     // highpassed metal-ish noise
                        break;
                    }
                }
                env *= envK;
                t += 1.0 / sr;
                s = std::tanh (s * drive * level) * 0.8f;
                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    out.addSample (ch, start + i, s);
            }
            if (env < 1.0e-4f) clearCurrentNote();
        }

        BuiltinInstrument& owner;
        juce::Random rng;
        Drum drum = Drum::kick;
        double f0 = 100, f1 = 50, phase = 0, t = 0;
        float level = 0, env = 0, envK = 0.99f, decay = 0.2f, lp = 0, hp = 0;
    };
}

BuiltinInstrument::BuiltinInstrument (Kind k)
    : BasicProcessor (k == Kind::rust ? "RUST (built-in)" : k == Kind::gravel ? "GRAVEL (built-in)"
                      : k == Kind::hymn ? "HYMN (built-in)" : "RUBBLE (built-in)",
                      true, false),
      kind (k)
{
    auto range = [] (float lo, float hi, float skewCentre = 0.0f)
    {
        juce::NormalisableRange<float> r (lo, hi);
        if (skewCentre > 0) r.setSkewForCentre (skewCentre);
        return r;
    };
    if (kind == Kind::rust)
    {
        addParameter (p1 = new juce::AudioParameterFloat ({ "ratio", 1 }, "Ratio", range (0.25f, 8.0f), 2.01f));
        addParameter (p2 = new juce::AudioParameterFloat ({ "index", 1 }, "FM Index", range (0.0f, 12.0f), 4.0f));
        addParameter (p3 = new juce::AudioParameterFloat ({ "decay", 1 }, "Decay s", range (0.05f, 6.0f, 1.0f), 1.2f));
        addParameter (p4 = new juce::AudioParameterFloat ({ "unused", 1 }, "-", range (0.0f, 1.0f), 0.0f));
        for (int i = 0; i < 12; ++i) synth.addVoice (new RustVoice (*this));
    }
    else if (kind == Kind::gravel)
    {
        addParameter (p1 = new juce::AudioParameterFloat ({ "tone", 1 }, "Tone Hz", range (100.0f, 9000.0f, 1500.0f), 1800.0f));
        addParameter (p2 = new juce::AudioParameterFloat ({ "decay", 1 }, "Decay s", range (0.02f, 1.5f, 0.3f), 0.25f));
        addParameter (p3 = new juce::AudioParameterFloat ({ "thump", 1 }, "Thump", range (0.0f, 48.0f), 24.0f));
        addParameter (p4 = new juce::AudioParameterFloat ({ "unused", 1 }, "-", range (0.0f, 1.0f), 0.0f));
        for (int i = 0; i < 12; ++i) synth.addVoice (new GravelVoice (*this));
    }
    else if (kind == Kind::hymn)
    {
        addParameter (p1 = new juce::AudioParameterFloat ({ "cutoff", 1 }, "Cutoff Hz", range (100.0f, 12000.0f, 1800.0f), 2200.0f));
        addParameter (p2 = new juce::AudioParameterFloat ({ "attack", 1 }, "Attack s", range (0.01f, 3.0f, 0.6f), 0.8f));
        addParameter (p3 = new juce::AudioParameterFloat ({ "release", 1 }, "Release s", range (0.1f, 8.0f, 2.0f), 2.5f));
        addParameter (p4 = new juce::AudioParameterFloat ({ "detune", 1 }, "Detune ct", range (0.0f, 30.0f), 10.0f));
        for (int i = 0; i < 10; ++i) synth.addVoice (new HymnVoice (*this));
    }
    else // kit
    {
        addParameter (p1 = new juce::AudioParameterFloat ({ "tune", 1 }, "Tune st", range (-12.0f, 12.0f), 0.0f));
        addParameter (p2 = new juce::AudioParameterFloat ({ "decay", 1 }, "Decay x", range (0.3f, 2.5f), 1.0f));
        addParameter (p3 = new juce::AudioParameterFloat ({ "tone", 1 }, "Tone", range (0.0f, 1.0f), 0.5f));
        addParameter (p4 = new juce::AudioParameterFloat ({ "drive", 1 }, "Drive dB", range (0.0f, 24.0f), 4.0f));
        for (int i = 0; i < 10; ++i) synth.addVoice (new KitVoice (*this));
    }
    synth.addSound (new AnySound());
}

std::unique_ptr<juce::AudioProcessor> BuiltinInstrument::create (const String& name)
{
    if (name == "rust")   return std::make_unique<BuiltinInstrument> (Kind::rust);
    if (name == "gravel") return std::make_unique<BuiltinInstrument> (Kind::gravel);
    if (name == "hymn")   return std::make_unique<BuiltinInstrument> (Kind::hymn);
    if (name == "rubble") return std::make_unique<BuiltinInstrument> (Kind::kit);
    return nullptr;
}

void BuiltinInstrument::prepareToPlay (double sr, int)
{
    synth.setCurrentPlaybackSampleRate (sr);
}

void BuiltinInstrument::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
}

void BuiltinInstrument::getStateInformation (juce::MemoryBlock& mb)
{
    juce::MemoryOutputStream out (mb, false);
    for (auto* p : { p1, p2, p3, p4 })
        out.writeFloat (p->get());
}

void BuiltinInstrument::setStateInformation (const void* data, int size)
{
    juce::MemoryInputStream in (data, (size_t) size, false);
    for (auto* p : { p1, p2, p3, p4 })
        if (in.getNumBytesRemaining() >= 4)
            p->setValueNotifyingHost (p->convertTo0to1 (in.readFloat()));
}

SimpleSynthProcessor::SimpleSynthProcessor() : BasicProcessor ("GlitchTone (built-in)", true, false)
{
    for (int i = 0; i < 8; ++i) synth.addVoice (new ToneVoice());
    synth.addSound (new ToneSound());
}

void SimpleSynthProcessor::prepareToPlay (double sr, int)
{
    synth.setCurrentPlaybackSampleRate (sr);
}

void SimpleSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
}

} // namespace dg
