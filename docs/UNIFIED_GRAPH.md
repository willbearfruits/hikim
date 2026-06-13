# HIKIM — the unified graph (PATCHER + WIRES become one)

The plan to merge the patcher mode and the WIRES device system into a single node
graph, with the old modulation bay rebuilt as objects. This realizes `NODES.md`
("one graph, three altitudes") at the model level, not just the shared canvas E1 gave
us. Supersedes the bespoke `RoutingView` and deletes `PatchView`.

## Owner decisions (2026-06-13)

1. **Merge depth: full graph, big-bang.** Target the unified session-scope graph
   directly — not a shared-palette-only half-measure. Build-order milestones below are
   internal; the end state is the whole thing.
2. **Old mod bay: retire, rebuild as objects.** `LFO 1-4 / Chaos / Follow` (the 6
   hard-coded engine sources) become palette objects; the side `PATCH` panel is
   deleted; existing sessions migrate.
3. **Audio everywhere.** Audio-rate objects (`osc~`, `sample~`, filters) make sound
   **at the session altitude**, not only inside devices. One true session audio graph.
4. **Mixer: floating window.** `MixerView` leaves the bottom band and becomes a
   detachable window (second-monitor friendly).

## The engineering strategy (how "audio everywhere" stays safe)

The naive reading of decision 3 — make tracks/buses/master into nodes and compile the
*whole session* to one `Program` — would mean rewriting `AudioEngine`'s spine: the
`AudioProcessorGraph`, `ClipPlayer`, automation block-splitting, PDC, sends. That spine
is the most battle-tested, crash-sensitive code in the app (CLAUDE.md's hard rules
exist because of it). Rejected.

**Chosen strategy: a session-scope patcher node that lives *alongside* the track graph
and bridges into it through primitives we already built.** The session gets one
top-level node graph (a `SessionGraphProcessor`, reusing `PatcherProcessor`'s compiler
+ RT-safety pattern) owned by the engine and wired into the master bus. It already has
every bridge it needs from E1/E2:

```
            ┌─────────────── SESSION GRAPH (new, top-level) ───────────────┐
            │  osc~ sample~ grain~ lores~ …  → master~  →─┐  (audio everywhere) │
            │  chan~ 2  ← taps any channel               │                     │
            │  strip 3.gain ← driven by lfo/chaos/…      │  number cables =    │
            │  lfo chaos drunk follow → param<target>    │  modulation          │
            └────────────────────────────────────────────┼─────────────────────┘
                                                          ▼ inject ring
  ClipPlayer→inst→inserts(incl. WIRES device)→strip→bus→[ master strip ]→out
                         ▲ dive into a device = its own sub-patch (same canvas)
```

- `master~` already injects audio into the master strip (E2 `InjectRing`). So
  `osc~ → master~` in the session graph **makes sound** — decision 3, already wired.
- `chan~` already taps any channel pre/post (E1); `strip` already drives gain/pan/mute
  by stamp-freshness (E1); `clock` already exposes transport. The session graph reads
  and writes the mixer through these — no engine-spine changes.
- Feedback stays legal exactly where it already is: ring taps (graph cycles are illegal
  in `AudioProcessorGraph`, which is *why* `chan~`/`master~` are rings).
- RT-safety is inherited verbatim: `PatcherProcessor` already does the message-thread
  snapshot → `shared_ptr` swap under `SpinLock` try-lock → graveyard-on-timer dance.

The result is genuinely "one graph" from the user's seat — the session canvas
synthesizes, routes, and modulates — while the proven track engine keeps running
underneath. Diving into a channel or device shows *its* sub-patch on the same canvas.

## Modulation = number cables (kills the mod bay)

Today: 6 hard-coded engine sources + a `mods()` ValueTree (`MOD`/`MODTARGET`) +
`ModRTState`, authored by `PatchView`'s "+TARGET" flow and `RoutingView`'s dashed
cables. Two UIs, one model, baked-in sources.

After:
- **Mod sources are objects**: `lfo` (≈ existing `lfo~`), `chaos` (Lorenz), `drunk`,
  `follow` (envelope), plus roadmap chaos (logistic map). They live in the session
  graph and emit **number** signals (NODES.md ■).
- **Every parameter is a patch point**: alt-drag any knob → it extrudes a `param`
  target node bound to that parameter (reusing `AudioEngine::resolveParamTarget`,
  which already maps `strip:gain` / `send:A` / `ins:<uid>:<n>`). Cabling a number into
  it *is* a modulation. No "+TARGET" menu.
- The engine keeps `ModRTState`/`resolveParamTarget` as the **application** layer
  (smoothed param writes, plugin-safe) — now fed by the compiled session graph instead
  of the mod-bay UI. The 6 fixed sources stay alive under the hood during migration and
  are *presented* as the equivalent objects, so old `.dgproj` files keep modulating.
- `PatchView` is deleted; `RoutingView` is replaced by the session-graph editor.

This is Bitwig's unified-modulation model: any signal → any parameter, modulators are
just modules with outputs. ([The Unified Modulation System](https://www.bitwig.com/userguide/latest/the_unified_modulation_system/),
[The Grid](https://www.bitwig.com/the-grid/) — "doesn't distinguish audio or CV".)

## Layout after the merge

- **PATCH side panel: deleted** (the thing the owner dislikes).
- **PATCHER view = the session graph** on `NodeCanvas` with the full object palette,
  dive/breadcrumb. The Cubase/Live/Bitwig hybrid resolves cleanly: ARRANGE = timeline,
  SESSION + horizontal device band = clips/devices, **PATCHER = the one node graph**.
- **MIXER → floating window** (decision 4); the bottom band keeps DEVICES / PIANO ROLL
  / SAMPLE.
- WIRES-the-device editor becomes a *dive* into a device node (same canvas), retiring
  the separate floating editor once dive lands.

## Build order (milestones — each builds, tests green, app runs)

- **M1 — session graph node in the engine.** `SessionGraphProcessor` owned by
  `AudioEngine`, persisted under a `GRAPH` child of the session tree, injected into
  master. Empty = bit-transparent. *Tests: empty graph silent; `osc~→master~` makes
  sound at master; `chan~`/`strip` reach the right strip.*
- **M2 — PATCHER view = session-graph editor.** Replace `RoutingView`'s bespoke
  channel drawing with `NodeCanvas` hosting the session graph (the same `NodeComp` +
  objects as WIRES). Channels surface as `chan~`/`strip` nodes (or a `channel` node);
  routing cables still edit the session tree. *The big UI merge.*
- **M3 — mod sources as objects + extrude-any-param + retire mod bay.** Add
  `lfo/chaos/drunk/follow` objects + the `param` target object + alt-drag extrude;
  migrate `mods()` → session-graph number-cables; delete `PatchView`. *Tests:
  `lfo→param` modulates; old-session migration preserves mods.*
- **M4 — mixer floats.** `MixerView` → `FloatingWindow` (Options / button); drop it
  from the bottom band.
- **M5 — dive + breadcrumb.** Double-click a channel/device node → its sub-patch on the
  same canvas; breadcrumb climbs out. WIRES device editor becomes a dive.
- **M6 — I/O audit + polish.** Typed ports correct across audio/number/event/notes for
  every object; LOD; palette grouping; the param-extrude affordance on every knob.

M1/M2/M5 are the heavy ones. Estimated several rounds each; the arc is multi-session.

## Risks / invariants to hold

- **Don't destabilize the engine spine.** The session graph is additive (one node +
  rings); the `AudioProcessorGraph`, `ClipPlayer`, automation splitting stay untouched.
- **RT-safety**: the session graph follows the existing snapshot/SpinLock/graveyard
  pattern (inherited from `PatcherProcessor`) — the audio thread never frees.
- **Bit-transparency**: an empty session graph and a default device patch must pass
  audio bit-identically (existing law; new tests guard it).
- **Migration**: every existing `.dgproj` must open and sound the same — the 6 fixed
  mod sources and `mods()` connections map to equivalent nodes (or stay under the hood,
  presented as nodes).
- **Latency/PDC** of injected session-graph audio into master — measure; keep it sample-
  aligned like the existing inject path.

## What gets deleted / superseded

- `Source/UI/PatchView.{h,cpp}` — deleted (function absorbed by the PATCHER altitude).
- `Source/UI/RoutingView.{h,cpp}` — replaced by the session-graph editor.
- The 6 hard-coded engine mod sources — kept internally for migration, no longer the
  authoring surface.
- `NODES.md` phases E1-E4 are folded into this plan (E1 shipped; E2 in progress; the
  rest land as M1-M6).
