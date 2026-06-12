# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

HIKIM: a cross-platform experimental DAW (JUCE 8, C++20, CMake) built around *creative
destruction* — the TEETH corruption rack and the WIRES patcher are the identity features,
and the clean signal path must stay pristine unless the user opts in. This is a JUCE
desktop app; the parent repo's character-only-art rules (canvas pieces) do **not** apply here.

Names live in one place: `Source/Common.h` (`dg::names::appName` = HIKIM, `rackName` = TEETH,
`patcherName` = WIRES) plus `PRODUCT_NAME` in `CMakeLists.txt`.

## Commands

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release      # JUCE: -DJUCE_SOURCE_DIR, ./JUCE, /opt/JUCE, else FetchContent 8.0.13
cmake --build build -j$(nproc)                 # builds app + tests
./build/dawglitch_artefacts/Release/HIKIM       # run the app
./build/ruin_tests_artefacts/Release/ruin_tests   # headless suite (~30s); exits non-zero on failure
```

Windows: the README's Visual Studio generator path may fail ("could not find any
instance of Visual Studio" even when VS is installed, and BuildTools' 14.36 toolset
links broken). Use Ninja from a pinned vcvars environment instead:

```bat
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 12
build\ruin_tests_artefacts\Release\ruin_tests.exe
```

There is no per-test filter; the suite runs everything (juce::UnitTest classes in
`Tests/Tests.cpp`). RubberBand is fetched at configure time (single-file vendored build)
unless a system `rubberband` pkg-config package exists. Crashes write a backtrace to
`hikim-crash.log` in the system temp dir (handler installed in `Main.cpp`).

## Architecture: one tree, everything reacts

`SessionModel` (Source/Model/) owns the session `ValueTree` (saved as XML `*.dgproj`)
and the global `UndoManager`. **All edits go through the tree**; the engine and every
view are `ValueTree::Listener`s. Never wire UI→engine directly for model changes.

Time conventions (mixing these up breaks everything silently):
- timeline positions/lengths in the tree: **double seconds**
- MIDI notes: **beats relative to clip start**
- audio clip `offset`: **source-file samples**
- RT structs are in engine samples at the *current* sample rate, rebuilt on SR change.

`AudioEngine` (Source/Engine/) is the spine — simultaneously the device callback
(it drives `AudioProcessorGraph::processBlock` itself, splitting blocks at loop
boundaries, session-launch times, and ≤256-sample chunks when automation is active),
the `AudioPlayHead` for all hosted plugins, and the tree's audio-side listener
(changes set `rebuild::` flags handled once per message-loop tick in `handleAsyncUpdate`).

Graph topology per track (PDC via the graph + `ioLatency` for recording placement):

```
audio:  [audio in] -> ClipPlayer -> inserts... -> ChannelStrip -+-> output-bus head
midi:   [midi in] -> MidiSource -> Instrument -> inserts... ----+   +-> SendA/B -> bus head
bus:    inserts... -> ChannelStrip -> master head
master: inserts... -> ChannelStrip -> [audio out]
```

Insert nodes are persistent across rebuilds, keyed by INSERT uid in `insertNodes`;
types: `rack` (TEETH), `patcher` (WIRES), `plugin` (hosted), `instrument`
(hosted or `builtin:rust|gravel|hymn|rubble|wires`).

## The RT-safety pattern (used everywhere — copy it, don't invent new ones)

Message thread builds an immutable snapshot (`AudioPlaylist`, `MidiPlaylist`, `TempoMap`,
automation lanes, mod state, WIRES `Program`), swaps a `shared_ptr` under a `SpinLock`
try-lock, and keeps the old one in a graveyard purged on a timer so **the audio thread
never frees memory** (BufferingAudioReaders especially). Session-view launches are
`SessionAction`s applied sample-accurately at quantize boundaries on the audio thread.

## Hard rules (violations have shipped crashes before)

- **Bit-transparency law**: TEETH with all modules off and WIRES' default patch must pass
  audio bit-identically. The test suite enforces both — keep those tests passing.
- **Editor windows must die before their processors.** The engine fires
  `onInsertWillBeRemoved(uid)` before removing any insert node; MainComponent erases the
  window. Any new node-removal path must call it.
- **Never call a view's `rebuild()` synchronously from a child component's own event
  handler** — rebuild deletes the child mid-event. Set `rebuildPending` (timer-handled)
  or defer via `MessageManager::callAsync`. Menu lambdas must not capture `this` of
  components that rebuilds delete (capture the parent view pointer + ValueTrees instead).
- `juce::OSCAddress`/`OSCAddressPattern` **throw** on malformed strings; WIRES args are
  user-typed — wrap in try/catch.
- Colours: `col::` (Source/UI/Look.h) is a *runtime* palette (light/dark themes). Read it
  at paint time. If a component calls `setColour` explicitly, it must refresh on theme
  change (`lookAndFeelChanged` or per-tick).

## Feature subsystems (where to extend)

- **TEETH** (Source/Rack/): 9 DSP modules behind `RackModule`; params in one APVTS;
  order/macros/CC-maps/gate-steps in an `EXTRA` child of the APVTS state. New module =
  RackModules.h/.cpp class + params in `createLayout` + entry in `kModuleIds/Names`.
- **WIRES** (Source/Patcher/): patch ValueTree compiled (`compile()`) into a topo-sorted
  `Program` of `PObj`s; feedback legal only through `delay~` (deferred write pass).
  New object = `Obj` enum + `specs()` row (name/ports/defaults/desc + NODES.md `Family`
  and per-port type chars `s`/`n`/`e`) + a `processBlock` case (~20 lines). The palette
  groups by family; ports and cables draw by port type. `param N` bridges 8 host
  parameters.
- **Clip editing** lives UI-free in `Source/Model/ClipOps.*` so tests drive exactly what
  the timeline does. Extend there, not in TimelineView.
- **Media**: `AudioEngine::mediaFileFor/createAnyReader` — JUCE decoders first, else a
  cached ffmpeg transcode. All file reads (import, playback, thumbnails, preview,
  StretchCache, BPM detect) must route through it.
- **Views**: three modes (TimelineView / SessionGrid / RoutingView) swapped by
  `MainComponent::toggleView`; bottom `TabbedComponent` hosts Mixer/Chain/Patch(mod
  bay)/PianoRoll/Sample/Files/Fx panels. Cross-view state + callbacks live in `UIState`.

Deliberate growth points are marked `// EXTEND:` at the exact line — grep for them before
designing something new. `ROADMAP.md` holds the owner-approved phase plan (Phases A–C of
the Live/Max build-out are done); the active workstream is the NODES unified-patching arc,
specced in `NODES.md` (phases E1–E4). If a `CONTINUE.md` exists at the repo root it is a
mid-round session handoff — read it first, finish it, delete it.

## Conventions

- Everything in `namespace dg`; tree vocabulary only via `Source/Model/Ids.h` (`DG_ID`).
- 4-space indent, JUCE-style spaces before `(`, members lowerCamel, no comment spam —
  comments only for invariants the code can't show.
- After every feature round: build, run `ruin_tests`, relaunch the app, then commit
  (the owner expects working-state commits with descriptive bodies).
