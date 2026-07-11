# Kapellmeister — Bug report + defensive handling: Custos preset-root persistence anomaly

> Handoff/bug-report artifact for a KM session. Observed on Custos `feat/unified-data-root` (PR #13).
> KM repo only — the root-cause FIX is Custos-side (tracked separately); this covers KM-side handling.

## Severity / status
Open, root cause TBD. **Custos-side defect** (observed with KM OFF), reported to KM because KM
orchestrates `setroot` and is the natural owner of the shared data-root config. Low practical impact
today (see "Why low impact"). Split: root-cause hardening = Custos; defensive handling + optional
ownership = KM (this doc).

## What was observed (live, 2026-07-11, 10-instance rig, KM not running)
Custos `feat/unified-data-root` relocates the favourites DB to a configurable machine-global
`presetRoot`, persisted in `%APPDATA%\Custos\presetRoot.txt`. During live E2E:
- `/custos/preset/setroot <path>` was accepted: the instance echoed `/custos/preset/root <N> <path>`,
  and the favourites were correctly carried/adopted to the new root (file I/O to `<root>\instruments.json`
  worked).
- BUT `%APPDATA%\Custos\presetRoot.txt` was NOT updated on disk (mtime unchanged across two setroot calls
  to two different paths). Only one such file exists machine-wide, so the plugin isn't writing elsewhere.
- Conclusion: the in-plugin `writePresetRoot` (`File::replaceWithText` on `presetRoot.txt`) silently
  failed. Because 10 Custos instances share that single machine-global file, the leading suspicion is
  lock/contention (or an AV/OneDrive handle) on that file when written from the GP-hosted plugin process.
  A console unit-test process writes the same file fine.

## Consequence
The in-memory root switch + favourites carry/adopt WORK for the session, but the new root may NOT survive
a GP restart (presetRoot.txt not persisted) → on restart Custos could load a stale/previous root. Silent
before this branch; now Custos emits `/custos/preset/error` on a failed persist (see below).

## Why low impact right now
On this machine JUCE's userDocuments resolves to OneDrive (KFM), so the DEFAULT preset root is already
`C:\Users\marti\OneDrive\Dokumente Martin\CustosPresets` — a backup-friendly OneDrive folder where the
operator's real `.cuspreset` folders + `instruments.json` already live. So even with persistence failing,
the default root is the desired one. The bug bites only if the operator sets a NON-default root and expects
it to survive a restart.

## Custos contract facts KM can rely on (shipped in PR #13)
- `/custos/preset/queryroot` (no args) → `/custos/preset/root <N> <path>`, read-only, mutates nothing.
- `/custos/preset/setroot <path:string>` → applies + echoes `/custos/preset/root`; adopts `<path>/instruments.json`
  if present-non-empty, else carries current favourites there.
- On a FAILED config persist, `setPresetRoot` now emits `/custos/preset/error <N> "preset root not persisted"`
  (hardened in PR #13 — no longer silent). All favourites writes also surface `/custos/preset/error` on failure.
- Standard addressing: send to `127.0.0.1:(9100+N)`; replies land on the KM hub `:8000` with `N` first.

## What KM should do (defensive handling)
1. **Watch for `/custos/preset/error`** after any `setroot` and surface it to the operator ("root change not
   persisted — will not survive a restart"). Do NOT treat the `/custos/preset/root` echo alone as "persisted".
2. **Re-assert the root on connect/handshake:** since persistence is unreliable, after (re)discovering an
   instance, `queryroot` it and, if the operator's intended root differs from what the instance reports,
   re-send `setroot`. This makes KM the source of truth and self-heals a non-persisted root next session.
3. **Set the root on ONE instance, not fanned out:** the root is machine-global; writing it from all 10
   instances at once is exactly the suspected contention. Send `setroot` to a single instance; if all live
   instances must switch immediately, sequence the sends, don't blast them.

## Optional (architectural, KM's call)
Consider making KM the AUTHORITY for the Custos data-root: KM persists the operator's chosen root in KM's own
settings and asserts it to Custos on connect (per point 2), treating Custos's `presetRoot.txt` as a best-effort
cache. This sidesteps the 10-instance shared-file write contention entirely and matches the KM-as-sole-writer
pattern used elsewhere. Not required to close this bug, but it's the clean long-term shape.

## Custos-side follow-up (NOT KM — tracked separately)
Root-cause the in-plugin `writePresetRoot` failure (repro in a controlled multi-instance run; check for the
file lock / retry the write / consider per-instance vs machine-global config). The error is now visible, which
will pinpoint it on the next live run.

## Repro (self-contained; KM hub :8000 must be free — stop any other :8000 listener)
1. With GP hosting ≥1 Custos instance N (new build), bind an OSC receiver on `0.0.0.0:8000`.
2. Send `/custos/preset/queryroot` to `127.0.0.1:(9100+N)` → note the reported root R from `/custos/preset/root`.
3. Read `%APPDATA%\Custos\presetRoot.txt` mtime.
4. Send `/custos/preset/setroot <some other existing folder T>` to the same instance → confirm the
   `/custos/preset/root <N> T` echo AND that `T\instruments.json` appears (carry worked).
5. Re-read `%APPDATA%\Custos\presetRoot.txt`: BUG = its mtime/content is unchanged (still the pre-setroot value),
   i.e. the root was not persisted despite the echo. With the hardened build, also expect a
   `/custos/preset/error <N> "preset root not persisted"` if the write truly failed.
6. Restore: `setroot` back to R.
