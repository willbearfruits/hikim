#pragma once
#include "../Common.h"

// Single source of truth for the session ValueTree vocabulary.
namespace dg::id
{
    #define DG_ID(name) static const juce::Identifier name (#name);

    // node types
    DG_ID (SESSION)   DG_ID (TRANSPORT) DG_ID (TEMPOMAP) DG_ID (TEMPO)
    DG_ID (TIMESIG)   DG_ID (TRACKS)    DG_ID (TRACK)    DG_ID (CLIPS)
    DG_ID (CLIP)      DG_ID (NOTES)     DG_ID (NOTE)     DG_ID (INSERTS)
    DG_ID (INSERT)    DG_ID (AUTO)      DG_ID (LANE)     DG_ID (PT)
    DG_ID (MARKERS)   DG_ID (MARKER)    DG_ID (VIDEO)
    DG_ID (MODS)      DG_ID (MOD)      DG_ID (MODTARGET)   // modulation patch
    DG_ID (SCENES)    DG_ID (SCENE)    DG_ID (SLOTS)    DG_ID (SLOT)   // session view

    // common props
    DG_ID (version)   DG_ID (uid)       DG_ID (type)     DG_ID (name)
    DG_ID (colour)

    // track props
    DG_ID (gain)      DG_ID (pan)       DG_ID (mute)     DG_ID (solo)
    DG_ID (armed)     DG_ID (monitor)   DG_ID (inputChan) DG_ID (inputStereo)
    DG_ID (outputBus) DG_ID (sendABus)  DG_ID (sendALevel)
    DG_ID (sendBBus)  DG_ID (sendBLevel) DG_ID (height)

    // clip props (start/length/offset in engine samples; midi notes in beats)
    DG_ID (start)     DG_ID (length)    DG_ID (offset)   DG_ID (file)
    DG_ID (lane)      DG_ID (fadeIn)    DG_ID (fadeOut)  DG_ID (clipGain)
    DG_ID (stretch)   DG_ID (stretchMode) DG_ID (fileSR)   // stretchMode: 0 varispeed, 1 pitch-locked
    DG_ID (loop)      DG_ID (loopLen)   // content loop: audio pass length in seconds (midi uses loopBeats)

    // tempo map (ramp: bpm glides linearly-in-beats to the next event)
    DG_ID (beat)      DG_ID (bpm)       DG_ID (num)      DG_ID (den)
    DG_ID (ramp)

    // notes
    DG_ID (pitch)     DG_ID (vel)       DG_ID (len)

    // inserts ("rack" | "plugin" | "instrument")
    DG_ID (fmt)       DG_ID (ident)     DG_ID (state)    DG_ID (bypass)

    // automation / modulation
    DG_ID (param)     DG_ID (mode)      DG_ID (visible)  DG_ID (t) DG_ID (v)
    DG_ID (src)       DG_ID (target)    DG_ID (amount)   DG_ID (base)
    DG_ID (x)         DG_ID (y)         DG_ID (rate)     DG_ID (shape)
    DG_ID (track)     DG_ID (loopBeats) DG_ID (scene)

    // transport / video
    DG_ID (loopStart) DG_ID (loopEnd)   DG_ID (loopOn)
    DG_ID (punchIn)   DG_ID (punchOut)  DG_ID (punchOn)
    DG_ID (metro)     DG_ID (fps)

    #undef DG_ID
}
