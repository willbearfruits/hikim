# RUIN — roadmap

Direction stays fixed: creative destruction, performable damage, clean path pristine.
Everything below serves one of the two poles — the breakcore/glitch side or the bare
sentimental side — or the machinery both stand on.

## Done since this file was written

- Crossfade comping (overlapping audible clips auto-crossfade, equal-power)
- Automation at <=256-sample resolution
- FILES bin (browse/preview/drag-in) + FX explorer (search/drag/double-click)
- Built-in instruments: RUST (FM), GRAVEL (noise perc), HYMN (pad)
- Window-wide drag-and-drop (audio / video / .dgproj)
- Resizable track + lane heights
- Punch removed from the UI (owner's call; engine support dormant)
- ffmpeg media bridge (decode anything) + full edit toolset + tool icons (1/2/3)
- Headless test suite (`ruin_tests`)
- CHAIN tab: Bitwig-style device boxes with inline TEETH macros
- PATCH tab: modulation node canvas - LFO x4 / Lorenz chaos / envelope follower
  cabled to any parameter in the session (the mod matrix from "the big four")

## Next (small, soon)

- **Clip loop + slip** — loop clip content to fill its length; drag content inside the
  clip without moving its edges. The missing half of break-editing.
- **Crossfade handles** — comping crossfades exist (overlaps auto-crossfade equal-power);
  draw them on the clips and make the overlap draggable.
- **Count-in / pre-roll** — two bars of click before punch-in. Tracking comfort.
- **Tempo ramps** — linear BPM glides between tempo events (accelerando into the wall).
- **Tap tempo** + playhead nudge keys.
- **Track freeze / bounce-in-place** — commit a corrupted track to audio with one key.
  Doubles as a *gesture*: freezing TEETH output makes destruction permanent.
- **MIDI clip looping** and a drum-lane mode in the piano roll.
- **A/B mixer snapshots** — flip the whole console between two states.

## The big four (defining features)

1. **Audio-signal node patching** — extend the PATCH canvas from modulation to audio:
   boxes = tracks/devices/buses, cables = signal flow, feedback routes allowed (the
   engine's AudioProcessorGraph already supports arbitrary routing; this is UI work).
   Max/MSP energy inside the DAW.
2. **Slicer/sampler instrument** — drop a break, onset-detect slices, play them from
   MIDI; "extract groove" from the slices to a swing template. The breakcore core
   workflow: chop the amen, resequence it in the piano roll, then feed it TEETH.
3. **Clip launcher (session grid)** — quantized scene/loop launching for live sets,
   recording the jam into the arrangement. RUIN as a performance instrument.
4. **Live RT RubberBand** — pitch-locked stretch playable in realtime (the render
   cache stays for quality; live mode makes stretch a knob you can ride).
   Plus mod-matrix depth: more chaos sources (logistic map, drunk walk), per-cable
   lag/curve, MIDI-note and velocity as sources.

## Identity moves (only RUIN would do these)

- **Wear** — opt-in: every bounce pass adds a breath of tape age; a project setting
  where the session accumulates patina each save. The file remembers being touched.
- **Chaos automation** — "generate" menu on automation lanes: attractor curves,
  drunk walks, decaying ratchets. Automation drawn by the same math as the canvas pieces.
- **ASCII scope** — a character-only oscilloscope/spectrogram window (every visible
  cell a typed character). The one place RUIN meets characterglitch on screen.
- **OSC bridge** — OSC in/out mapped to params/macros/transport; sclang as an external
  corruption brain, or RUIN driving the browser pieces in sync.
- **Hold-to-destroy** — momentary key/MIDI mappings: TEETH modules engage only while
  held. Destruction as a played articulation, not a state.
- **Stems → grainfield** — one-click stem export as `.ogg` sets shaped for the
  granular pieces' sample folders.

## Housekeeping (when it hurts)

- Autosave + crash recovery (tree snapshot every few minutes).
- Collect-and-save: consolidate all referenced media into the project folder.
- Out-of-process plugin scanning (parked by request).
- FLAC/MP3 export; loudness/peak meter on the master.
- Per-sample automation ramps (currently <=256-sample resolution).
- Linux video decode (libmpv) for the video track.
