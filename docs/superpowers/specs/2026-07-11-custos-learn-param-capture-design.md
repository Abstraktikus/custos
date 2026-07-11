# Custos Learn — Parameter Capture for Macro Binding (design)

**Date:** 2026-07-11
**Status:** approved (design), pending implementation plan
**Scope:** give KM a way to bind a macro by *moving the knob* instead of hunting for a facade index. When KM opens a short "Learn" window on a specific Custos, Custos streams which facade parameter(s) the user moves and to what value. KM picks the intended parameter (largest movement, or a candidate list) and derives Min/Max from the value sweep in the same gesture. This is the identity/range half of macro binding; live display of an already-bound macro is out of scope (KM already has it — see below).

## Motivation

Binding a KM/GP macro to a Custos facade parameter today means knowing the facade index up front. The facade is a stable 5000-slot layer mirroring an arbitrary inner synth, so index 2373 is meaningful but opaque. The natural authoring gesture — "click Learn, wiggle the knob on the synth, done" — needs Custos to report *which inner parameter just moved*, because only Custos sees the hosted synth's parameters.

The hook already exists and is deliberately empty: `CustosProcessor::audioProcessorParameterChanged (inner, index, newValue)` (`CustosProcessor.h:180`) is an overridden no-op, and Custos is already registered as an `AudioProcessorListener` on the inner synth at every load (`CustosProcessor.cpp:99`). Its comment records *why* it is empty: per-value automation changes must not churn the facade binding. This spec keeps that default — the callback stays a no-op **except while a Learn window is open**.

### Explicitly NOT in this change (KM decisions, 2026-07-11)

- **Live display of a moving macro (Fall 1).** When a macro drives a facade param from outside, KM already knows the *value* (it sent it) and the *name* (from the `/custos/param` dump taken at load, F1). No Custos feedback is required for display. This spec is only about the **Learn** gesture.
- **No source attribution.** Custos does not try to distinguish "user turned this knob" from "an internal LFO moved it." The **1–2 s window** is the mitigation: outside the window Custos reports nothing; inside it, KM takes what moved (and normally the operator is deliberately moving exactly one knob). Accepted trade-off: an LFO running during a Learn window can produce noise events; KM's "largest movement wins" heuristic and the operator's deliberate sweep handle the common case.
- **No GP mirror.** `learn/*` is KM-authoring traffic and stays hub-only (`:8000`), like `preset/saved` — it is not mirrored to GP `:54344`.
- **No protoVer bump.** `/custos/here` is unchanged; the verbs are purely additive. `kProtoVersion` stays at its current value (**3**). KM feature-detects behaviourally (send `learn/start`, receive `learn/started` or time out).

## Data flow

```
KM  --/custos/learn/start N-->  Custos :BASE+N
                                   |  learnActive = true; arm safety timer
   <--/custos/learn/started N----  |  ack
                                   |
operator turns knob on synth --> inner->audioProcessorParameterChanged(innerIdx, val)   [may be audio thread]
                                   |  push {innerIdx, val} into a lock-free FIFO (no OSC/alloc on RT)
                                   |
                          drain timer (~25 ms, message thread):
                            coalesce latest-per-idx, map innerIdx->facadeIdx,
                            resolve name, apply deadband, emit
   <--/custos/learn/moved N,facadeIdx,val,name--   (one per tick per moved param)
                                   |
KM  --/custos/learn/stop N----->   |  (or safety timeout ~10 s)
   <--/custos/learn/stopped N,reason-  |  ack; learnActive = false
```

KM accumulates the stream and decides: winning `facadeIdx` = largest span (or a user pick from a candidate list); `min`/`max` = smallest/largest `val` seen for that index across the sweep. Custos performs no winner selection.

## Contract additions (protoVer unchanged, currently 3)

### KM → Custos (send to `127.0.0.1:BASE+N`)

| Address | Args | Effect |
|---|---|---|
| `/custos/learn/start` | `N:int` | open the Learn window; arm the safety timer. A second `start` while already open is idempotent: it re-arms the safety timer and re-emits `learn/started` (no duplicate streaming state, no second drain timer). |
| `/custos/learn/stop` | `N:int` | close the Learn window. |

### Custos → KM (send to `127.0.0.1:8000`; first arg always `N`)

| Address | Args | Meaning |
|---|---|---|
| `/custos/learn/started` | `N` | window is open; streaming armed |
| `/custos/learn/moved` | `N, facadeIdx:int, value:float, name:string` | one per drain tick per moved facade param; `value` normalised 0..1; `name` is the facade/inner parameter name (carried on every event, stateless — KM may ignore repeats) |
| `/custos/learn/stopped` | `N, reason:string` | window closed; `reason` is `"stop"` (explicit) or `"timeout"` (safety) |

## Behaviour details

**Gating.** `audioProcessorParameterChanged` does real work only while `learnActive`; otherwise it returns immediately (the existing `parameterInfoChanged` re-bind path in `audioProcessorChanged` is untouched — Learn only uses the per-value callback).

**Threading.** The per-value callback may fire on the audio thread (host automation) as well as the message thread (GUI). It must not allocate or send OSC. It writes `{innerIdx, float}` into a lock-free single-producer/single-consumer ring; a message-thread timer drains it. All OSC emission happens on the message thread via the existing `outboundSink`.

**Coalescing & rate.** Drain timer ~25 ms. Within a tick, collapse to the **latest value per index** (a human sweep produces plenty of samples; the operator dwells at the end-stops, so Min/Max are captured without sending every intermediate). Value need not be sample-accurate — this drives display/authoring, not audio.

**Deadband.** Drop changes smaller than ε ≈ 0.001 (normalised) from the previously emitted value for that index, to suppress dither/noise. A single discrete step of a coarse param is far larger than ε and passes.

**Index range.** The callback reports the **inner** index. Map to the facade index (1:1 within `boundCount`). Ignore `innerIdx >= boundCount` — the unbound facade tail cannot host a macro binding anyway. Emit the **facade** index (what KM binds to), consistent with `buildParam`.

**Targeting.** Learn is per-instance: KM sends `learn/start` only to the `N` it is binding for. No broadcast, no fan-out across the rig.

**No inner loaded.** `learn/start` still returns `learn/started` (KM's UI state stays consistent); no `moved` events flow until/unless a synth is present.

**Safety timeout.** If no `stop` arrives within ~10 s, Custos closes the window itself and emits `learn/stopped N "timeout"`. Guards against a lost `stop` packet leaving the stream armed.

## Components touched

- **`OscContract.h`** — three new builders:
  - `buildLearnStarted (int n)` → `("/custos/learn/started", n)`
  - `buildLearnMoved (int n, int facadeIdx, float value, const juce::String& name)` → `("/custos/learn/moved", n, facadeIdx, value, name)`
  - `buildLearnStopped (int n, const juce::String& reason)` → `("/custos/learn/stopped", n, reason)`
- **`CustosProcessor` (.h/.cpp)** —
  - state: `std::atomic<bool> learnActive`, the lock-free FIFO of `{innerIdx, value}`, a per-index "last emitted value" map for the deadband, a drain `juce::Timer` (or reuse the existing timer infrastructure), and the safety-timeout timer.
  - fill the previously-empty `audioProcessorParameterChanged` to enqueue when `learnActive`.
  - `void startLearn();` — set flag, emit `buildLearnStarted`, arm safety timer, start drain timer.
  - `void stopLearn (const juce::String& reason);` — clear flag, stop timers, flush, emit `buildLearnStopped`.
  - drain routine — coalesce, map inner→facade, deadband, `outboundSink (buildLearnMoved (...))`.
- **`CustosOscServer.h`** — add `LearnStart`, `LearnStop` to the `Command::kind` enum.
- **`CustosOscServer.cpp`** — parse `/custos/learn/start` → `{ Command::LearnStart }` and `/custos/learn/stop` → `{ Command::LearnStop }`; handlers call `proc.startLearn()` / `proc.stopLearn("stop")`. Learn addresses are **not** added to `gpMirrorsFeedback` (hub-only).
- **`docs/osc-contract.md`** — new inbound rows (§2), new feedback rows (§3), and a short "Learn" subsection describing the windowed capture model and the KM-side winner/Min-Max derivation.

No state-format change (Learn is transient, nothing persists). No editor change. `kProtoVersion` unchanged (currently 3).

## Testing

- **`OscCommandTest`** — `/custos/learn/start` and `/custos/learn/stop` parse to `Command::LearnStart` / `Command::LearnStop`; malformed variants fall through to `Unknown`.
- **`OscContractTest`** — `buildLearnStarted/moved/stopped` produce the right addresses and arg layouts; `gpMirrorsFeedback("/custos/learn/started"|"/moved"|"/stopped")` → **false** (hub-only).
- **`LearnProcessorTest`** (new, FakeInnerProcessor drives `parameterChanged`, capturing `outboundSink`):
  - no `moved` events emitted while `learnActive == false`.
  - after `startLearn()`: `started` emitted once; a burst of rapid `parameterChanged` on one index collapses to one `moved` per drain tick (coalescing).
  - deadband: a sub-ε change emits nothing; a supra-ε change emits.
  - `innerIdx >= boundCount` produces no `moved`.
  - facade index in `moved` matches the inner index within `boundCount`.
  - `stopLearn("stop")` emits `stopped` with reason `"stop"`; safety timeout path emits reason `"timeout"`.
  - re-entrant `startLearn()` while active re-emits `started` (idempotent for KM UI) and re-arms the safety timer; assert a single active window and no duplicate drain timer / streaming state.

## Out of scope

Live display of an already-bound macro (Fall 1 — KM-covered), source attribution (user vs LFO), GP mirroring of `learn/*`, protoVer bump, any persisted Learn state, and the KM-side UI (winner heuristic, candidate list, Min/Max capture, ctrlmap write) — the KM side is a separate handoff, consistent with the cross-project split.
