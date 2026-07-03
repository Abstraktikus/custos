# Custos M3 — Runtime Synth Load via OSC + Self-Contained Persistence (Design)

**Date:** 2026-07-04
**Status:** Approved (brainstorming complete, ready for planning)
**Milestone:** M3 (builds on M1/M2; precedes M4 glitch-free swap, M5 resident mode)

---

## 1. Purpose

Make Custos **dynamic**. Until now the inner synth is hard-coded at build time (a fixed CS-80
wrapper). M3 lets Kapellmeister load/replace the inner synth **at runtime over OSC**, and persists
the choice + the synth's state in Custos's own VST state so a gig **reloads correctly standalone**
(without Kapellmeister running). This closes the core functional gap — the whole reason Custos
exists (dynamic voice switching that GP's runtime `ReplacePlugin` can't do).

M3 delivers a **safe** runtime swap (no crash, no data race; a brief audible gap is acceptable).
**Glitch-free** swap (settling, click-free) is **M4**.

---

## 2. Scope

### In scope
- An **OSC receiver** in Custos (its own OSC-in port) for meta commands from KM: `load`, `clear`,
  with an **ack** back to KM.
- **Safe runtime swap** of the inner synth (SpinLock try-lock in `processBlock`; silence while
  swapping), resolving the M1-flagged audio-thread race + `releaseResources()` on the outgoing synth.
- **Self-contained persistence**: `getStateInformation` saves the synth path + inner state chunk;
  `setStateInformation` reloads and restores. KM not required for restore.

### Out of scope (later milestones)
- **Glitch-free** swap (settle tails, click-free / crossfade) — **M4**.
- Multi-instance OSC addressing (port = base + slot) and the variant build matrix — **M4**.
- Window OSC control (show/hide/setRect/focus) + docking over a KM panel — **separate spec**.
- Resident mode (effective count) — **M5**.
- No changes to the M1 facade/passthrough or M2 editor/window model beyond what the swap requires.

---

## 3. Architecture

Three units, each independently testable:

### 3.1 `CommandHandler` (transport-free command logic)
The meaning of each command, decoupled from OSC so it is unit-testable without a socket.
- `void load (const juce::String& path)` — load/replace the inner synth (safe swap, §3.3).
- `void clear ()` — unload the inner synth → silent facade (all inert).
- Returns/reports a result (`{ok, innerCount, error}`) the OSC layer turns into an ack.
Implemented as methods on `CustosProcessor` (it owns `inner`, the facade, the loader). The
"CommandHandler" is a conceptual seam: tests call `processor.load(path)` / `processor.clear()`
directly; the OSC receiver is a thin adapter on top.

### 3.2 `CustosOscServer` (OSC transport adapter)
Owns a `juce::OSCReceiver` bound to `CUSTOS_OSC_PORT` (compile-def, default 9100) and a
`juce::OSCSender` for acks. Receiver listeners fire on the **message thread**.
- `/custos/load  <path:string>` → `processor.load(path)` → ack `/custos/ack "loaded <path> count=<n>"`.
- `/custos/clear`               → `processor.clear()` → ack `/custos/ack "cleared"`.
- On any failure → ack `/custos/ack "error <msg>"`.
- Ack destination is configurable (compile-def `CUSTOS_ACK_HOST`/`CUSTOS_ACK_PORT`, default
  `127.0.0.1:8000` — the KM hub).
- **Single instance for M3.** If the port is already bound (a second Custos instance), binding
  fails gracefully — log it, no crash; that instance simply has no OSC-in. Multi-instance
  addressing (port = base + slot) is M4.
- Owned by `CustosProcessor`, created after construction, destroyed before `inner` in the dtor.

### 3.3 Safe runtime swap
The load (creating the `AudioPluginInstance`) is slow → **message thread**. The pointer exchange
is guarded so the audio thread never touches a half-swapped `inner`:
- A `juce::SpinLock swapLock`.
- `processBlock`: `juce::SpinLock::ScopedTryLockType tl (swapLock);` — if `tl.isLocked()` and
  `inner != nullptr`, forward to inner; otherwise **`buffer.clear()`** (silence).
- `load(path)`: **first `hideSynthWindow()`** — the M2 synth window hosts the *outgoing* inner's
  editor, so it must be destroyed before the old inner is (else use-after-free). Then build +
  `prepareToPlay` the new inner **outside** the lock (slow work), take the lock, `unbindAll` the
  facade, `releaseResources()` + destroy the old inner, move in the new inner, `InnerBinding::bind`,
  release the lock, and `refreshEditor()` (the editor's `Synth: <name>`/button reflect the new synth;
  the window stays closed until reopened via the button or a later KM window command). The lock is
  held only for the fast pointer/bind swap, so the audio thread's try-lock rarely misses (one short
  silent block at most).
- This is **safe** (no crash, no data race) but may briefly gap/click. **M4** replaces it with a
  lock-free, click-free swap (settle + crossfade).
- Removes the M1 review carry-forwards: the `inner`-pointer/audio-thread race and the missing
  `releaseResources()` on the outgoing synth.

### 3.4 Persistence (`getStateInformation` / `setStateInformation`)
Custos's VST state becomes self-describing:
- **Format** (`getStateInformation`): `"CUS1"` magic + version byte + `int32 pathBytes` + UTF-8 path
  + `int32 innerBytes` + the inner synth's `getStateInformation()` chunk. Empty path + 0 inner bytes
  when no synth is loaded.
- **`setStateInformation`** (message thread, gig load): parse → `load(path)` (safe swap) → apply the
  inner chunk via `inner->setStateInformation(...)` → facade already bound by `load`. Fully
  synchronous; **KM not required**.
- **Boot:** the constructor no longer treats the hard-coded synth as the primary source. The synth
  arrives via `setStateInformation` (gig) or a later OSC `load`. `CUSTOS_HARDCODED_SYNTH_PATH`
  remains a **dev/headless default** only (loaded in the constructor if set; `setState` overrides it).

---

## 4. Data flow & error handling

```
KM --OSC /custos/load "C:/.../Diva.vst3"--> CustosOscServer (msg thread)
   -> processor.load(path)
      -> SynthLoader.loadVST3(path)   (slow, msg thread)
         success -> safe-swap inner, bind facade -> ack "loaded <path> count=<n>"
         failure -> KEEP current inner (or stay silent if none) -> ack "error <msg>"
Gig save -> getStateInformation -> "CUS1" + path + inner state chunk
Gig load -> setStateInformation -> load(path) + inner->setStateInformation(chunk) -> facade bound
            (synth path missing/uninstalled -> load fails -> silent facade + log; gig never crashes)
```

- **Load failure** never loses a working synth: the old inner stays; only a fresh `clear` or a
  successful `load` replaces it.
- **Port bind failure** (second instance): logged, no OSC-in for that instance, no crash.
- **Out-of-range / malformed OSC args**: ignored with an `error` ack; never crash.

---

## 5. Testing

- **Unit (headless, fakes — no socket, no real synth):**
  - `CommandHandler`/processor: `load`/`clear` semantics against a seam that injects a fake loader
    (so no real VST3 needed) — `load` binds the facade to the new (fake) inner and reports count;
    `clear` leaves the facade inert; a failing load keeps the previous inner + reports error.
  - OSC arg parsing: a pure function `parseCommand(address, args) -> Command` tested directly.
  - Persistence round-trip: `serializeState(path, innerChunk)` ↔ `parseState(block)` byte-exact,
    including empty/no-synth; and `setStateInformation` calls the loader with the parsed path and
    applies the inner chunk (verified via a fake inner recording `setStateInformation`).
  - Safe-swap: `processBlock` outputs silence while `swapLock` is held; forwards otherwise; a swap
    calls `releaseResources()` on the outgoing inner (fake records it).
- **E2E (autonomous, driven by me):** send `/custos/load <path>` via OSC, then use the GP-side
  `/KM/Plugin` harness (`tools/kmplugin.py`) to confirm `PCount`/`PList` now mirror the **new**
  synth; save the gig, reload, confirm the synth + patch restored; point `load` at a missing path
  and confirm the ack error + no crash.

---

## 6. Milestones (M3 tasks)

- **T1: Safe runtime swap + `load`/`clear` on the processor.** Introduce `swapLock`; `processBlock`
  try-lock→silence; `load(path)` (build+prepare outside lock, swap+bind inside) with
  `releaseResources()` on the outgoing inner; `clear()`. Refactor the constructor's hard-coded load
  to go through `load()`. Unit tests against a fake loader seam.
- **T2: Persistence.** `serializeState`/`parseState` + `getStateInformation`/`setStateInformation`
  wired to `load()` + inner chunk restore. Unit round-trip tests.
- **T3: OSC channel.** `CustosOscServer` (juce_osc receiver on `CUSTOS_OSC_PORT` + ack sender);
  `/custos/load` and `/custos/clear` → processor + ack; graceful bind-failure. `parseCommand` unit
  test. Then the autonomous E2E (OSC load → `/KM/Plugin` verify → gig save/reload).

---

## 7. Relationship to neighbouring milestones

- **From M1/M2:** reuses `SynthLoader`, `InnerBinding`, the facade, and `attachInner` (now folded
  into `load()`); the M2 synth window keeps working (on swap, the window is torn down safely — its
  hosted editor belongs to the outgoing inner, so `load()` closes the window before destroying the
  old inner, mirroring the dtor ordering).
- **To M4:** the safe swap becomes glitch-free (lock-free pointer exchange + settle/crossfade); the
  variant build matrix adds `CUSTOS_SLOT` and multi-instance OSC ports (base + slot).
- **To M5:** resident mode reports the effective inner count; the persisted path lets a resident
  wrapper know its synth at boot before the host queries the count.
