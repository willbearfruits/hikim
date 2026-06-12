# NODES — the unified patching system (standing spec)

One graph, three altitudes. WIRES-the-device and PATCHER-the-mode become the same
canvas with the same palette; what changes is zoom. This document is the spec for
phases E1–E4; it gets refined as pieces land.

## The law

ARRANGE, SESSION and PATCHER are connected views over ONE session tree, and they
stay that way. The node system is another lens over the same tree — never a fork,
never a migration. Every phase ships with all existing modes fully working;
nothing is removed until its replacement does everything the old thing did.

## Decisions (defaults until the owner overrules)

- Object suffixes: `~` = signal rate, `#` = glyph-matrix rate, bare = numbers/events.
- Ports: inlets top, outlets bottom (Max-style); navigation is TouchDesigner-style
  (ctrl-wheel zoom, double-click dives, breadcrumb climbs out).
- Channel/strip patching edits are session edits: same undo stack as the timeline.
- `poly` voice cap 16, CPU-guarded per instance.
- The glyph matrix is typed characters, never pixels: each cell = (glyph, hue, intensity).

## Cable types

| type | carries | visual |
|---|---|---|
| signal `~` | audio | thick; alpha shimmers with live RMS |
| number | floats (the modulation currency) | thin solid; dot travels on change |
| event | bangs/triggers | thin; pulses on fire |
| notes | MIDI | dashed violet |
| glyphs `#` | character matrices | drawn as tiny travelling characters |

Port shapes: ● signal ■ number ▲ event ◆ notes ▣ glyphs.

## The number box

The modulation primitive. Drag = coarse, Shift = fine, double-click = type,
right-click = range/units/curve. Inlet sets, outlet emits on change. Every
parameter on every object can be extruded into a patchable number box
(alt-drag it out). Modulated boxes show base ghosted + live filled, accent
colored. MIDI-learnable, automation-recordable, OSC-addressable.

## Object set

**SOURCES** (amber): `adc~ osc~ phasor~ noise~ lfo~ sig` (exist), `sample~`
(FILES/slot buffer: trigger/rate/pos/loop, waveform face), `grain~` (granular
over same buffer), `slot~` (live session-slot tap), `chan~` (any channel as a
node, tap pre/post; face = mini strip with meter).

**INSTRUMENTS** (track colors): `rust~ gravel~ hymn~ rubble~` (builtins as
nodes), `plug~` (hosted VST3 as node), `teeth~` (rack as one box, 4 macro
ports), `poly` (wrap a subpatch into N allocated voices).

**EFFECTS** (red): `lores~ hipass~ delay~ tanh~` (exist), `comb~ verb~ crush~
fold~ xfade~ pan~`. Faces: one defining control + tiny response curve.

**NUMBERS & MATH** (grey-blue): `number`, `+ - * /`, `scale` (exists), `clip
wrap slew > < counter accum line quant` (quant = pitch-quantize to scale).

**TIME & CHANCE** (green): `metro random` (exist), `seq` (16-step, glyph-grid
face), `euclid`, `clock` (transport: bpm/beat/bar/phase), `chance`, `sel`,
`holdev`.

**NOTES** (violet): `notein noteout ccin ccout arp chord kbd`.

**ROUTING & MULTIPLEX** (teal): `mux/mux~` (N→1, selector is a modulatable
number), `demux/demux~`, `matrix~` (M×N crosspoint patchbay, face is the grid,
savable routing presets), `gate~`, `send/recv` (wireless cables by name).
Endgame: multichannel cables (`split`/`join`).

**SESSION**: `chan~`, `strip` (gain/pan/mute as number ports), `master~`,
`scene` (launch + scene-change events), `clock`.

**GLYPHS** (mono, `#`): generators `noise# ramp# text# life# flow# rd#`;
operators `blur# edge# invert# thresh# scroll# mix# feedback#`; audio→glyph
`scope# sono# vu#`; glyph→audio `scan#`; glyph→number `cell#`; render target
`screen#` (dockable panel or window). `#` faces show live matrix thumbnails.

## Zoom & LOD

Ctrl-wheel/pinch zooms the canvas. Box LODs: far = chip (name + meter), mid =
ports, near = full editable face. Double-click dives into subpatches/channels;
breadcrumb (`MASTER ▸ TAPE ▸ wobbler`) climbs out. Far zoom doubles as the
overview map — meters and cable shimmer stay live.

## Polish bucket

Hover a cable = floating mini-scope/number probe. Typed-inlet tooltips.
Per-node bypass/mute + tiny CPU readout. Encapsulate-selection → subpatch.
Comment boxes. Align/distribute. `preset` object snapshots all number boxes
and interpolates between snapshots. Patches saved into FILES as draggable
devices.

## Phasing

- **E1** unify palette + channels-as-nodes + zoom/LOD + the number box
- **E2** `sample~` / instruments-as-nodes / `poly` + `mux`/`matrix~`
- **E3** the `#` family + `screen#`
- **E4** multichannel cables + preset morphing
