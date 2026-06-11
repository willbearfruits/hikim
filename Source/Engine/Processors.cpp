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
}

void ChannelStripProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int n = buffer.getNumSamples();
    const bool muted = mute->get() || soloMuted.load() || forceMute.load();
    const float g = muted ? 0.0f : juce::Decibels::decibelsToGain (gainDb->get(), -60.0f);
    const float p = pan->get();
    smGainL.setTargetValue (g * (p <= 0.0f ? 1.0f : 1.0f - p));
    smGainR.setTargetValue (g * (p >= 0.0f ? 1.0f : 1.0f + p));

    float* l = buffer.getWritePointer (0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    float pkL = 0.0f, pkR = 0.0f;

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
