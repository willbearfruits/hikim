# HIKIM v2 — layout redesign (the horizontal turn)

Owner brief: *"I still don't like the layout, things should be horizontal, learn a
lot from Bitwig."* Plus the standing complaint that assigning an instrument / finding
things is *"not intuitive at all."*

This is a **layout + interaction** spec. No engine work, no model changes. It reuses
the `Dock` we already have and re-homes panels into a Bitwig-shaped anatomy. Every
phase is a shippable round (build + `ruin_tests` + relaunch + commit), and every phase
leaves all three views fully working — same rule as NODES.

---

## The diagnosis (why it reads wrong today)

HIKIM already has good bones: a movable/collapsible `Dock` with LEFT / RIGHT / BOTTOM
zones + focus mode (`Source/UI/Dock.h`), three center views swapped by Tab, and a
`ChainPanel` whose header literally says *"Bitwig-style device chain… one box per
insert."* The pieces are right. **Three things put them in the wrong place:**

1. **The device chain is vertical.** `ChainPanel` lays its device boxes out
   *horizontally* in a `Viewport` (`Source/UI/ChainPanel.cpp`) — but it's registered
   in the **RIGHT** zone (`MainComponent.cpp`: `registerPanel("CHAIN", …, zRight)`), a
   tall narrow strip. A horizontal chain crushed into a vertical column is the entire
   "should be horizontal" complaint, almost verbatim. Bitwig and Live both put the
   device chain in a **horizontal strip across the bottom** of the body.
   [[bitwig-anatomy]] [[bitwig-vs-live-devices]]

2. **Nothing follows selection.** In Bitwig the bottom panel *is* the detail of
   whatever you selected — click a track → its devices; double-click a clip → its
   editor. [[bitwig-inspector]] HIKIM makes you hunt: CHAIN is a right tab, PIANO ROLL
   is a bottom tab, and you must know which tab to open. That's the "not intuitive"
   feeling — the detail of the thing you're touching isn't where your eyes are.

3. **The side rails read as vertical chrome.** `Dock` paints LEFT/RIGHT tab chips with
   **rotated vertical text**. Vertical labels + a vertical device panel = a UI that
   feels columnar. Bitwig's persistent selectors are a **horizontal row** along the
   bottom and a thin footer. [[bitwig-anatomy]]

Bitwig's window is a clean **four-band vertical stack**: header → transport → a
"mercurial" body that changes with what you're doing → a footer that always shows the
available actions and the hovered parameter. [[bitwig-anatomy]] We can land that shape
on top of the `Dock` without rewriting it.

---

## Target anatomy (v2 default profile)

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ TRANSPORT   ▸▮ ●rec │ 1.1.000  120.0  1/16 │ LOOP CLICK │ ARRANGE SESSION PATCH │  42px (exists)
├───────┬──────────────────────────────────────────────────────────┬────────────┤
│ FILES │                                                            │            │
│  FX   │     CENTER  —  ARRANGE / SESSION / PATCH                   │  INSPECTOR │
│       │     (timeline · clip launcher · node canvas)               │  (follows  │
│ left  │                                                            │  selection)│
│ rail  │                                                            │  thin/opt  │
├───────┴──────────────────────────────────────────────────────────┴────────────┤
│ ▸DEVICES (track)   PIANO ROLL (clip)   SAMPLE   MIXER   PATCH        ← selector │  22px
│ ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌───┐                       │
│ │  WIRES  │▸▸│  TEETH  │▸▸│  RUST   │▸▸│ plugin  │▸▸│ + │   horizontal device   │  ~260px
│ │ ▢ ⚙ ◀▶✕ │  │ ◉◉◉◉ mac│  │  inst   │  │         │  └───┘   strip, h-scroll     │
│ └─────────┘  └─────────┘  └─────────┘  └─────────┘                              │
├────────────────────────────────────────────────────────────────────────────────┤
│ hint: double-click an empty lane to draw a clip · ⛏ PENCIL · param 0.42         │  20px footer
└────────────────────────────────────────────────────────────────────────────────┘
```

The star is the **bottom band**: a horizontal selector row + a tall horizontal panel
that defaults to the selected track's **device strip**, and flips to PIANO ROLL /
SAMPLE / MIXER as selection or the user dictates. The RIGHT zone shrinks to a thin
context **inspector** (optional, collapsible). The LEFT rail keeps FILES/FX (browser).
A new **footer** runs the full width with live hints.

---

## Panel specs

| Band | Pos | Default size | Holds | Behavior |
|---|---|---|---|---|
| Transport | top | 42px (exists) | play/rec, time, tempo, grid, LOOP/CLICK, view switch | unchanged |
| **Bottom band** | bottom | **260px** + 22px selector | **DEVICES**, PIANO ROLL, SAMPLE, MIXER, PATCH | the contextual detail surface — see below |
| Left rail | left | 240px, collapsible | FILES, FX (browser) | drag a device/file → drops onto the device strip or a lane |
| Inspector | right | 220px, **collapsed by default** | selected track or clip params | follows selection; opt-in |
| Center | fill | rest | ARRANGE / SESSION / PATCH | unchanged; Tab still cycles |
| **Footer** | very bottom | 20px | hint text · active tool · hovered-param value | always-on discoverability bar |

Sizing rationale: 260px matches HIKIM's current bottom-zone default (240) and is the
band where a device box (~180×230) and a usable piano-roll both fit. Bitwig's body is
"mercurial" — the bottom panel height is the single most-resized thing in the app —
so the top edge of the bottom band stays drag-resizable (already true in `Dock`).

---

## The headline change — horizontal device strip

**What:** move CHAIN from `zRight` to a wide `zBottom`, and make it the **default**
bottom panel. The component barely changes — it already builds horizontal device boxes
in a `Viewport`. It just needs a short wide home instead of a tall narrow one.

**Device box** (each insert; the box already exists, this is polish):
- Fixed size ~**180w × 230h**, laid left→right, horizontal scroll past the edge.
- Header: power toggle · name · `⚙` open-editor · `◀ ▶` reorder · `✕` remove (all exist).
- Body is device-specific: **TEETH** shows its 4 macro knobs inline (exists); **WIRES**
  shows a mini patch thumbnail (cheap: reuse the far-zoom NodeCanvas chip render);
  **instrument/plugin** shows 2–4 mapped params or a generic face.
- The cables `▸▸` between boxes are signal flow L→R (decoration; the order is the chain).
- A trailing **`+` box** is always present (exists) — the always-visible "add a device"
  affordance. For a MIDI track with no instrument it reads **"set instrument…"** and
  opens the instrument picker (the placeholder pattern already shipped in PATCHER view,
  commit `0946365` — reuse `showInstrumentMenu`).

**Why horizontal wins** (and isn't just taste): a device chain is a *sequence in time/
signal*; left-to-right matches the mental model and the cable metaphor, reads the same
direction as the arranger above it, and lets a box be tall enough for real inline
controls (macros, a curve, a thumbnail) instead of a one-line list row. Every
horizontal-bottom DAW (Live, Bitwig) converged here; the vertical-rack holdouts (FL's
channel rack, Reaper's FX list) trade inline control for density. HIKIM's identity
(TEETH macros you ride, WIRES patches) *needs* the inline control, so horizontal is the
right call for this app specifically. [[bitwig-vs-live-devices]]

**Cost:** ~½ day. The risky part (horizontal box layout) is already written.

---

## Selection-follows-detail (the intuition fix)

One rule kills most of the "not intuitive" feeling: **the bottom band shows the detail
of the current selection, automatically.** [[bitwig-inspector]]

- Select a **track** (header click, or a box in PATCHER) → bottom shows **DEVICES** for
  that track.
- Double-click a **MIDI clip** → bottom shows **PIANO ROLL** on it (already wired via
  `ui.openPianoRoll`; just also make single-select *surface* the right tab).
- Double-click an **audio clip** → bottom shows **SAMPLE**.
- The selector row still lets you pin a panel manually (override the auto-flip until
  selection type changes).

Plumbing: `UIState` already carries `selectedTrack` / `selectedClips` and has
`openPianoRoll` / `openSampleEditor` callbacks. Add `ui.onSelectionChanged` and a tiny
router in `MainComponent` that calls `dock->showPanel(...)` by selection type. `Dock`
already has `showPanel(name)`. This is wiring, not new surface.

---

## The inspector (consolidate scattered params)

Bitwig's right Inspector is context-sensitive: a track's full params when a track is
selected, a clip's loop/groove/time-sig when a clip is selected. [[bitwig-inspector]]
HIKIM scatters these across track headers and PATCHER boxes.

v2: a thin RIGHT panel, **collapsed by default** (don't force it on people), that when
opened shows:
- track selected → name/colour, gain/pan, in/out routing, send levels (read the same
  tree props the mixer strip binds).
- clip selected → start/length, loop on + loop length (the timeline round added these
  to the tree), clip gain, fades, stretch mode.

Low priority — it's a consolidation/nicety, not a fix. Ship it after the bottom band.

---

## The footer hint bar (cheapest discoverability win)

A 20px full-width strip at the very bottom, always on. Three slots:
`[ context hint ……………… ] [ active tool ] [ hovered param value ]`

- **Context hint**: changes with hover/mode — *"double-click an empty lane to draw a
  clip"*, *"drag a device here"*, *"⛏ pencil: drag to draw a note"*. This is the
  single biggest answer to "not intuitive at all" — Bitwig's footer always tells you
  what you can do right now. [[bitwig-anatomy]]
- **Active tool**: mirrors the tool palette so the current mode is never a mystery.
- **Param readout**: the value of whatever knob/slider is under the cursor.

Implementation: one `juce::Component` owned by `MainComponent`, a `ui.setHint(String)`
callback panels call on mouse-enter. ~60 lines. Do this **first** — it pays off during
every later phase and needs nothing else.

---

## Tools — unify and complete

Bitwig ships five and makes them visible + holdable: Pointer (1), Time-Select (2),
Pen (3), Eraser (4), Knife (5); click the palette, press the number, or **hold** a
number for momentary use, and each timeline keeps its own tool. [[bitwig-tools]]

HIKIM today: select / razor(=knife) / erase on 1/2/3, plus the new pencil on 4
(timeline) and the piano-roll tool strip the edit-tools round just shipped. Gaps:

1. **Add a Time-Select tool** — drag a time range across all tracks (for range
   loop/cut/insert-silence). New `Tool::timeRange`.
2. **One palette, every surface** — the piano roll got a visible strip; give the
   **timeline** the same visible strip (not just toolbar icons) and use identical
   glyphs/order so the muscle memory transfers. Bitwig's "each timeline keeps its own
   selected tool" is the right model — keep per-surface tool state but identical UI.
3. **Hold-to-temp** — hold `3` to pencil while held, release back to the prior tool.
   Bitwig's best ergonomic touch and a few lines in the key handler.
4. Reconcile numbering with Bitwig if we want parity (theirs: 1 pointer / 2 time /
   3 pen / 4 erase / 5 knife). Optional; document whatever we pick in the footer + F1.

---

## What we deliberately keep

- **`Dock`** — movable/collapsible zones, drag-resize, focus mode, persistence. Good
  code; v2 only changes *defaults* (which panel starts in which zone) and adds the
  bottom **selector row** + auto-flip. Don't rewrite it.
- **Three-view swap** (ARRANGE/SESSION/PATCH on Tab). Bitwig coexists arranger + clip
  launcher as toggleable sub-panels rather than a full swap [[bitwig-anatomy]]; that's
  a *later* idea (a SESSION strip overlaid on ARRANGE), not part of this pass. Lead
  with the bottom band; the owner's complaint is the device chain, not the view model.
- The engine, the tree, RT-safety — untouched. This is chrome.

---

## Phased plan (each phase = one shippable round)

1. **Footer hint bar.** New `StatusBar` component + `ui.setHint`. Wire a handful of
   hints. Free-standing, instant payoff. *(no model, no tests needed — pure UI)*
2. **Re-home the device chain.** CHAIN → `zBottom`, make it the default bottom panel,
   polish the device box (WIRES thumbnail, always-on `+`/"set instrument…"). This is
   *the* fix; mostly `MainComponent.cpp` defaults + `ChainPanel` layout.
3. **Selection-follows-detail.** `ui.onSelectionChanged` → `MainComponent` routes
   `dock->showPanel`. Track→DEVICES, MIDI clip→PIANO ROLL, audio clip→SAMPLE.
4. **Tools: Time-Select + visible timeline palette + hold-to-temp.** Logic (range
   ops) is UI-free in `ClipOps` with tests; the palette is UI.
5. **Inspector panel.** Thin right, collapsed by default, follows selection. Nicety.
6. **(later)** SESSION-as-a-strip over ARRANGE; horizontal selector chrome replacing
   the rotated side-rail labels.

Phases 1–3 are the redesign the owner actually asked for and are ~2–3 days together.
4–6 are polish that can trail.

---

## Sources

- [[bitwig-anatomy]] Bitwig Studio User Guide — *Anatomy of the Bitwig Studio Window*
  (4-band header/transport/body/footer; the "mercurial" body; the footer's actions +
  parameter readout). <https://www.bitwig.com/userguide/latest/anatomy_of_the_bitwig_studio_window/>
- [[bitwig-tools]] Bitwig Studio User Guide — *The Arrange View and Tracks* (the five
  tools, keys 1–5, hold-to-temp, per-timeline tool state, smart tool switching).
  <https://www.bitwig.com/userguide/latest/the_arrange_view_and_tracks/>
- [[bitwig-inspector]] Bitwig Studio User Guide — *Meet Inspector Panel* / *The
  Inspector Panel on Arranger Clips* (selection-driven track vs clip parameters).
  <https://www.bitwig.com/userguide/latest/meet_inspector_panel/>
- [[bitwig-vs-live-devices]] MusicRadar — *Ableton Live vs Bitwig Studio* (both put
  device chains in a horizontal lower panel; Bitwig also exposes them from the mixer).
  <https://www.musicradar.com/news/ableton-live-vs-bitwig-studio>
- [[bitwig-modulators]] Bitwig Studio User Guide — *Modulators, Device Nesting, and
  More* (per-device modulator pane, nested device chains — context for the device box
  design). <https://www.bitwig.com/userguide/latest/advanced_device_concepts/>
