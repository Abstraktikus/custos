# Kapellmeister — Custos Unified Data Root: path discovery + migration

> Handoff artifact. Actionable only AFTER Custos `feat/unified-data-root` (esp.
> `/custos/preset/queryroot` + relocated `instruments.json`) is merged + deployed.
> KM repo only — do NOT modify Custos; the verbs below are fixed contract.

## Context
Custos moved its instrument/favourites database out of the hidden, non-configurable
`%APPDATA%\Custos\instruments.json` into its already-configurable machine-global **data root**
(`presetRoot`, default `~/Documents/CustosPresets`). Goal: the operator's curated favourites
live in ONE backup-friendly folder together with the `.cuspreset` presets.

`instruments.json` is the single canonical favourites/instrument DB (a JSON array of the same
`Favorite` records KM already knows). `favorites.json` is a legacy read-only fallback name.

Custos now exposes the root over OSC and self-heals from the legacy location. KM's job here is
NOT to move Custos data by reaching into `%APPDATA%` — it is to (a) know where the root is,
(b) let the operator choose it, and (c) make sure the operator's existing curation ends up there.

## Custos OSC surface (contract, already shipped)
Standard Custos addressing: identity `N` (1..15), send to `127.0.0.1:(9100+N)`; Custos replies to
the KM hub `:8000` with `N` as the first argument.

| Direction | Address | Args | Meaning |
|---|---|---|---|
| KM → Custos | `/custos/preset/setroot` | `path:string` | Set the data root (presets **and** instrument DB). Persisted machine-global. Echoes `/custos/preset/root`. On set, Custos **adopts** `<path>/instruments.json` if present, else **carries** its current in-memory favourites into `<path>/instruments.json`. |
| KM → Custos | `/custos/preset/queryroot` | (none) | Report the current root without changing anything. Echoes `/custos/preset/root`. |
| Custos → KM (:8000) | `/custos/preset/root` | `N:int, path:string` | The applied/current data root. Emitted in reply to both verbs above. |

The favourites file for a given root is always `<root>/instruments.json`.

## What KM must implement

### 1. Track the data root per Custos instance
- On connect / handshake, send `/custos/preset/queryroot` to each known Custos `N`; store the
  returned `path` (keyed by `N`).
- Also update the stored root whenever an unsolicited `/custos/preset/root N path` arrives (emitted
  on every `setroot`, including operator-driven ones).
- The root is machine-global in Custos, so all `N` should report the same path; if they diverge,
  surface a warning (a stale instance that hasn't restarted).

### 2. Let the operator choose the folder ("Ich setze den Ordner")
- Provide a folder picker in KM (natural home: the existing Custos/Voice-Selector config surface).
- On pick, send `/custos/preset/setroot <chosenPath>` to a live Custos `N`; confirm the
  `/custos/preset/root` echo before showing success.
- Recommend a backup-covered folder; resolve from KM's Locations settings — no hardcoded paths.

### 3. Migration of the operator's existing curation (core of this handoff)
Existing curation sits at `%APPDATA%\Custos\instruments.json` (canonical) or `favorites.json`
(older). When the operator points Custos at a new empty root, that work must arrive there. Two
idempotent paths — prefer the first:

- **Preferred — let Custos self-heal.** Send `/custos/preset/setroot <newRoot>`. Custos adopts
  `<newRoot>/instruments.json` if present, else carries its in-memory favourites there. On the next
  Custos start its resolver also copies a legacy `%APPDATA%` file up once. In most cases KM touches
  no files — just set the root and verify.
- **Fallback — KM copies.** Query the root; if `<root>/instruments.json` is absent AND a
  `%APPDATA%\Custos\instruments.json` (else `favorites.json`) copy exists, copy it to
  `<root>/instruments.json`. Idempotent (skip if target present).

### 4. Ordering / do-no-harm
- Query before you decide. Never overwrite `<root>/instruments.json` with stale `%APPDATA%` content
  after Custos may have seeded fresher data. Read the target first; if it exists non-empty, leave it.
- Last-writer-wins on identical content is fine; a destructive overwrite of newer curation is not.
- If KM reads `instruments.json` anywhere today, switch that read to the root-resolved path
  (`<root>/instruments.json`, discovered via step 1), with the same legacy `%APPDATA%` fallback for
  un-migrated installs. No hardcoded `%APPDATA%` read.

## Acceptance
- KM shows the current Custos data root (from `queryroot`) and lets the operator change it.
- After the operator sets a backup-covered folder, `<root>/instruments.json` exists with the full
  favourites list; no curation lost.
- KM reads favourites from the root-resolved location, with legacy fallback.
- Re-running migration is a no-op; no destructive overwrite of newer data.

## Boundaries
- KM repo only. Do not modify Custos. English UI + GitHub. Follow KM Locations/path conventions.
