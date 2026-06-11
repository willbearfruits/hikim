#include "RackModules.h"

namespace dg
{

static constexpr double kDivBeats[] = { 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.75, 0.25, 1.0 / 6.0, 0.125, 0.0625 };
static constexpr double kGateBeats[] = { 1.0, 0.5, 0.25, 0.125 };
static constexpr int kGridSizes[] = { 4, 8, 16, 32 };

// ===================================================================== 1 beat repeat

BeatRepeatModule::BeatRepeatModule (juce::AudioProcessorValueTreeState& s) : RackModule (s)
{
    pDiv = raw ("br_div"); pChance = raw ("br_chance"); pRatchet = raw ("br_ratchet");
    pPitch = raw ("br_pitch"); pDecay = raw ("br_decay");
}

void BeatRepeatModule::prepare (double, int)
{
    mask = (1 << 19) - 1;
    ring.setSize (2, mask + 1);
    reset();
}

void BeatRepeatModule::reset()
{
    ring.clear(); wp = 0; divPhase = 0; repeating = false; repIndex = 0; repMix = 0.0f;
}

void BeatRepeatModule::process (juce::AudioBuffer<float>& buf, const ModuleContext& ctx)
{
    const int n = buf.getNumSamples();
    const double spb = 60.0 / ctx.bpm * ctx.sr;                       // samples per quarter
    const double divLen = juce::jmax (32.0, kDivBeats[(int) pDiv->load()] * spb);
    const int ratchet = juce::jmax (1, (int) pRatchet->load());
    const double chunkLen = divLen / ratchet;
    const float chance = pChance->load();
    const float decay = pDecay->load();
    const double pitchRate = std::pow (2.0, pPitch->load() / 12.0);

    float* l = buf.getWritePointer (0);
    float* r = buf.getWritePointer (juce::jmin (1, buf.getNumChannels() - 1));
    float* rl = ring.getWritePointer (0);
    float* rr = ring.getWritePointer (1);

    if (ctx.playing)
        divPhase = std::fmod (ctx.ppq * spb, divLen);                 // stay transport-locked

    // declick: ~3ms engage/release crossfade + edge fades on every chunk restart
    const float mixStep = (float) (1.0 / juce::jmax (8.0, 0.003 * ctx.sr));
    const double edgeFade = juce::jmin (96.0, chunkLen * 0.25);

    for (int i = 0; i < n; ++i)
    {
        rl[wp] = l[i]; rr[wp] = r[i];

        divPhase += 1.0;
        if (divPhase >= divLen)
        {
            divPhase -= divLen;
            if (rng.nextFloat() < chance)
            {
                repeating = true;
                capStart = (wp - (int) divLen) & mask;
                repIndex = 0; chunkPos = 0; readFrac = 0; repGain = 1.0f;
            }
            else
                repeating = false;
        }

        const float target = repeating ? 1.0f : 0.0f;
        repMix += juce::jlimit (-mixStep, mixStep, target - repMix);

        if (repMix > 1.0e-4f)
        {
            if (chunkPos >= chunkLen)
            {
                chunkPos = 0; readFrac = 0; ++repIndex;
                repGain *= juce::jmax (0.05f, decay);
            }
            const int rp = (capStart + (int) readFrac) & mask;
            const float eg = (float) juce::jmax (0.0, juce::jmin (1.0, chunkPos / edgeFade,
                                                                  (chunkLen - chunkPos) / edgeFade));
            const float g = repGain * eg;
            l[i] += repMix * (rl[rp] * g - l[i]);
            r[i] += repMix * (rr[rp] * g - r[i]);
            readFrac += std::pow (pitchRate, (double) repIndex);
            chunkPos += 1.0;
        }
        wp = (wp + 1) & mask;
    }
}

// ===================================================================== 2 scrambler

ScramblerModule::ScramblerModule (juce::AudioProcessorValueTreeState& s) : RackModule (s)
{
    pGrid = raw ("sc_grid"); pShuffle = raw ("sc_shuffle"); pReverse = raw ("sc_reverse");
    pDrop = raw ("sc_drop"); pSpray = raw ("sc_spray"); pSeed = raw ("sc_seed");
}

void ScramblerModule::prepare (double, int)
{
    ringLen = 1 << 20;
    ring.setSize (2, ringLen);
    reset();
}

void ScramblerModule::reset()
{
    ring.clear(); curSlice = -1; readPos = 0; freePpq = 0;
}

void ScramblerModule::process (juce::AudioBuffer<float>& buf, const ModuleContext& ctx)
{
    const int n = buf.getNumSamples();
    const double spb = 60.0 / ctx.bpm * ctx.sr;
    const int grid = kGridSizes[(int) pGrid->load()];
    const double barBeats = ctx.beatsPerBar;
    const double barLen = juce::jmin ((double) ringLen, juce::jmax (256.0, barBeats * spb));
    const double sliceLen = barLen / grid;
    const float shuffle = pShuffle->load(), reverse = pReverse->load(), drop = pDrop->load();
    const float spray = pSpray->load();
    const int seed = (int) pSeed->load();

    float* l = buf.getWritePointer (0);
    float* r = buf.getWritePointer (juce::jmin (1, buf.getNumChannels() - 1));
    float* rl = ring.getWritePointer (0);
    float* rr = ring.getWritePointer (1);

    double posInBar = ctx.playing
        ? std::fmod (juce::jmax (0.0, ctx.ppq - ctx.barStartPpq), barBeats) * spb
        : freePpq;
    juce::int64 barIndex = ctx.playing ? (juce::int64) (ctx.ppq / juce::jmax (0.001, barBeats)) : 0;

    const double fadeLen = juce::jmin (96.0, sliceLen * 0.1);

    for (int i = 0; i < n; ++i)
    {
        const int widx = juce::jlimit (0, (int) barLen - 1, (int) posInBar);
        rl[widx] = l[i]; rr[widx] = r[i];

        const int slice = juce::jmin (grid - 1, (int) (posInBar / sliceLen));
        if (slice != curSlice)
        {
            curSlice = slice;
            juce::Random pr ((juce::int64) seed * 1000003 + barIndex * 131 + slice * 7919);
            srcSlice = pr.nextFloat() < shuffle ? pr.nextInt (grid) : slice;
            sliceRev = pr.nextFloat() < reverse;
            sliceDrop = pr.nextFloat() < drop;
            const float semis = spray > 0.01f ? (pr.nextFloat() * 2.0f - 1.0f) * spray : 0.0f;
            sliceRate = std::pow (2.0, semis / 12.0);
            readPos = 0;
        }

        float ov = 0.0f, owv = 0.0f;
        if (! sliceDrop)
        {
            const double inSlice = sliceRev ? sliceLen - 1 - std::fmod (readPos, sliceLen)
                                            : std::fmod (readPos, sliceLen);
            const int ridx = juce::jlimit (0, (int) barLen - 1, (int) (srcSlice * sliceLen + inSlice));
            const double edge = posInBar - slice * sliceLen;
            float fade = 1.0f;
            if (edge < fadeLen)                 fade = (float) (edge / fadeLen);
            if (sliceLen - edge < fadeLen)      fade = juce::jmin (fade, (float) ((sliceLen - edge) / fadeLen));
            ov = rl[ridx] * fade; owv = rr[ridx] * fade;
        }
        l[i] = ov; r[i] = owv;

        readPos += sliceRate;
        posInBar += 1.0;
        if (posInBar >= barLen) { posInBar -= barLen; ++barIndex; }
    }
    if (! ctx.playing) freePpq = posInBar;
}

// ===================================================================== 3 granular

GranularModule::GranularModule (juce::AudioProcessorValueTreeState& s) : RackModule (s)
{
    pSize = raw ("gr_size"); pDensity = raw ("gr_density"); pJitter = raw ("gr_jitter");
    pSpray = raw ("gr_spray"); pReverse = raw ("gr_reverse"); pFreeze = raw ("gr_freeze");
}

void GranularModule::prepare (double, int)
{
    mask = (1 << 18) - 1;
    ring.setSize (2, mask + 1);
    reset();
}

void GranularModule::reset()
{
    ring.clear(); wp = 0; spawnTimer = 0;
    for (auto& g : grains) g.active = false;
}

void GranularModule::process (juce::AudioBuffer<float>& buf, const ModuleContext& ctx)
{
    const int n = buf.getNumSamples();
    const bool freeze = pFreeze->load() > 0.5f;
    const double sizeSamp = juce::jlimit (32.0, (double) mask * 0.5, (double) pSize->load() * 0.001 * ctx.sr);
    const double density = juce::jmax (0.2f, pDensity->load());
    const float jitter = pJitter->load(), spray = pSpray->load(), revProb = pReverse->load();

    float* l = buf.getWritePointer (0);
    float* r = buf.getWritePointer (juce::jmin (1, buf.getNumChannels() - 1));
    float* rl = ring.getWritePointer (0);
    float* rr = ring.getWritePointer (1);

    for (int i = 0; i < n; ++i)
    {
        if (! freeze)
        {
            rl[wp] = l[i]; rr[wp] = r[i];
            wp = (wp + 1) & mask;
        }
        l[i] = 0; r[i] = 0;

        if (--spawnTimer <= 0)
        {
            spawnTimer = ctx.sr / density * (0.4 + 1.2 * rng.nextDouble());
            for (auto& g : grains)
            {
                if (g.active) continue;
                g.active = true;
                g.len = juce::jmax (32, (int) (sizeSamp * (0.6 + 0.8 * rng.nextDouble())));
                g.age = 0;
                const double back = sizeSamp + rng.nextDouble() * jitter * (double) mask * 0.9;
                g.pos = (double) ((wp - (juce::int64) back) & (juce::int64) mask);
                double rate = std::pow (2.0, (rng.nextDouble() * 2.0 - 1.0) * spray / 12.0);
                if (rng.nextFloat() < revProb) rate = -rate;
                g.inc = rate;
                g.gain = 0.7f;
                break;
            }
        }

        for (auto& g : grains)
        {
            if (! g.active) continue;
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) g.age / (float) g.len);
            const int p0 = ((int) g.pos) & mask;
            l[i] += rl[p0] * w * g.gain;
            r[i] += rr[p0] * w * g.gain;
            g.pos += g.inc;
            if (g.pos < 0) g.pos += mask + 1;
            if (++g.age >= g.len) g.active = false;
        }
    }
}

// ===================================================================== 4 bitcrush

BitcrushModule::BitcrushModule (juce::AudioProcessorValueTreeState& s) : RackModule (s)
{
    pBits = raw ("bc_bits"); pDown = raw ("bc_down"); pDrive = raw ("bc_drive"); pDither = raw ("bc_dither");
}

void BitcrushModule::process (juce::AudioBuffer<float>& buf, const ModuleContext&)
{
    const int n = buf.getNumSamples();
    const float q = std::pow (2.0f, pBits->load() - 1.0f);
    const int down = juce::jmax (1, (int) pDown->load());
    const float drive = juce::Decibels::decibelsToGain (pDrive->load());
    const float dither = pDither->load();

    for (int ch = 0; ch < juce::jmin (2, buf.getNumChannels()); ++ch)
    {
        float* d = buf.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            if (--holdCount[ch] <= 0)
            {
                holdCount[ch] = down;
                float x = std::tanh (d[i] * drive);
                if (dither > 0) x += (rng.nextFloat() - 0.5f) * dither / q;
                x = std::round (x * q) / q;
                if (dither < 0) x += (rng.nextFloat() - 0.5f) * (-dither) / q;   // anti-dither grit
                held[ch] = x;
            }
            d[i] = held[ch];
        }
    }
}

// ===================================================================== 5 feedback net

FeedbackNetModule::FeedbackNetModule (juce::AudioProcessorValueTreeState& s) : RackModule (s)
{
    pTime = raw ("fb_time"); pSpread = raw ("fb_spread"); pFeedback = raw ("fb_fb");
    pDamp = raw ("fb_damp"); pTuned = raw ("fb_tuned"); pPitch = raw ("fb_pitch");
}

void FeedbackNetModule::prepare (double sr, int)
{
    mask = (1 << 17) - 1;
    lines.setSize (3, mask + 1);
    for (auto& d : delaySm) d.reset (sr, 0.08);
    reset();
}

void FeedbackNetModule::reset()
{
    lines.clear(); wp = 0;
    lpState[0] = lpState[1] = lpState[2] = 0;
}

void FeedbackNetModule::process (juce::AudioBuffer<float>& buf, const ModuleContext& ctx)
{
    const int n = buf.getNumSamples();
    const float fb = pFeedback->load();
    const float dampK = juce::jlimit (0.01f, 0.99f,
        (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * pDamp->load() / ctx.sr)));

    double times[3];
    if (pTuned->load() > 0.5f)
    {
        const double f = juce::MidiMessage::getMidiNoteInHertz ((int) pPitch->load());
        times[0] = ctx.sr / f; times[1] = ctx.sr / (f * 1.5); times[2] = ctx.sr / (f * 2.0);
    }
    else
    {
        const double t0 = pTime->load() * 0.001 * ctx.sr;
        const double sp = pSpread->load();
        times[0] = t0; times[1] = t0 * sp; times[2] = t0 * sp * sp;
    }
    for (int k = 0; k < 3; ++k)
        delaySm[k].setTargetValue ((float) juce::jlimit (8.0, (double) mask - 2, times[k]));

    float* l = buf.getWritePointer (0);
    float* r = buf.getWritePointer (juce::jmin (1, buf.getNumChannels() - 1));
    float* dl[3] = { lines.getWritePointer (0), lines.getWritePointer (1), lines.getWritePointer (2) };

    for (int i = 0; i < n; ++i)
    {
        float y[3];
        for (int k = 0; k < 3; ++k)
        {
            const float dt = delaySm[k].getNextValue();
            const double rp = (double) wp - dt;
            const int ip = ((int) std::floor (rp)) & mask;
            const float frac = (float) (rp - std::floor (rp));
            y[k] = dl[k][ip] + frac * (dl[k][(ip + 1) & mask] - dl[k][ip]);
        }
        const float inL = l[i], inR = r[i];
        const float ins[3] = { inL, (inL + inR) * 0.5f, inR };
        for (int k = 0; k < 3; ++k)
        {
            const float v = ins[k] + fb * 0.7f * (y[(k + 1) % 3] + y[(k + 2) % 3]);
            lpState[k] += dampK * (v - lpState[k]);
            dl[k][wp] = std::tanh (lpState[k]);          // runaway-safe, still screams
        }
        l[i] = y[0] + y[1] * 0.5f;
        r[i] = y[2] + y[1] * 0.5f;
        wp = (wp + 1) & mask;
    }
}

// ===================================================================== 6 gate sequencer

GateSeqModule::GateSeqModule (juce::AudioProcessorValueTreeState& s, std::array<std::atomic<float>, 32>& sv)
    : RackModule (s), steps (sv)
{
    pRate = raw ("gs_rate"); pDepth = raw ("gs_depth"); pSmooth = raw ("gs_smooth");
    pSwing = raw ("gs_swing"); pSteps = raw ("gs_steps");
}

void GateSeqModule::prepare (double, int) { env = 1.0f; freePpq = 0; }

void GateSeqModule::process (juce::AudioBuffer<float>& buf, const ModuleContext& ctx)
{
    const int n = buf.getNumSamples();
    const double stepBeats = kGateBeats[(int) pRate->load()];
    const int numSteps = juce::jlimit (2, 32, (int) pSteps->load());
    const float depth = pDepth->load();
    const float swing = pSwing->load();
    const float k = juce::jlimit (0.0005f, 0.9f,
        (float) (1.0 - std::exp (-1.0 / juce::jmax (8.0, pSmooth->load() * 0.001 * ctx.sr))));
    const double beatsPerSample = ctx.bpm / 60.0 / ctx.sr;

    float* l = buf.getWritePointer (0);
    float* r = buf.getWritePointer (juce::jmin (1, buf.getNumChannels() - 1));
    double ppq = ctx.playing ? ctx.ppq : freePpq;

    for (int i = 0; i < n; ++i)
    {
        const double stepPos = ppq / stepBeats;
        int idx = (int) stepPos;
        const double frac = stepPos - idx;
        if ((idx & 1) == 1 && frac < swing * 0.5) --idx;   // swing delays odd steps
        const float target = 1.0f - depth * (1.0f - steps[(size_t) (idx % numSteps)].load());
        env += k * (target - env);
        l[i] *= env; r[i] *= env;
        ppq += beatsPerSample;
    }
    if (! ctx.playing) freePpq = ppq;
}

// ===================================================================== 7 data mangle

DataMangleModule::DataMangleModule (juce::AudioProcessorValueTreeState& s) : RackModule (s)
{
    pShRate = raw ("dm_shrate"); pShAmt = raw ("dm_shamt"); pDrop = raw ("dm_drop");
    pHold = raw ("dm_hold"); pFlip = raw ("dm_flip");
}

void DataMangleModule::prepare (double sr, int)
{
    winLen = juce::jmax (64, (int) (sr * 0.012));
    holdBuf.setSize (2, juce::jmax (256, (int) (sr * 0.1)));
    reset();
}

void DataMangleModule::reset()
{
    shVal[0] = shVal[1] = 0; shCount = 0; winCount = 0; winMuted = false;
    holdLen = 0; holdPos = 0; holdRepeats = 0;
}

void DataMangleModule::process (juce::AudioBuffer<float>& buf, const ModuleContext& ctx)
{
    const int n = buf.getNumSamples();
    const double shPeriod = ctx.sr / juce::jmax (20.0f, pShRate->load());
    const float shAmt = pShAmt->load(), drop = pDrop->load(), holdP = pHold->load(), flip = pFlip->load();

    float* l = buf.getWritePointer (0);
    float* r = buf.getWritePointer (juce::jmin (1, buf.getNumChannels() - 1));
    float* hl = holdBuf.getWritePointer (0);
    float* hr = holdBuf.getWritePointer (1);
    const int holdCap = holdBuf.getNumSamples();

    for (int i = 0; i < n; ++i)
    {
        if (--winCount <= 0)
        {
            winCount = winLen;
            winMuted = rng.nextFloat() < drop;
            if (holdRepeats <= 0 && rng.nextFloat() < holdP * 0.5f)
            {
                holdLen = juce::jmin (holdCap, winLen * (1 + rng.nextInt (4)));
                holdPos = 0;
                holdRepeats = 1 + rng.nextInt (3);
            }
        }

        float xl = l[i], xr = r[i];

        // glitch hold: capture a tiny window, then loop it
        if (holdRepeats > 0 && holdLen > 0)
        {
            if (holdPos < holdLen) { hl[holdPos] = xl; hr[holdPos] = xr; }
            else
            {
                const int rp = holdPos % holdLen;
                xl = hl[rp]; xr = hr[rp];
            }
            if (++holdPos >= holdLen * (holdRepeats + 1)) holdRepeats = 0;
        }

        // randomized sample & hold
        if (--shCount <= 0)
        {
            shCount = shPeriod * (0.5 + rng.nextDouble());
            shVal[0] = xl; shVal[1] = xr;
        }
        xl += shAmt * (shVal[0] - xl);
        xr += shAmt * (shVal[1] - xr);

        // bit flips on the int16 representation
        if (flip > 0.001f && rng.nextFloat() < flip * 0.04f)
        {
            auto v = (juce::int16) juce::jlimit (-32768.0f, 32767.0f, xl * 32767.0f);
            v = (juce::int16) (v ^ (1 << rng.nextInt (13)));
            xl = (float) v / 32767.0f;
        }

        if (winMuted) { xl = 0; xr = 0; }
        l[i] = xl; r[i] = xr;
    }
}

// ===================================================================== 8 tape

TapeModule::TapeModule (juce::AudioProcessorValueTreeState& s) : RackModule (s)
{
    pWow = raw ("tp_wow"); pFlutter = raw ("tp_flutter"); pSat = raw ("tp_sat");
    pHiss = raw ("tp_hiss"); pAge = raw ("tp_age");
}

void TapeModule::prepare (double, int)
{
    mask = 8191;
    line.setSize (2, mask + 1);
    reset();
}

void TapeModule::reset()
{
    line.clear(); wp = 0; wowPhase = 0; flutPhase = 0;
    lpState[0] = lpState[1] = 0; dipGain = dipTarget = 1.0f; dipCount = 0;
}

void TapeModule::process (juce::AudioBuffer<float>& buf, const ModuleContext& ctx)
{
    const int n = buf.getNumSamples();
    const float age = pAge->load();
    const float wow = pWow->load() * (1.0f + age);
    const float flutter = pFlutter->load();
    const float g = juce::Decibels::decibelsToGain (pSat->load()) * 0.9f + 0.1f;
    const float invTanh = 1.0f / std::tanh (juce::jmax (0.2f, g));
    const float hiss = pHiss->load() + age * 0.15f;
    const float cutoff = 18000.0f * (1.0f - age * 0.85f) + 400.0f;
    const float lpK = juce::jlimit (0.01f, 0.999f,
        (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * cutoff / ctx.sr)));

    float* l = buf.getWritePointer (0);
    float* r = buf.getWritePointer (juce::jmin (1, buf.getNumChannels() - 1));
    float* tl = line.getWritePointer (0);
    float* tr = line.getWritePointer (1);

    const double base = 0.03 * ctx.sr;
    const double wowInc = 0.6 / ctx.sr, flutInc = 6.7 / ctx.sr;

    for (int i = 0; i < n; ++i)
    {
        tl[wp] = l[i]; tr[wp] = r[i];

        flutNoise += 0.002f * ((rng.nextFloat() * 2 - 1) - flutNoise);
        const double mod = wow * 0.004 * ctx.sr * std::sin (wowPhase * juce::MathConstants<double>::twoPi)
                         + flutter * 0.0007 * ctx.sr * (std::sin (flutPhase * juce::MathConstants<double>::twoPi) + flutNoise * 4.0);
        wowPhase += wowInc; if (wowPhase >= 1) wowPhase -= 1;
        flutPhase += flutInc; if (flutPhase >= 1) flutPhase -= 1;

        const double rp = (double) wp - (base + mod);
        const int ip = ((int) std::floor (rp)) & mask;
        const float frac = (float) (rp - std::floor (rp));
        float xl = tl[ip] + frac * (tl[(ip + 1) & mask] - tl[ip]);
        float xr = tr[ip] + frac * (tr[(ip + 1) & mask] - tr[ip]);

        xl = std::tanh (xl * g) * invTanh;
        xr = std::tanh (xr * g) * invTanh;

        lpState[0] += lpK * (xl - lpState[0]);
        lpState[1] += lpK * (xr - lpState[1]);
        xl = lpState[0]; xr = lpState[1];

        hissLp += 0.25f * ((rng.nextFloat() * 2 - 1) - hissLp);
        const float hs = hissLp * hiss * 0.012f;

        if (--dipCount <= 0)
        {
            dipCount = (int) (ctx.sr * 0.3);
            dipTarget = (age > 0.01f && rng.nextFloat() < age * 0.3f)
                            ? 1.0f - age * (0.3f + 0.4f * rng.nextFloat()) : 1.0f;
        }
        dipGain += 0.0008f * (dipTarget - dipGain);

        l[i] = (xl + hs) * dipGain;
        r[i] = (xr + hs) * dipGain;
        wp = (wp + 1) & mask;
    }
}

// ===================================================================== 9 convolution junk

ConvolutionJunkModule::ConvolutionJunkModule (juce::AudioProcessorValueTreeState& s) : RackModule (s)
{
    irFormats.registerBasicFormats();
}

void ConvolutionJunkModule::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    conv.prepare ({ sampleRate, (juce::uint32) maxBlock, 2 });
}

void ConvolutionJunkModule::reset() { conv.reset(); }

void ConvolutionJunkModule::loadIR (const File& f, int mangleMode)
{
    std::unique_ptr<juce::AudioFormatReader> reader (irFormats.createReaderFor (f));
    if (reader == nullptr) return;

    const int len = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 5));
    if (len < 16) return;
    juce::AudioBuffer<float> ir (juce::jmin (2, (int) reader->numChannels), len);
    reader->read (&ir, 0, len, 0, true, ir.getNumChannels() > 1);

    juce::Random prng (0x5151);
    if (mangleMode == 1)          // reverse
        ir.reverse (0, len);
    else if (mangleMode == 2)     // crush
        for (int ch = 0; ch < ir.getNumChannels(); ++ch)
        {
            float* d = ir.getWritePointer (ch);
            for (int i = 0; i < len; ++i) d[i] = std::round (d[i] * 8.0f) / 8.0f;
        }
    else if (mangleMode == 3)     // chop
        for (int w = 0; w < len; w += 512)
            if (prng.nextBool())
                for (int ch = 0; ch < ir.getNumChannels(); ++ch)
                    ir.clear (ch, w, juce::jmin (512, len - w));

    conv.loadImpulseResponse (std::move (ir), reader->sampleRate,
                              juce::dsp::Convolution::Stereo::yes,
                              juce::dsp::Convolution::Trim::yes,
                              juce::dsp::Convolution::Normalise::yes);
    irLoaded = true;
}

void ConvolutionJunkModule::process (juce::AudioBuffer<float>& buf, const ModuleContext&)
{
    if (! irLoaded.load()) return;
    juce::dsp::AudioBlock<float> block (buf);
    conv.process (juce::dsp::ProcessContextReplacing<float> (block));
}

} // namespace dg
