# HIKIM

HIKIM is a cross-platform experimental DAW built with JUCE 8, C++20 and CMake.
It is for clean arrangement, ugly signal damage, live clip launching, and patchable
audio systems in the same desktop app.

The identity pieces are:

- **TEETH**: an opt-in corruption rack. With every module off it must pass audio
  bit-identically.
- **WIRES**: a Max-style patcher that can run as an effect device or as an instrument.
- **Three working views**: ARRANGE, SESSION and PATCHER/Routing over one shared project tree.
- **Pristine by default**: the clean path stays clean until the user inserts damage.

Project page: <https://willbearfruits.github.io/hikim/>

![HIKIM arrange view](docs/assets/screenshots/hikim-arrange.png)

## Downloads

Prebuilt artifacts are tracked in this repository:

| Platform | File | Notes |
| --- | --- | --- |
| Windows | [`dist/HIKIM-setup.exe`](dist/HIKIM-setup.exe) | Installer build. |
| Windows | [`dist/HIKIM-windows-portable.zip`](dist/HIKIM-windows-portable.zip) | Portable `HIKIM.exe` plus README. |
| Linux x86_64 | [`dist/HIKIM-linux-x86_64.tar.gz`](dist/HIKIM-linux-x86_64.tar.gz) | Portable binary. |

These are experimental builds. Keep a copy of important sessions and audio before
using any early DAW build for serious work.

## Screenshots

| Arrange | Session | Patcher/Routing |
| --- | --- | --- |
| ![Arrange timeline](docs/assets/screenshots/hikim-arrange.png) | ![Session launcher](docs/assets/screenshots/hikim-session.png) | ![Routing patcher](docs/assets/screenshots/hikim-routing.png) |

## What Works

- Timeline editing with audio/MIDI tracks, split, ripple delete, nudge, duplicate,
  undo/redo, loop selection and drag/drop import.
- Session grid with quantized clip and scene launching, follow/tracker-style scene
  chaining, slot waveforms and BPM detect/conform.
- TEETH corruption rack with module ordering, macros, MIDI learn and rack state.
- WIRES patcher with oscillators, filters, delay feedback, host `param` bridge and
  OSC in/out.
- Plugin hosting for VST3 everywhere, AU on macOS and LV2 where supported by JUCE.
- Built-in instruments: RUST, GRAVEL, HYMN and the WIRES instrument path.
- Mixer, channel chain, modulation patch bay, piano roll, sample editor, file bin
  and FX browser.
- Offline export, stems, stretch cache, ffmpeg-backed media decode fallback and
  XML project files (`*.dgproj`).

## Quick Start

### Linux artifact

```sh
tar -xzf dist/HIKIM-linux-x86_64.tar.gz
./HIKIM
```

### Windows artifact

Run `dist/HIKIM-setup.exe`, or unzip `dist/HIKIM-windows-portable.zip` and launch
`HIKIM.exe`.

### From source

Requirements:

- CMake 3.22 or newer
- C++20 compiler
- JUCE 8.0.13, supplied by `-DJUCE_SOURCE_DIR`, `./JUCE`, `/opt/JUCE`, or fetched by CMake

Linux dependencies:

```sh
sudo apt install build-essential cmake libasound2-dev libjack-jackd2-dev \
    libfreetype-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
    libxinerama-dev libxrandr-dev libxrender-dev libfontconfig1-dev
```

Build:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/dawglitch_artefacts/Release/HIKIM
```

Windows:

```bat
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
build\dawglitch_artefacts\Release\HIKIM.exe
```

macOS:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -G Xcode
cmake --build build --config Release -j
open "build/dawglitch_artefacts/Release/HIKIM.app"
```

## Controls

- **Space/K**: play or stop.
- **J/L**: jump one bar, Shift for four bars.
- **Return**: return to start.
- **R**: record.
- **Shift+L**: loop on/off.
- **Ctrl+L**: loop around selection.
- **S**: split at playhead.
- **Delete**: delete selected clips.
- **Shift+Delete**: ripple delete.
- **Ctrl+X/C/V/D/A**: cut, copy, paste, duplicate, select all.
- **Ctrl+T / Ctrl+Shift+T**: new audio or MIDI track.
- **Ctrl+Z / Ctrl+Shift+Z**: undo or redo.
- **Ctrl+N/O/S/E**: new, open, save, export.
- **Tab / V / view button**: cycle ARRANGE, SESSION and PATCHER views.
- **1/2/3**: select, razor and erase tools.
- **F1**: help.

Most editing surfaces have context menus. Right-click clips, track headers, the
ruler, automation lanes, rack knobs and session slots.

## Project Model

`SessionModel` owns one JUCE `ValueTree`, saved as XML `.dgproj`. All UI and engine
state changes go through that tree so undo, views and the audio graph stay in sync.

Time conventions:

- timeline positions and lengths: double seconds
- MIDI notes: beats relative to clip start
- audio clip offset: source-file samples
- realtime engine snapshots: engine samples at the current sample rate

Read [`ARCHITECTURE.md`](ARCHITECTURE.md) for the full graph and subsystem notes.

## Testing

The headless suite covers model operations, clip editing, comp crossfades, TEETH,
WIRES bit-transparency, built-in instruments and the stretch cache.

```sh
cmake --build build --target ruin_tests -j$(nproc)
./build/ruin_tests_artefacts/Release/ruin_tests
```

Crashes write a backtrace to `/tmp/ruin-crash.log`.

## Extension Points

Deliberate growth points are marked with `// EXTEND:` in the source. The next
owner-approved roadmap work is Phase C: recording into session slots and capturing a
session jam back into the arrangement.

Names live in `Source/Common.h`: `dg::names::appName`, `rackName`, `patcherName`,
plus `PRODUCT_NAME` in `CMakeLists.txt`.
