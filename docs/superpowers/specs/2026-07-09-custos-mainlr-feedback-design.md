# Custos Main L/R Fold — Feedback + Query (design)

**Date:** 2026-07-09
**Status:** approved (design), pending implementation plan
**Scope:** close the one remaining write-only blind spot in the external-control contract — the Main L/R audio-fold flag (`/custos/mainlr`) is currently set-only. KM cannot read back whether an instance is folded (`1`) or mapped across buses (`0`). Add feedback-on-set plus an on-demand query, mirroring the existing `/custos/midi/route` + `/custos/midi/query` pattern.

## Motivation

`/custos/mainlr <on:int>` applies the audio-fold and returns nothing. Every other stateful control that KM cares about is observable (`/custos/here` for mode/inner, `/custos/midi/query` for the route map, `/custos/loaded` for the inner, `/custos/window/rect` feedback for geometry). Main L/R is the sole exception the contract calls out as *"Set-only (no feedback)"*. This spec removes that exception.

Deliberately **not** part of this change (KM decisions, 2026-07-09):
- **Volume trim** stays write-only — out of scope; only Main L/R is being retrofitted.
- **No status consolidation.** We add a targeted `/custos/mainlr/query`, not a combined `/custos/status` snapshot.

## Contract additions (protoVer stays 2)

The change is additive and back-compatible, so **`kProtoVersion` remains 2**. KM detects support behaviourally: it sends `/custos/mainlr/query` and either receives `/custos/mainlr` or times out — consistent with the existing "no heartbeat, KM re-probes" model. The whole rig is rebuilt together, so no version gate is needed.

### KM → Custos (send to `127.0.0.1:BASE+N`)

| Address | Args | Effect |
|---|---|---|
| `/custos/mainlr/query` | — | no state change; triggers a `/custos/mainlr` feedback reply with the current flag |

`/custos/mainlr <on:int>` (inbound set) is **unchanged**.

### Custos → KM (send to `127.0.0.1:8000`; first arg always `N`)

| Address | Args | Meaning |
|---|---|---|
| `/custos/mainlr` | `N, on:int` | Main L/R fold feedback — the current flag (`1` = summed onto stereo Out 1, `0` = inner pairs mapped across the 5 buses). Emitted after an applied `/custos/mainlr` set and in reply to `/custos/mainlr/query` |

This reuses the `midi/route` direction convention: the **set** carries no `N` (the port encodes it); the **feedback** is `N`-first so KM can demux on `:8000`. Same address, direction-dependent arg shape — exactly as `/custos/midi/route`.

## What emits (and what does not)

Feedback fires on exactly two paths:
1. **OSC set** — `Command::MainLR` handler, after `proc.setMainLROnly(on)`, calls `proc.emitMainLR()` (twin of the `emitMidiRoute()` call after `setMidiRoute`).
2. **Query** — new `Command::MainLRQuery` handler calls `proc.emitMainLR()` only, no state change (twin of `Command::MidiQuery`).

**Not emitting:** the Custos-editor toggle (`CustosEditor.cpp:117`) stays as-is — an operator flip in the Custos UI does **not** push feedback (KM decision 2026-07-09, minimal-change). KM stays authoritative-on-write and re-queries when it needs the truth. (Trade-off accepted: a UI-driven flip is invisible to KM until it next sends `/custos/mainlr/query`.)

## Components touched

- **`OscContract.h`** — new `inline juce::OSCMessage buildMainLR (int n, bool on)` returning `("/custos/mainlr", n, on ? 1 : 0)`. Twin of `buildMidiRoute`.
- **`CustosProcessor` (.h/.cpp)** — new `void emitMainLR();` → `if (outboundSink) outboundSink (buildMainLR (identityN, mainLROnly()));`. Twin of `emitMidiRoute()`.
- **`CustosOscServer.h`** — add `MainLRQuery` to the `Command::kind` enum.
- **`CustosOscServer.cpp`** — parse `/custos/mainlr/query` → `{ Command::MainLRQuery }`; handler `case MainLR:` gains a trailing `proc.emitMainLR();`; new `case MainLRQuery: proc.emitMainLR();`.
- **`docs/osc-contract.md`** — §2 add the query row, §3 add the feedback row, §4 amend the Main L/R bullet from *"Set-only (no feedback)"* to describe feedback + query.

No state-format change (the flag already persists in state v4). No editor change. `kProtoVersion` unchanged.

## Testing

- **`OscCommandTest`** — `/custos/mainlr/query` parses to `Command::MainLRQuery`; malformed variants fall through to `Unknown`. Existing `/custos/mainlr <on>` → `Command::MainLR` case unchanged.
- **`OscContractTest`** — `buildMainLR(N, true/false)` produces `/custos/mainlr` with args `[N, 1|0]`. If the harness captures the outbound sink: applying `Command::MainLR` and `Command::MainLRQuery` each emit exactly one `/custos/mainlr` feedback carrying the current flag.

## Out of scope

Volume feedback/query, any `/custos/status` consolidation, editor-toggle feedback, protoVer bump.
