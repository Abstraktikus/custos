# Custos Preset Store — Design

**Status:** design (brainstormed 2026-07-07).
**Relates to:** OSC contract v2 (`docs/superpowers/specs/2026-07-05-custos-osc-contract-v2-design.md`),
state persistence (`src/StateCodec.*`), favourites push-once pattern (`src/FavoritesStore.*`).

## 1. Context & problem

Historically each VST's sound was captured as a **GP User Preset** (`.gpp.vst` under
`~/Documents/GPPluginPresets/<plugin>/…`). Custos, being a host-in-a-plugin, does not expose a
usable per-inner-synth snapshot: a GP User Preset of a Custos instance would capture the **whole
Custos chunk** (facade + inner together), which is **build-dependent** (facade ladder 1000…10000,
protoVer) and couples the sound library to GP. That is unacceptable for a portable per-synth sound
library.

**Key enabler:** Custos already holds the exact bytes a preset needs. `StateCodec`'s
`innerState` is the raw state chunk of the hosted inner plugin (from its `getStateInformation`).
A preset is therefore just that block, written under a name at a central, per-synth location, and
loadable again via the existing OSC load path (M1/M3, already E2E-verified).

**Goal (chosen approach a):** *Custos generates presets per inner synth, across all Custos IDs, at
one central location, sorted per instrument.* Rejected approach b (Custos offers GP presets) —
build-dependent and GP-coupled.

## 2. Framing decisions (locked in brainstorming)

- **Store key = the inner synth's VST3 class-id.** All presets for one synth live in one folder,
  regardless of which Custos slot (N1…N10) hosts it. Robust against renames. "Sorted per
  instrument" = per synth type. The human-readable synth name is **not** the folder key.
- **Central root lives under the KM Locations area** — a `CustosPresets/` subfolder of the
  `rackspaceDir` (Snapshots), alongside song `.ini` + VstDatabase. Custos does **not** read KM's
  Locations; **KM pushes the root path once over OSC** and Custos persists it (the existing
  favourites "shared, push-once" pattern). Absent a push, Custos falls back to
  `~/Documents/CustosPresets/` and reports it.
- **Preset file format = Custos-native `.cuspreset`**, not GP presets and not standard
  `.vstpreset`. Symmetric write/read (Custos writes via `getStateInformation`, loads via
  `setStateInformation`) → **guaranteed loadable, 100% build- and host-independent.** Only Custos
  reads it — which is exactly the requirement. (A best-effort standard `.vstpreset` export was
  considered and deferred; JUCE may not expose hosted state as a raw VST3 chunk, so interop is not
  guaranteed and out of scope.)
- **Ordering is implicit alphabetical** (case-insensitive). No index/order sidecar file.
- **Three roles over one file-store foundation:**
  - **Custos window** — quick "save current inner state as preset" (name field) + direct-load list.
    Self-sufficient, works without KM/GP.
  - **KM preset-manager** — full management per class-id (browse/rename/delete), text input.
    KM-side UI is separate, later work; this spec defines only the OSC wire it drives.
  - **GP-Script Voice-Selector** — live recall (next/prev/set) within the currently loaded synth's
    presets. GP writes no files; it only sends OSC verbs. Delivered as a GP-session handoff prompt.

**Non-goals:** the KM preset-manager UI itself; standard `.vstpreset` interop; cross-synth
auto-swap on load; any change to GP file-writing (GP stays read+execute-only).

## 3. Data model & store layout

```
<PresetRoot>/                       ← pushed by KM (default ~/Documents/CustosPresets/)
  <hex(class-id)>/                  ← one folder per inner synth, key = VST3 class-id, hex-encoded
    Warm Pad.cuspreset
    Aggressive Lead.cuspreset
    ...
```

- **Folder name** = hex-encoded VST3 class-id (class-ids are not guaranteed filesystem-safe).
- **No index file.** Order = alphabetical, case-insensitive, over the `.cuspreset` files. `next/prev`
  wrap around at the ends.
- **Human-readable synth name** is stored **inside** each `.cuspreset` (metadata), so it is always
  recoverable for display without a separate index.

### `.cuspreset` format (extends the `StateCodec` pattern)

```
Magic "CUSP" + version byte
  + int32 classIdLen + UTF-8 class-id
  + int32 synthNameLen + UTF-8 synth display name
  + int32 presetNameLen + UTF-8 preset name
  + int32 innerLen + inner state bytes
```

Parse returns false (leaving output untouched) on wrong magic/version or truncation — same
contract as `parseState`. `PresetCodec` is a new small unit mirroring `StateCodec`.

## 4. OSC surface

Follows contract v2: commands to Custos at `127.0.0.1:(9100+N)`; feedback to hub `:8000` with **N
as the first argument**. Preview/debounce mirrors the existing favourite-browsing
(`/custos/browsing`) — while stepping, load only once the selection settles (~400 ms).

**Commands → Custos:**

| Verb | Args | Purpose | Driver |
|---|---|---|---|
| `/custos/preset/setroot` | `path` | set + persist the central root | KM (push-once) |
| `/custos/preset/save` | `name` | write current inner state as a preset in the loaded synth's class-id folder | Custos window / KM |
| `/custos/preset/list` | — | list presets for the currently loaded synth | KM, GP-Script |
| `/custos/preset/load` | `name` \| `idx` | load a preset (`setStateInformation`) | all |
| `/custos/preset/next` | — | live recall: next preset (wrap) | GP-Script |
| `/custos/preset/prev` | — | live recall: previous preset (wrap) | GP-Script |
| `/custos/preset/set` | `idx` | live recall: absolute index | GP-Script |
| `/custos/preset/rename` | `old` `new` | rename | KM |
| `/custos/preset/delete` | `name` | delete | KM |

**Feedback → hub `:8000` (N first):**

| Reply | Args | When |
|---|---|---|
| `/custos/preset/root` | `N` `path` | after setroot / on query |
| `/custos/preset/saved` | `N` `name` `idx` | after a successful save |
| `/custos/preset/loaded` | `N` `name` `idx` | after a successful load / recall |
| `/custos/preset/list` | `N` `count` `name0` `name1` … | reply to list |
| `/custos/preset/browsing` | `N` `name` `idx` | mid-step preview during next/prev |
| `/custos/preset/error` | `N` `reason` | any rejected op |

## 5. Edge cases & rules

- **Save with no synth loaded / unknown class-id** → reject, `/custos/preset/error`. Never write an
  empty preset.
- **Name collision on save** → exact name overwrites (predictable). The Custos window confirms
  "overwrite?" first; KM can rename.
- **Load when a *different* synth is loaded** than the preset's class-id → **reject + report**
  (recall is meant *within* the current synth). Live next/prev stays inside the loaded synth's
  folder by construction, so mismatch only arises on explicit cross-load.
- **Corrupt `.cuspreset` / `setStateInformation` fails** → report error, do not crash, keep current
  sound.
- **Empty folder** → `list` count 0; `next/prev` is a no-op. With ≥1 preset, `next/prev` wraps.
- **Root not yet pushed** → default fallback `~/Documents/CustosPresets/` + warning.
- **Preset-name sanitizing** — strip filesystem-illegal characters from the on-disk filename; the
  original display name is preserved inside the file's metadata.

## 6. Build & test plan (TDD, along the existing structure)

**New units (small, isolated — like the existing ones):**
- `PresetCodec` — `.cuspreset` serialize/parse (mirrors `StateCodec`).
- `PresetStore` — file IO: save/list/load/delete/rename, alphabetical order, sanitizing, root
  management. Pure logic against a temp directory (like `FavoritesStoreTest`).
- OSC verb handling in `CustosOscServer` + feedback shapes (like `OscCommandTest`).
- Custos window: "Save" button + name field + direct-load list.

**Tests:** codec round-trip; store save/list/load/delete/rename against a temp dir; sanitizing;
collision-overwrite; alphabetical order + wrap; class-id-mismatch detection; OSC verb/feedback
shape.

**Build/run:** load `vcvars64.bat`, use the VS-bundled cmake against `C:\dev\custos\build` (Ninja
auto-reconfigures), run `build/tests/custos_tests.exe`.

**E2E:** load a synth in GP → save a preset via the Custos window → verify the file on disk →
recall it over OSC → verify the ack.

## 7. Deliverables

1. **Custos PR** — `PresetCodec` + `PresetStore` + OSC verbs + Custos-window save/load UI.
2. **KM handoff** — preset-manager tab (separate KM work, later; consumes the OSC wire above).
3. **GP-Script handoff prompt** — recall axis (next/prev/set) in the Voice-Selector.
