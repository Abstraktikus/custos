# Custos brief — expose the inner synth's stable classId (OSC v4)

**From:** Kapellmeister (KM) side. **To:** a Custos implementation session.
**Date:** 2026-07-22. **Run this through your normal brainstorm → spec → plan → TDD flow.**

## Why (KM's need — context, not instructions to relay)

KM wants to resolve a **preset** to its owning **synthesizer** by a *stable* key so it can,
when a slot has no synth loaded, show a synth-spanning preset list and — on click — load
that preset's synth and the preset into the slot. Today the only cross-store link between
"a preset" and "a loadable instrument" is the human **synth name** (soft, rename-prone).
Custos already owns the stable key — the VST3 `createIdentifierString()` (your `classId`,
i.e. `innerSynthKey()`), used to name preset folders and stamped inside each `.cuspreset`.
KM needs that classId **exposed on the wire and carried in `instruments.json`** so it can
build a hard `preset(classId) → instrument(classId) → path → load` link.

The design mirrors how **`slots`** already flows: Custos exposes `innerTotal` on
`/custos/loaded`, KM learns it and re-pushes it in the favourite push. Do the same for
classId — **no new write on the load path**, `instruments.json` stays push-only.

## Scope — OSC contract bump to v4

Four additive, backward-compatible changes. Every new argument is **trailing/optional**, so
a v3 KM keeps working during rollout, and a v4 Custos talking to a v3 KM degrades cleanly.

### 1. Version constant + contract doc
- `src/OscContract.h:11` — `constexpr int kProtoVersion = 3;` → `4`. This flows into
  `buildHere(...)` automatically.
- `docs/osc-contract.md` — bump the `protoVer = 3` line (≈ line 5) to 4 and update the
  changed message rows (`/custos/loaded`, `/custos/favorite`, `/custos/preset/*`).

### 2. `/custos/loaded` gains `classId` (Custos → KM)
- `src/OscContract.h:30-33` — `buildLoaded(n, path, boundCount, innerTotal)` →
  add a trailing `const juce::String& classId` and `.addString(classId)` (append after
  `innerTotal`). New shape: `N, path, boundCount, innerTotal, classId`.
- Emit site: the single `buildLoaded(...)` call inside `emitLoaded()`
  (`CustosProcessor.cpp:204`). Editing it once covers all three `emitLoaded()` callers
  (lines 121, 148, 197) — no need to touch the call sites. At the emit point `inner` is
  live (or null when cleared), so `innerSynthKey()` is valid: pass `innerSynthKey()`
  (empty string when nothing is loaded — that is correct and parses fine as an empty
  trailing arg).

### 3. `/custos/favorite` accepts + persists `classId` (KM → Custos, round-trip)
- Parse: `CustosOscServer.cpp` `/custos/favorite` block (currently parses optional args up
  to `paramUp` at `msg[9]`, lines ≈ 64-88). Add an **optional 11th arg**:
  `if (msg.size() >= 11 && msg[10].isString()) c.fav.classId = msg[10].getString();`
- Model: add `juce::String classId;` to `struct Favorite` (`src/FavoritesStore.h:7-10`).
- Persist: the JSON fields are emitted in `favoritesToJson` (the `setProperty` block,
  `src/FavoritesStore.cpp:16-19`) and read in `favoritesFromJson` (the `getProperty`
  block, `src/FavoritesStore.cpp:33-41`) — **not** in `writeFavorites`, which only writes
  the finished string. Add `o->setProperty("classId", f.classId);` to the serializer and
  `f.classId = v.getProperty("classId", "").toString();` to the parser. A missing field
  reads as empty (back-compat with existing `instruments.json` files).
- This is stored data only — Custos does not need to *use* the pushed classId; it is a
  durable record so `instruments.json` carries the same stable key KM holds.

### 4. `/custos/preset/*` gain `classId` + `synthName` (Custos → KM, live tagging)
- `CustosProcessor.cpp:841-849` `emitPreset(verb, name, idx)` — append
  `.addString(innerSynthKey()); .addString(innerSynthName());` after the existing
  `N, name, idx`. One funnel covers `browsing`, `loaded`, `saved`, `renamed`, `deleted`
  (call sites ≈ lines 661, 679, 699, 711, 723 — unchanged, they route through `emitPreset`).
  New shape: `N, name, idx, classId, synthName`. These verbs only fire with an inner
  present, so both fields are always real; note that with no inner `innerSynthKey()` is
  `""` but `innerSynthName()` returns `"(none)"`, not empty — don't assert an empty
  synthName in the no-inner case.
- **Why classId here too** (it is already on `/custos/loaded`): the preset verbs mirror to
  GP `:54344`, and GP drives the preset axis autonomously without tracking load state — the
  classId+synthName tag lets GP bind a preset event to its synth on its own. This is for
  GP autonomy, not for KM (which already learns classId from `/custos/loaded`); keep it.

## Non-goals / guardrails
- **Do not** add any write to the load path for `instruments.json` — classId reaches the DB
  only via KM's favourite push (change 3). Change 2 only *emits* the classId; it does not
  persist anything.
- **Do not** change folder naming, the `.cuspreset` format, or `PresetStore` keying — the
  classId already lives there; you are only surfacing it on the wire + in `instruments.json`.
- Keep all additions trailing so back-compat holds; do not reorder existing args.

## Tests (Catch2)
- `buildLoaded` emits the classId as the 5th arg; empty when nothing loaded.
- `/custos/favorite` with 11 args stores `classId`; with ≤10 args leaves it empty (back-compat).
- `writeFavorites`/read round-trips `classId`; a legacy JSON without the field loads as empty.
- `emitPreset` appends classId + synthName for each verb.
- A v3-shaped `/custos/favorite` (≤10 args) still parses (no regression).

## Coordination
KM will land the matching consumer in lockstep: parse classId from `/custos/loaded`, store
it on its master `InstrumentEntry`, and re-push it as the new `/custos/favorite` arg. KM's
design doc: `kapellmeister/docs/superpowers/specs/2026-07-22-preset-knows-synth-design.md`.
Ship Custos v4 first; the trailing-optional args mean neither side breaks the other mid-rollout.
