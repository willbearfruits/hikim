# ARCHITECTURE

One sentence: a `juce::ValueTree` session model drives a `juce::AudioProcessorGraph`
rebuilt by `AudioEngine`, which owns the device callback and the transport; everything
visible is a view over the same tree.

## The session model (`Source/Model/`)

`SessionModel` owns the root `ValueTree` (saved as XML, `*.dgproj`) and the global
`UndoManager`. **All edits go through the tree** — UI components write properties/children,
and both the engine and the other views listen and react. That single rule is what keeps
undo global and the engine in sync without explicit wiring.

Vocabulary lives in `Model/Ids.h`. Time conventions:

- Timeline positions/lengths: **double seconds** (sample-rate independent, so a session
  renders identically at any export rate).
- MIDI notes: **beats relative to clip start** (they follow the tempo map).
- Audio clip `offset`: **source-file samples**.

Tree shape:

```
SESSION
 +- TRANSPORT  loop/punch/metronome
 +- TEMPOMAP   TEMPO{beat,bpm}* TIMESIG{beat,num,den}*
 +- TRACKS     TRACK{uid,type=audio|midi|bus|video|master, mixer props}
 |              +- CLIPS  CLIP{start,length,offset,fades,gain,stretch,lane, NOTES{NOTE*}}
 |              +- INSERTS INSERT{uid,type=rack|plugin|instrument,ident,state(base64),bypass}
 |              +- AUTO   LANE{param,mode, PT{t,v}*}
 +- MARKERS    MARKER{t,name}
 +- VIDEO      fps (the clip on the video track is the offset authority)
```

## The engine (`Source/Engine/`)

`AudioEngine` is the spine. It is at once:

- the `AudioIODeviceCallback` — it drives `graph.processBlock()` itself so each device
  block can be **split into segments** at loop and punch boundaries (sample-accurate wrap),
- the `AudioPlayHead` — every node (incl. hosted plugins) sees bpm/ppq/bar/loop state,
- the session tree's audio-side `ValueTree::Listener` — structural changes mark rebuild
  flags handled once per message-loop tick.

Graph topology per track (PDC: `AudioProcessorGraph` compensates parallel paths internally;
`getTotalLatencySamples()` adds device I/O latency for recording placement):

```
audio: [audio in] -> ClipPlayer -> inserts... -> ChannelStrip -+-> output bus head
midi:  [midi in] -> MidiSource -> Instrument -> inserts... ----+   +-> SendA/B -> bus head
bus:   inserts... -> ChannelStrip -> master head
master: inserts... -> ChannelStrip -> [audio out]
```

**RT-safety pattern** used throughout: the message thread builds an immutable snapshot
(`AudioPlaylist`, `MidiPlaylist`, `TempoMap`, automation lanes), swaps a `shared_ptr`
under a `SpinLock` try-lock, and keeps the old snapshot in a graveyard purged on a timer
so the audio thread never frees memory (readers especially).

- **ClipPlayer** (per audio track): plays snapshot clips via `BufferingAudioReader`
  (disk thread, zero-timeout), linear-interp varispeed for file-SR conversion + stretch,
  fades/gain per sample. Also captures input: monitor-through-chain feeds input into the
  insert chain; recording writes through a `ThreadedWriter` owned by a `RecordSession`.
- **StretchCache**: pitch-locked stretch as a render cache — a low-priority thread runs
  RubberBand offline over the source file; the playlist plays varispeed until the render
  lands, then re-snapshots onto the stretched file (offsets scaled by the ratio).
- **Recording**: one WAV per record pass; loop passes append to the same file with
  pass marks, sliced into **take lanes** on stop (newest take = lane 0 = audible; older
  overlapping clips pushed up). Start times are compensated by device+graph latency.
  MIDI records into the merged event list and lands as new/overdubbed clips.
- **Automation**: lanes resolve to `AudioProcessorParameter*` at rebuild; read/touch
  applied per segment (block-rate); write/touch captured via parameter listeners into a
  queue the UI drains into the tree (`MainComponent::timerCallback`).
- **Renderer**: removes the device callback, re-prepares the graph at export SR
  (non-realtime), runs the same segment processing offline. Stems = per-track passes
  with `forceMute` on the other strips.

## The corruption rack (`Source/Rack/`)

`RackProcessor` is a normal `AudioProcessor` (so it sits in the graph like any plugin)
with an APVTS holding ~50 parameters: per module `_on` + `_mix` + its DSP params, plus
4 macro params. The 9 modules (`RackModules.h/.cpp`) are plain DSP classes with
`prepare/process(buffer, ModuleContext)` — `ModuleContext` carries sr/bpm/ppq/bar so
tempo-synced modules lock to the transport and free-run when stopped.

Per block: MIDI CC map (+ learn) → macro fan-out (macro value mapped through per-target
lo/hi ranges, applied via `setValueNotifyingHost` so moves are visible and recordable as
automation) → modules in user order, each wet/dry-blended with a smoothed mix (doubling
as click-free bypass) → a soft safety stage **only when at least one module is active**.
With everything off the rack is bit-transparent — the design law ("corruption is
insert-only and opt-in") is enforced here and by the graph topology, which never routes
destruction into the clean path.

Order, macro maps, CC maps, gate steps and the IR path live in an `EXTRA` child of the
APVTS state tree, so full state rides along `get/setStateInformation` like any plugin —
that's also what user `.rack` files and the session's insert `state` property contain.
Factory gestures are functions over parameters (moves, not fixers).

## Plugin hosting (`Source/Plugins/`)

`PluginHost` wraps `AudioPluginFormatManager` (VST3 everywhere, AU on macOS, LV2) and a
persisted `KnownPluginList`. The manager UI is `juce::PluginListComponent` (async scan,
dead-man's-pedal blacklist). Instantiation is synchronous on the message thread inside
the graph rebuild; failures become `MissingPluginProcessor`, which passes audio and
preserves the saved state blob. `AudioEngine::syncToTree()` serializes every insert's
state to base64 before save; load restores by identifier and re-applies state.

## UI (`Source/UI/`, `MainComponent`)

`MainComponent` owns the model, host, engine, and views; it is also the `MenuBarModel`
and the window-level `KeyListener` (transport keys). `UIState` carries cross-view bits
(snap mode, selection, open-piano-roll / open-insert-editor callbacks).

- `TimelineView`: ruler (seek, loop drag, punch/tempo/timesig/markers via right-click),
  scrolled canvas (grid from the tempo map, playhead follow), `ClipComp` (move/trim/fade/
  alt-duplicate/cross-track move/split/take-promote), `AutoLaneComp` (point editing),
  headers (M/S/R/MON/FX/A + input pick). FX menu is the single place inserts are managed;
  the mixer reuses it.
- `PianoRoll`: bottom tab; draw/move/resize/velocity/marquee, quantize with strength +
  swing, humanize, scale lock, MIDI step input. Alt-drag = off-grid (no snap).
- `MixerView`: strips with fader/pan bound directly to `ChannelStrip` parameters,
  sends + bus routing, meters polled from strip atomics.
- `Dialogs`: audio settings (`AudioDeviceSelectorComponent`), plugin manager, export
  (offline bounce / stems / realtime master capture), `VideoView` (transport-locked,
  decoder stubbed on Linux behind `DG_HAVE_VIDEO`).

## Where to extend

Grep for `// EXTEND:` — every deliberate growth point is marked at the exact line:
live RT time-stretch, crossfade comping, out-of-process scanning, Linux video decode,
single-pass stems, MIDI FX slot, sample-accurate automation ramps, deeper IR abuse.
