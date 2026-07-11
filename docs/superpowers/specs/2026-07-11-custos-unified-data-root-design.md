# Custos Unified Data Root — Design

**Date:** 2026-07-11
**Branch:** `feat/unified-data-root`
**Status:** Design approved (brainstorming), ready for implementation plan

## Problem

Custos stores the operator's curated instrument/favourites database in
`%APPDATA%\Custos\instruments.json` (canonical) with `%APPDATA%\Custos\favorites.json`
as a legacy read fallback. This path is:

- **Hidden** — `%APPDATA%` is not a folder the musician browses to.
- **Not configurable** — hardcoded to `userApplicationDataDirectory`.
- **Outside any backup plan** — the operator cannot point it at a folder his backups cover.

The operator fears losing the work of curating his favourite slots (which VST voices sit in
which slot, per synth) because that data lives somewhere he cannot reach or protect.

The `.cuspreset` preset store, by contrast, **already** has a configurable, OSC-settable,
persisted root (`presetRoot`, default `~/Documents/CustosPresets`, verb
`/custos/preset/setroot`, echo `/custos/preset/root`). The fix is to bring the
instrument/favourites database under that same roof.

Note: on-disk inspection (2026-07-11) confirmed there are **no** real user `.cuspreset`
files — all found were test fixtures (`Apple`/`Banana` under `FakeInner` in `%TEMP%`). The
operator's real curated work is entirely `instruments.json` (~41 KB). A stale
`presetRoot.txt` pointing at a since-deleted `%TEMP%` folder was reset to the default as
immediate hygiene (separate from this feature).

## Goal

Unify all Custos persistent data under the existing machine-global `presetRoot`, so a single
folder — chooseable, OSC-settable, backup-friendly — holds everything:

```
<root>/<classId-hex>/*.cuspreset      (unchanged)
<root>/instruments.json               (NEW — was %APPDATA%\Custos\instruments.json)
```

One folder, one backup, one OSC verb. The tiny pointer `presetRoot.txt` stays in
`%APPDATA%\Custos\` (path only, no payload).

## Scope

**In scope (Custos repo only):**

1. Make the instrument/favourites database (`instruments.json`) live under `presetRoot`.
2. Layered read resolution with legacy fallback (self-heal).
3. One-time copy-up (self-heal seed) when data is read from a legacy tier while a root is set.
4. Runtime re-point on `/custos/preset/setroot`.
5. New read-only query verb `/custos/preset/queryroot`.

**Out of scope (handoff, NOT this repo):**

- **Migration** of the operator's existing `%APPDATA%` files to the chosen root — Kapellmeister
  will drive this. Custos's self-heal seed (item 3) is an independent safety net, not the
  migration path; the two are idempotent and do not conflict.
- **Setting the root** in normal use — the operator / KM sends `setroot`.
- **KM reading `instruments.json` from the root** instead of hardcoded `%APPDATA%` — KM-side
  change, delivered as a handoff prompt (uses `queryroot`).

## Design

### Data model

There is exactly **one** logical dataset: `std::vector<custos::Favorite>`, persisted as JSON.
Historical naming: it began as `favorites.json`, was renamed/migrated to `instruments.json`
(canonical since the db-ownership work). `favorites.json` remains only as a legacy read tier.

### Path resolution

Introduce a root-aware path and a layered source resolver in `FavoritesStore`:

```
juce::File instrumentsFileIn (const juce::File& root);   // <root>/instruments.json

// First existing of, in order:
//   1. <root>/instruments.json         (configured location — authoritative once present)
//   2. %APPDATA%/Custos/instruments.json   (legacy canonical)
//   3. %APPDATA%/Custos/favorites.json     (legacy-legacy)
// Returns the resolved file + a flag "came from legacy tier".
```

`presetRootPath` is resolved in the `CustosProcessor` constructor before the
`CustosOscServer` is constructed, so the root is available at the load site
(`CustosOscServer` ctor, currently line 197).

### Load (startup)

At construction, resolve the source with the layered resolver against `proc.presetRoot()`:

- Read the favourites from the first existing tier.
- **Self-heal seed:** if the data came from a legacy tier (tier 2 or 3) AND `root` is a valid
  non-empty path, write it once to `<root>/instruments.json`. Idempotent — a second start reads
  tier 1 and skips the seed. This guarantees the operator's data lands in the backup-friendly
  folder even if KM migration never runs.

### Write

On `FavEnd` (currently line 309), write to `instrumentsFileIn(proc.presetRoot())` instead of
the hardcoded `%APPDATA%` file. If `root` is empty, fall back to the legacy `%APPDATA%` path
(backward compatibility for a Custos with no root configured).

### Runtime re-point (`/custos/preset/setroot`)

`setPresetRoot` already persists the root and echoes `/custos/preset/root`. Extend it so the
favourites database follows the switch:

- If `<newroot>/instruments.json` exists → reload favourites from it (`setFavorites`) and emit
  the favourites feedback so KM/GP see the new set.
- Else → seed `<newroot>/instruments.json` from the current in-memory favourites (data follows
  the move).

This ensures switching folders at runtime never blanks the favourites and always carries the
data along.

### Query verb (`/custos/preset/queryroot`)

New read-only verb, twin of the `/custos/mainlr/query` pattern (PR #10):

| Address | Args | Effect |
|---|---|---|
| `/custos/preset/queryroot` | (none) | emit `/custos/preset/root <N> <path>`, change nothing |

KM uses it to learn the source/target for migration and to verify; we use it to verify the
feature live without mutating config.

### Backward compatibility

- `presetRoot` default stays `~/Documents/CustosPresets`; existing `setroot` semantics for
  `.cuspreset` unchanged.
- A Custos with no root configured (empty `presetRootPath`) still reads/writes the legacy
  `%APPDATA%` location — nothing breaks for an un-migrated install.
- `protoVer` unchanged (additive verb + relocated file; no wire-format change to existing
  messages).

## Components touched

| File | Change |
|---|---|
| `src/FavoritesStore.h/.cpp` | Add `instrumentsFileIn(root)` + layered source resolver returning (file, cameFromLegacy). |
| `src/CustosProcessor.h/.cpp` | Load favourites from resolved root at init + self-heal seed; write to `instrumentsFileIn(root)`; extend `setPresetRoot` to re-point favourites; helper to seed. |
| `src/CustosOscServer.cpp` | Load site uses root-aware resolver; write site uses root-aware path; parse + dispatch `/custos/preset/queryroot`. |
| `src/OscContract.h` | Add `PresetQueryRoot` command. |
| `docs/osc-contract.md` | Document `/custos/preset/queryroot`; note that `setroot` now also relocates the instrument DB. |
| `tests/*` | Cover: layered resolution order, self-heal seed idempotency, root-aware write, runtime re-point (both branches), queryroot echo, empty-root legacy fallback. |

## Error handling

- Unwritable root (permissions / missing drive): write fails gracefully, emit existing preset
  error channel; in-memory favourites remain intact; no crash. Load falls through to legacy tier.
- Empty/whitespace root path: treated as "no root" → legacy `%APPDATA%` behaviour.
- Corrupt `instruments.json` at a tier: `favoritesFromJson` yields empty; resolver may continue
  to next tier only if the file is *absent*, not if it is present-but-corrupt (avoid silently
  masking a corrupt authoritative file — surface empty rather than fall back).

## Testing strategy

Unit tests in the existing `FavoritesStoreTest` / `PresetStoreTest` style (temp-dir rooted, no
live GP):

1. Resolver returns tier 1 when `<root>/instruments.json` exists; tier 2 when only legacy
   canonical exists; tier 3 when only `favorites.json` exists; none → empty.
2. Self-heal seed writes `<root>/instruments.json` on legacy-tier read; second read is tier 1
   and does not re-seed (mtime / content stable).
3. Root-aware write lands at `<root>/instruments.json`; empty root writes legacy path.
4. `setPresetRoot` to a root with existing file reloads; to an empty root seeds from memory.
5. `queryroot` emits `/custos/preset/root N path` and mutates nothing.

Live E2E (my standing OSC-probe permission): `queryroot` → confirm `/custos/preset/root`;
`setroot <tmp>` → confirm favourites file appears at `<tmp>/instruments.json`; restore.

## Handoff (Kapellmeister — separate session)

Deliver a prompt covering:

- KM must read `instruments.json` from `<presetRoot>/instruments.json`, discovering the root via
  `/custos/preset/queryroot` → `/custos/preset/root`, not from hardcoded `%APPDATA%`.
- KM drives migration: query root, if `<root>/instruments.json` absent and `%APPDATA%` copy
  exists, copy it up (or simply send `setroot` and let Custos self-heal seed — decide in KM).
- Ordering guidance so KM migration and Custos self-heal do not double-write destructively
  (both idempotent; last-writer-wins on identical content).
