# RUIN

A cross-platform experimental DAW (JUCE 8 / C++20) built around one idea: **creative destruction**.
Damage, corruption and breakage are generative, intentional, and performable — never accidental,
never forced on the signal. The clean path stays pristine; the **TEETH** corruption rack is
insert-only and opt-in, so a bare acoustic vocal and a shredded amen live in the same session
without touching each other.

## Build

Requires CMake ≥ 3.22 and a C++20 compiler. JUCE is found in this order:

1. `-DJUCE_SOURCE_DIR=/path/to/JUCE`
2. `./JUCE` (git submodule / checkout next to `CMakeLists.txt`)
3. `/opt/JUCE`
4. FetchContent from GitHub, pinned to **8.0.13** (needs network on first configure)

### Linux

```sh
sudo apt install build-essential cmake libasound2-dev libjack-jackd2-dev \
    libfreetype-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
    libxinerama-dev libxrandr-dev libxrender-dev libfontconfig1-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/dawglitch_artefacts/Release/"RUIN"
```

### macOS

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -G Xcode   # or default generator
cmake --build build --config Release -j
open "build/dawglitch_artefacts/Release/RUIN.app"
```

AU hosting is enabled automatically on macOS (plus VST3 and LV2).

### Windows

```sh
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
build\dawglitch_artefacts\Release\RUIN.exe
```

ASIO: drop the Steinberg ASIO SDK in and enable `JUCE_ASIO=1` in `CMakeLists.txt`
(see the `// EXTEND` comment there). WASAPI works out of the box.

## Quick orientation

- **Space** play/stop - **R** record - **L** loop - **S** split selected clip at playhead -
  **Del** delete - **Shift+Del** ripple delete - **Ctrl+X/C/V** cut/copy/paste at playhead -
  **Ctrl+D** duplicate - **Ctrl+A** select all - **Ctrl+Z / Ctrl+Shift+Z** undo/redo -
  **Ctrl+S** save - **Ctrl+E** export - **Ctrl+wheel** zoom.
- Timeline tools (toolbar top-left, or keys **1/2/3**): **select** (arrow),
  **razor** (click a clip to split there), **erase** (click a clip to delete it).
- **Tab** (or V) flips **SESSION** ↔ **ARRANGE**. The session grid: tracks as columns,
  scenes as rows; click a slot to launch it quantized (combo top-right, default 1 bar);
  clips loop in sync until the track's stop square or STOP ALL; scene ▶ fires a row.
  Click an empty MIDI cell to create a loop and edit it in the piano roll; drop audio
  files onto audio cells. Right-click a slot: delete / rename / loop length.
- Drag audio files anywhere in the window to import (`.dgproj` opens, video files load
  the video track). Formats: WAV/AIFF/FLAC/OGG/MP3 natively; with **ffmpeg** installed,
  anything it can decode (opus, m4a, wma, exotic wavs, ...) bridges in via a one-time
  cached transcode. The **FILES** tab is a bin: browse a folder, double-click to preview,
  drag onto the timeline. The **FX** tab is a searchable explorer (TEETH, built-in
  instruments, plugins) — drag onto a track or double-click to hit the selected track.
- Right-click everything: ruler (tempo/timesig changes, markers), clips, track headers,
  automation lanes, rack knobs (macro assign / MIDI learn). Drag a header's bottom edge
  to resize the track or lane.
- **CHAIN** tab: the selected track's devices as boxes (power, edit, reorder, remove;
  TEETH boxes carry live macro knobs). **PATCH** tab: the modulation bay — drag cables
  from LFOs / CHAOS (Lorenz) / FOLLOW (envelope follower) onto any parameter
  (*+ TARGET* adds one); cable amount is bipolar, knobs wiggle around wherever you
  set them, and moving a knob re-centres its modulation.
- Track header buttons: **M**ute, **S**olo, **R**ecord-arm, **MON** (off → direct dry →
  through the insert chain), **FX** (insert chain: add **TEETH**, add plugins,
  set instrument — built-ins: GlitchTone, RUST, GRAVEL, HYMN), **A** (automation lanes).
- First plugin scan: *Options → Plugin manager → Options → Scan for new...*
- Project files are XML (`*.dgproj`); recorded audio lands in `<project>_Assets/`.

## Naming

The app is **RUIN**; the corruption rack is **TEETH**. Both are set in one place if you
ever want different names: `Source/Common.h` → `dg::names::appName` / `rackName`
(plus `PRODUCT_NAME` in `CMakeLists.txt`).

## What's deliberately minimal (marked `// EXTEND:` in source)

- Time-stretch: varispeed by default; right-click a clip → *Pitch-locked stretch (RubberBand)*
  renders the stretch in the background and swaps in seamlessly. Live RT stretch is the marked slot.
- Take comping: split a take, "promote" the slice, nudge edges to overlap — overlapping
  audible clips crossfade automatically (equal-power). Visual fade handles on overlaps are next.
- Plugin scanning runs in-process (async, with crash blacklist); out-of-process marked.
- Video decoding on Linux (sync logic + frame counter run everywhere; mac/win play video).
- Stems render one pass per track; single-pass multi-writer marked.
- MIDI FX / arpeggiator slot on instrument tracks.

## Testing

A headless suite covers the model, clip operations, comp crossfades, the TEETH
modules (including the bit-transparency law and feedback clamping), the built-in
instruments, and the stretch cache:

```sh
cmake --build build --target ruin_tests -j$(nproc)
./build/ruin_tests_artefacts/Release/ruin_tests
```

Clip editing lives in `Source/Model/ClipOps.*` (UI-free) precisely so the suite
drives the same code the timeline does.

See `ARCHITECTURE.md` for how the graph, session model and subsystems fit together.
