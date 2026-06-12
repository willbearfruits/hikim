# CONTINUE — session handoff (2026-06-12)

Delete this file once the round below is finished and committed.

## Where we are

Working on **NODES E1, piece 2** (after the number box + canvas nav landed in
`90d6009`). This commit contains a compiling, test-passing snapshot of:

- `Spec` (PatcherProcessor.h) gained `Family fam` + per-port types
  (`inTypes`/`outTypes`, chars: `s` signal / `n` number / `e` event) and a
  `specFor (name)` lookup. Spec rows reordered into family groups.
- `col::nodeSource/nodeEffect/nodeMath/nodeTime/nodeRouting` in Look.h/.cpp
  (NODES.md family colours: amber/red/grey-blue/green/teal, light + dark).
- WIRES palette (PatcherEditor.cpp `ObjPalette`) grouped into NODES.md
  sections (SOURCES / EFFECTS / NUMBERS & MATH / TIME & CHANCE / ROUTING),
  object names painted in family colour; headers are non-draggable rows.
- NodeComp LOD rides canvas zoom via `PatcherEditor::canvasZoom()`:
  zoom < 0.6 = chip (name only, family-tinted fill); >= 1.4 = full face
  (adds the spec desc line); between = the familiar mid face.
  NB: `near`/`far` are windef.h macros — locals are named `chip`/`full`.
- Typed port shapes (signal dome / number square / event triangle) and typed
  cables (signal thick accent, number thin grey-blue, event thin green).

## To finish this round

1. **Unit tests** (Tests.cpp, PatcherTests): every spec's
   `strlen(inTypes) == ins` and `strlen(outTypes) == outs`; `specFor` finds
   every name and returns null for junk. Not written yet.
2. **Launch the app and eyeball it**: palette sections, port shapes at the
   three LOD levels, cable colours (metro -> random should read green/event
   into the box). `build\dawglitch_artefacts\Release\HIKIM.exe`.
3. Amend/commit the round + update site/README if worth mentioning.

## Then: the rest of E1 (NODES.md)

- **channels-as-nodes** (`chan~`, `strip`, `master~`) — the big remaining E1
  bullet; engine work, plan first.
- Unifying the WIRES palette with PATCH/Routing canvases is the wider E1
  "unify palette" goal — this round only did the WIRES side.

## This machine's build incantation (important)

VS 2022 **Community has no C++ toolchain**; MSVC lives in **BuildTools 2022**
and the 14.36 toolset there is broken (missing `msvcrt.lib`) — pin **14.44**.
The Visual Studio CMake generator can't find any instance; use **Ninja**:

```bat
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 12
build\ruin_tests_artefacts\Release\ruin_tests.exe
```

(JUCE 8.0.13 is fetched by CMake; first configure takes a while.)
