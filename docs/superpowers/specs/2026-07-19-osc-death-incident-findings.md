# OSC total death 2026-07-19 — incident findings (Phase 1: root cause)

**Status:** investigation findings, pre-fix
**Date:** 2026-07-19
**Repro:** `tests/FacadeGuardTest.cpp` (this branch)

## Incident

During live use, a big synth was directed (via OSC) at a small-facade Custos. Afterwards
*every* Custos instance's OSC port (9101..9110) was dead. Favourites browsing had also been
observed empty (silent `wrapped=1` stepping).

## Evidence

1. **Windows Event Log / WER**: `AppHang_GigPerformer5.ex_…` report, 2026-07-19 06:30:36,
   `EventType=AppHangB1`, `IsFatal=1` — Gig Performer **froze** ("reagiert nicht") and was
   closed by the operator. **Not** an access-violation crash (no Event 1000).
2. The WER module list confirms the **facade ladder live in the rig**: `Custos 1000.vst3`,
   `Custos 3000.vst3`, `Custos 5000.vst3`, `Custos 10000.vst3` — plus the big synths
   (`Omnisphere.vst3`, `JD-800.vst3`, …).
3. The WER module list shows **debug CRT DLLs** (`ucrtbased.dll`, `MSVCP140D.dll`,
   `VCRUNTIME140D.dll`): `scripts/deploy.cmd` builds the deployed Custos ladder with
   `-DCMAKE_BUILD_TYPE=Debug`. Debug builds are drastically slower (facade construction,
   binding, iterator checks) — an aggravating factor for message-thread stalls.
4. **On-disk instrument DB is healthy**: `<presetRoot>/instruments.json` (OneDrive
   CustosPresets, last write 2026-07-11) holds 115 entries, 47 favourites, `slots > 0` for
   ALL entries. The legacy `%APPDATA%/Custos/instruments.json` (148 entries) has `slots = 0`
   everywhere. So "favourites empty / no size data" was a **runtime state**, not data loss —
   prime suspect: an empty/failed read of the OneDrive file at GP boot (Files-on-Demand /
   sync lock), which empties the in-memory list AND disables every slots-based guard at once.

## Root cause chain

1. All Custos instances in the GP process receive OSC via `juce::OSCReceiver` in its default
   **message-loop delivery** mode → every instance's `oscMessageReceived` runs on the ONE
   shared message thread.
2. `oscMessageReceived` executes heavy work **synchronously in the callback**:
   `Command::Load` → `proc.load()` → `SynthLoader::loadVST3` (full VST3 instantiation +
   `prepareToPlay`) on the message thread.
3. A big-synth instantiation that stalls/freezes therefore freezes GP's UI **and** the OSC
   processing of **all** instances (sockets stay bound; nothing is dispatched) — exactly the
   observed "all 10 ports dead". WER's AppHang report is that freeze.
4. The facade guard (`favouriteFits`) covers only browse stepping and the browse commit.
   The direct load paths accept a known-oversized synth unchecked:
   - `/custos/load <path>` (KM) → `CustosProcessor::load` — **no check**
   - `/custos/instrument/load <name>` (GP song load) → `loadByName` — **no check**
   - `/custos/instrument/set` — cursor jump unchecked (commit checks; OK)
   And every check is vacuous when `slots <= 0` (unknown ⇒ "fits").

## Consequences (fix plan, this branch)

- **Guard all load paths**: refuse a list-known oversized synth in `load()` itself (single
  choke point: browse commit, `loadByName`, `/custos/load` with a list path all funnel there),
  with an explicit error ack; `slots <= 0` stays permitted (an unpushed DB must not block
  loads) — documented policy.
- **DB-read robustness**: when the resolved instruments file exists but reads empty, retry
  and say so loudly (trace + ack), instead of silently running guardless with no favourites.
- **Report empty favourites explicitly** on scope-0 browse instead of silent `wrapped=1`.
- **Architecture (to discuss, separate step)**: move instantiation off the message thread
  (async load). "Re-bind/self-heal ports" is the wrong medicine — the sockets never died,
  the thread did.
- **Deploy builds**: switch `deploy.cmd` to a Release/RelWithDebInfo ladder (debug CRT in the
  live rig, see Evidence 3) — separate change.
