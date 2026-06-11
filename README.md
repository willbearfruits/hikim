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
  **Del** delete clips - **Ctrl+D** duplicate - **Ctrl+Z / Ctrl+Shift+Z** undo/redo -
  **Ctrl+S** save - **Ctrl+E** export - **Ctrl+wheel** zoom.
- Drag audio files onto the timeline to import. Right-click everything: ruler
  (punch points, tempo/timesig changes, markers), clips, track headers, automation lanes,
  rack knobs (macro assign / MIDI learn).
- Track header buttons: **M**ute, **S**olo, **R**ecord-arm, **MON** (off → direct dry →
  through the insert chain), **FX** (insert chain: add **TEETH**, add plugins,
  set instrument), **A** (automation lanes).
- First plugin scan: *Options → Plugin manager → Options → Scan for new...*
- Project files are XML (`*.dgproj`); recorded audio lands in `<project>_Assets/`.

## Naming

The app is **RUIN**; the corruption rack is **TEETH**. Both are set in one place if you
ever want different names: `Source/Common.h` → `dg::names::appName` / `rackName`
(plus `PRODUCT_NAME` in `CMakeLists.txt`).

## What's deliberately minimal (marked `// EXTEND:` in source)

- Time-stretch is varispeed (rate change affects pitch) — RubberBand/SoundTouch slot is marked.
- Take comping: takes stack in lanes, "promote take" swaps lanes; no crossfade comping yet.
- Plugin scanning runs in-process (async, with crash blacklist); out-of-process marked.
- Video decoding on Linux (sync logic + frame counter run everywhere; mac/win play video).
- Stems render one pass per track; single-pass multi-writer marked.
- MIDI FX / arpeggiator slot on instrument tracks.

See `ARCHITECTURE.md` for how the graph, session model and subsystems fit together.
