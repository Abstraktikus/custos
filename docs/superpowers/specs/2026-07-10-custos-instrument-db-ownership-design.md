# Custos owns the Instrument DB — design

**Status:** approved design, pre-plan
**Date:** 2026-07-10
**Repos touched:** `custos` (bulk of the work) + `Global Rackspace.gpscript` (coordinated shrink)
**Supersedes / builds on:** `2026-07-06-custos-instrument-browse-design.md`,
`2026-07-07-custos-preset-store-design.md`, `2026-07-04-custos-external-control-contract-design.md`

---

## 1. Motivation

Today the synth metadata lives in `VstDatabase.txt`, a `key = value` text file that
Gig Performer's GPScript parses at boot. GP reads six things from it (path, controlType,
paramDown, paramUp, trim, favOrder) and *runs the logic* itself: it resolves name→path for
song load, applies per-instrument trim, and drives the inner synth's preset browsing
(param-inject / Program Change / GP host preset list).

Two problems:

1. **The `.txt` is a bad database.** It caps synth *count* at GPScript's silent 256-array
   limit, its parser code sits against GP's compile-size ceiling (+140 lines crashes the
   compiler), and every extra column KM would want to write risks touching that parser.
2. **The logic lives in the wrong layer.** Driving an inner synth's preset mechanism belongs
   in the thing that *hosts* the inner synth. That is Custos — a host-inside-a-plugin — not
   the DAW.

**Goal:** GP-Script stops reading `VstDatabase.txt` entirely and becomes a dumb macro-trigger
surface. Custos owns the instrument database and executes all instrument/patch/preset logic.

## 2. The autonomy principle, restated

The hard rule: **GP plays live without KM; GP is only *configured* with KM.** This is easy to
misread as "the intelligence must live in GP." It does not. The rule demands *resident without
KM*, not *resident in GP*.

Custos is on GP's side of the KM-absence line: it is *always* loaded (it **is** the host slot),
so a config it reads at boot is exactly as KM-independent as a file GP reads at boot. This
partitions the three-way tangle into two clean tiers:

| Tier | Responsibility | When |
|---|---|---|
| **KM** | writes/maintains the full instrument DB inside Custos; binds macros to the joystick (layers, full-deflect) | config-time (needs KM) |
| **Custos** | owns the DB; executes Instrument / Patch / Preset verbs | show-time (resident) |
| **GP-Script** | exposes **macros** that each fire *one* Custos verb as a dumb OSC trigger | show-time (resident) |

Moving logic GP→Custos is **free** with respect to the autonomy rule, because both sit on the
show-time side. The joystick never appears in this design as a "function" — only as a surface KM
binds macros onto.

## 3. Terminology (locked)

- **Instrument** — the synth (VST) itself. *Instrument Prev/Next* switches the whole VST.
- **Patch** — the synth's own factory sound. *Patch Prev/Next* steps it natively (PARAM or PC).
- **Preset** — a Custos snapshot (`.cuspreset`). *Preset Prev/Next / Load* walks Custos's store.

The old internal label **`HOST` is removed** — to the operator these are just *Presets*, and we
do not keep two names for one thing.

## 4. Data model (Custos)

The store grows from favourites-only to the **full instrument list**. Per entry:

```
name · path · favOrder · gainDb · brand · slots · controlType · paramDown · paramUp
```

- `favOrder`: `0` = known-but-not-a-favourite, `≥1` = favourite rank (sort key).
- `gainDb`: per-instrument **base** trim — applies to **every** synth on load, not only
  favourites (today `applyVolumeDefault` gives non-favourites unity 0 dB; with the full list
  every known synth carries its trim). **Authored and written solely by KM** (calibration is
  configuration — see §6a). GP never writes it.
- `controlType` ∈ `PARAM | PC | PRESET | NONE`; `paramDown` / `paramUp` are the inject indices
  for `PARAM`. New fields — KM supplies them.
- `slots`: inner param count; browsing/loading skips synths whose `slots > facadeCap`
  (unchanged from the browse design).

**On-disk:** today's `%APPDATA%/Custos/favorites.json` becomes the full list. Rename to
`instruments.json` with a one-time migration that reads a legacy `favorites.json` if present
(favourites become entries with their existing `favOrder`; everything else is additive).

**Population:** KM extends the existing favourites push to carry the full list plus the three new
fields. Either widen `/custos/favorite`'s arg list or introduce a parallel `/custos/db/begin …
/entry … /end` — decided in the plan; semantics identical (accumulate, then commit + write
config + `refreshEditor`).

## 5. Custos OSC verbs (the primitives)

| Verb | Args | Effect |
|---|---|---|
| `/custos/instrument/next` | `scope:int` (`0`=fav, `1`=all) | cursor +1 over the full list, filtered by scope; **stateless** (scope per-call, no mode state). Reports NAME via `/custos/browsing`, deferred load. |
| `/custos/instrument/prev` | `scope:int` | cursor −1, same. |
| `/custos/instrument/set` | `i:int` | jump cursor to index `i` (clamped). |
| `/custos/instrument/load` | `name:string` | **new** — resolve name→path *inside Custos* and load. Replaces GP's `GetPathForVst`. |
| `/custos/patch/next` | — | step the factory sound; Custos dispatches by the loaded synth's `controlType` (§6). |
| `/custos/patch/prev` | — | as above, −1. |
| `/custos/preset/next` | — | cycle Custos snapshots (`.cuspreset`) for the loaded synth's classId. |
| `/custos/preset/prev` | — | as above, −1. |
| `/custos/preset/load` | `name:string` | existing named recall. |

- One cursor over the full list; **scope is only the skip predicate** (skip `favOrder<1` when
  `scope=0`). Reuses the existing `favouriteFits` skip loop in `browseInstrument`.
- **Patch and Preset are independently triggerable** — different macros, never one collapsed
  control. `/custos/load <path>` remains for KM's direct path loads.
- Existing `/custos/instrument/next|prev|set` gain an **optional** `scope` int (default `0`) so
  the change is backward-compatible with the v2 contract.
- Trim uses the **existing** `/custos/volume <gainDb>` (no new verb): KM/GP deliver an override
  that outranks the list base (§6a).

**Feedback (Custos → KM/GP, mirrored to GP `:54344` where noted):**

- `/custos/browsing` — instrument-browse preview (existing).
- `/custos/loaded` — inner changed / ready (existing); carries applied path.
- `/custos/preset/loaded` — preset recall confirm (existing).
- **new** `/custos/patch/stepped N, label:string` — patch-step feedback. Best-effort: real name
  where the inner synth exposes it, otherwise a generic marker (e.g. PC/bank number). Mirrored
  to GP `:54344` so the HUD can show it without KM.

## 6. controlType semantics (Patch axis)

Explicitly **declared, never sniffed** — Custos cannot tell whether a synth responds to Program
Change, so PC is never a blind fallback.

- **`PARAM`** → Custos injects `paramUp` / `paramDown` on the inner synth (momentary toggle,
  same shape GP used: zero → 1.0 → hold ~150 ms → 0.0). Custos already mirrors the inner params.
- **`PC`** → Custos sends Program Change ±1 into the inner synth (it passes MIDI through).
- **`PRESET` / `NONE`** → the Patch axis **falls back to the Preset store** — i.e. `patch/next`
  does exactly what `preset/next` does. This is the only *implicit* fallback, and it always
  works because Custos owns the snapshots.

Fallback priority applies **only to the Patch axis**: `PARAM` if declared, else `PC` if
declared, else `PRESET`. The Preset axis always cycles snapshots regardless.

Name feedback: authoritative for Preset snapshots; best-effort for native `PARAM`/`PC` (the
synth owns its patch pointer — Custos often cannot read the name). Accepted trade-off.

## 6a. Trim — authoring is configuration (KM), not runtime (GP)

Trim is **configuration**, so by definition it belongs to KM. Deliberately *not* in this design:
the whole trim-**authoring** loop (measuring a clip-free ceiling, computing a trim, deduping
duplicate synths by min). Today GP owns it via the `BTN_AutoGainCalibration` watchdog workflow;
that moves to KM entirely — how KM authors it is KM's concern.

The runtime split:

- **Base trim** lives in Custos's instrument list (`gainDb`), written only by KM, applied on
  load (`applyVolumeDefault`).
- **Per-song override** (a value that deviates from the base for one song) is the **only** trim
  role GP keeps: GP reads it from `Song.ini` and pushes it via the existing `/custos/volume
  <gainDb>` verb after load.
- **Precedence:** a delivered `/custos/volume` override **always outranks** the list `gainDb`.
  No override for a song → the list base stands.

Note (plan-time boundary): the live **overload watchdog** that attenuates in real time is a
*playing-time safety* feature, not trim authoring — the removal in §7 targets the
*write/calibration* path, not runtime attenuation. Confirm the exact cut in the plan.

## 7. GP-Script side

### Added — macro catalogue (single-step only)

Each macro is a thin wrapper that fires one Custos verb as an OSC trigger and does nothing else:

- `Instrument Prev` / `Instrument Next` — with a scope variant (fav vs all).
- `Patch Prev` / `Patch Next`.
- `Preset Prev` / `Preset Next`.
- `Preset Load <name>`.

These are ordinary macros in GP's existing macro system. **KM binds them to joystick
axes/layers/full-deflect** — that binding is out of scope here (see §9). GP-Script's only job is
to *provide* the macros.

### Removed — `VstDatabase.txt` read + native engine

- Whole `.txt` pipeline: `LoadVstDatabase`, all `DB_*` arrays, `GetPathForVst`,
  `RefreshVstTrims`, `VstDatabaseFilePath`.
- **Trim authoring/write path** (configuration → KM): the `BTN_AutoGainCalibration` workflow,
  its `DB_Trim` writes + min-dedup, `PushVstTrimsToKM`, `VstTrimsDirty`/`CalibrationActive`
  state. Trim is no longer authored in GP (see §6a; watchdog-attenuation boundary noted there).
- Native stepping engine: `TriggerVstPresetChange`, `CyclePluginPreset`, `FullDeflectVoice`.
- `_LIBR` / `_INST` remnants: `LibCyclePrefix`, `LoadRigConfig`, `BuildPresetSublist`,
  `MovePreset`, and the `RigConfig.txt` path. **The library/instrument-cycle concept is dropped
  and will be re-thought later** — not reproduced in this pass.

### Kept in GP

- Reading `Song.ini` → per slot `songVstName` → `/custos/instrument/load <name>`. GP stays dumb:
  it forwards the string it read; Custos resolves name→path and applies the base `gainDb`.
- Reading a **per-song trim override** from `Song.ini` (only when it deviates from the base) →
  `/custos/volume <gainDb>` after load. This is GP's *only* remaining trim role (§6a). No
  override present → GP sends nothing and the list base stands.

### Bonus

Large net line reduction in `Global Rackspace.gpscript` — relieves the compile-size ceiling.

## 8. Autonomy walkthrough (cold boot, no KM)

1. GP boots; each Custos instance reads `instruments.json` (resident, KM-independent).
2. Song load: GP reads `Song.ini`, fires `/custos/instrument/load <name>` per slot → Custos
   resolves name→path, loads, applies `gainDb`.
3. Joystick macro `Patch Next` → `/custos/patch/next` → Custos steps by the loaded synth's
   `controlType`.
4. HUD updates from `/custos/loaded` / `/custos/patch/stepped` / `/custos/preset/loaded`
   (mirrored to GP `:54344`).

No `.txt`, no KM. An empty store behaves exactly like today's never-written `.txt` (no
regression): first configuration always needs KM, which the rule already accepts.

## 9. Explicitly out of scope (later)

- **Coarse / bank jump** and the required *second* PARAM pair (library-nav index). Single-step
  only for the first cut; additive later without breaking anything.
- **Library concept** (`_LIBR` re-thought) — removed now, redesigned separately.
- **KM push protocol** exact wire shape — sketched in §4, nailed in the plan.
- **Joystick binding** (layers, full-deflect modes) — KM configuration, not this design.

## 10. Responsibilities

- **Custos:** the full instrument store + migration; the verbs in §5; `controlType` dispatch in
  §6; base `gainDb` on load; **override precedence** (`/custos/volume` outranks list `gainDb`);
  the new `/custos/patch/stepped` feedback.
- **KM:** populate the full DB (incl. `controlType`/`paramDown`/`paramUp` **and base `gainDb`**);
  **author trim** (calibration is configuration); bind macros to the joystick.
- **GP-Script:** provide the macro catalogue; delete the `.txt` pipeline, native engine, trim
  authoring, and `_LIBR`/`_INST`; forward `Song.ini` names via `/custos/instrument/load` and
  per-song trim overrides via `/custos/volume`.

## 11. Open items for the plan

- Store push shape: widen `/custos/favorite` vs new `/custos/db/*`.
- `instruments.json` migration from legacy `favorites.json`.
- `/custos/patch/stepped` best-effort name sourcing per synth class (what, if anything, Custos
  can read for PARAM/PC).
- GP-Script removal ordering so the script always compiles between steps (compile-ceiling repo).
