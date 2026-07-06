# Custos OSC Contract v2 — Design

**Status:** design (brainstormed 2026-07-05). Extends contract v1 (`docs/osc-contract.md`).
**protoVer:** 1 → **2**.

## 1. Context & goal

KM's goal is a UI that embeds each hosted synth position-accurately and drives a **flexible
rig with fewer, reusable Custos slots** (multitimbral inners, layered/targeted play, resident
favourites). To get there, KM needs three things over OSC that v1 does not provide, plus a
build-time facade-size ladder so the `.gig` stays lean.

**Framing decisions (locked in brainstorming):**

- **Custos never reads `VstDatabase.txt`.** It is a pure OSC receiver. Where KM *sources* and
  *persists* the driving metadata (extra DB columns, a sidecar, measured vs. manual) is
  **KM-owned and out of scope here.** This spec defines only the **wire** — what flows over OSC,
  in which direction, in what shape.
- **Correction captured:** `VstDatabase.txt` `ParamDown/ParamUp` are the **preset Prev/Next
  paging** parameter indices — they say *nothing* about a synth's total parameter count. They
  must not be used to infer facade sizing.
- **v2 is the whole additive tier since the v1 lock** (`2155ac4`). It folds in the already-merged
  but not-yet-consumed additions — favourites `brand`, window control + window feedback — **plus**
  the three new areas below. So KM negotiating "v2" gets one coherent consumption target instead
  of a drip of v1-additive changes.

## 2. Scope

Additive only; every change appends fields or adds verbs so a v1 peer degrades gracefully.

1. **Facade-capacity & overflow reporting** (Custos → KM)
2. **MIDI channel routing matrix** (KM ↔ Custos, persisted in VST state)
3. **Richer parameter dump** for KM's control surface (Custos → KM)
4. **Facade-size build ladder** (build/packaging — no wire change beyond area 1's report)

**Non-goals:** KM-side storage/UI; the favourites-push `brand` and window verbs themselves
(already merged, only referenced here for the version story); actual parameter *value* control
(stays on the GP-hosted facade / ctrlmap path — unchanged).

## 3. Area 1 — Facade capacity & overflow reporting

KM must know **which variant** an instance is (see §6) and whether a loaded synth **outgrew** it.

| Message | v1 | v2 |
|---|---|---|
| `/custos/here` | `N, protoVer, mode, inner, boundCount, port` | `… port, facadeCap:int` |
| `/custos/loaded` | `N, path, boundCount` | `N, path, boundCount, innerTotal:int` |

- **`facadeCap`** = `kFacadeParamCount` of this build (`facadeSize()`), e.g. `2000`. Lets KM map
  instance → variant and check `boundCount ≤ facadeCap`.
- **`innerTotal`** = the inner synth's *full* parameter count (`inner->getParameters().size()`;
  `0` when cleared). When `innerTotal > facadeCap` the synth has more parameters than the facade
  mirrors → the top `innerTotal − facadeCap` params are **unbound / uncontrollable**. KM surfaces
  this as "synth outgrew this Custos variant — use a larger one."

Both fields are **appended** → a v1 KM reading the first args is unaffected.

## 4. Area 2 — MIDI channel routing matrix

A per-instance 16→16 routing map: for each input MIDI channel, either **drop** it or **remap** it
to an output channel handed to the inner synth. Enables layering (two instances listen on the same
input), targeted play (distinct inputs), multitimbral pass-through (identity 1..16), and
normalisation ("whatever GP sends, the inner always sees ch 1 → stable across synth swaps").

### Wire

Encode enable+target in **one int per input channel**: the 16 ints correspond to input channels
1..16 **in order** (1st arg = input ch 1 … 16th arg = input ch 16). Each value ∈ `0..16`: `0` =
drop that input channel, `1..16` = route it to that output channel. **Identity** (the default) =
each arg equals its own channel number (`1, 2, …, 16`).

| Address | Args | Dir | Effect |
|---|---|---|---|
| `/custos/midi/route` | `t1..t16 : int` | KM → Custos | set the whole map atomically; persist; apply |
| `/custos/midi/query` | — | KM → Custos | request current map |
| `/custos/midi/route` | `N, t1..t16 : int` | Custos → KM | **feedback**: current map (on any change / on query) |

Inbound = 16 args; outbound feedback = 17 args with **leading `N`** — same address, the leading `N`
marks it a report (identical convention to `/custos/window/rect`). **Default = identity**
(`t[i] = i`, all pass-through). Custos is deliberately dumb here: it applies the resolved 16-map;
the `Multitimbral` capability hint that *shapes* a sensible default lives entirely KM-side.

### Apply (audio thread)

In `processBlock`, before `inner->processBlock`, walk the `MidiBuffer`: for each channel-voice
message, look up `t[inputCh−1]`; drop if `0`, else rewrite the channel nibble to the target. Non-
channel messages (clock, SysEx) pass through untouched. Cost = O(events/block), microseconds — no
measurable audio-CPU impact (consistent with the existing zero-copy passthrough).

### Persistence — state format v3

`StateCodec` is versioned (`CUS1` + version byte; v1 = path+inner, v2 = +`identityN`). Add **v3**:
after `identityN`, write **16 bytes** (`t1..t16`, each `0..16`). `serializeState` writes v3;
`parseState` accepts v1/v2/v3, defaulting the map to **identity** for v1/v2 blobs. The map travels
in the gig with the instance, like `N`.

## 5. Area 3 — Richer parameter dump

KM builds its control surface from the param dump, so `/custos/param` needs more than name+value.

| Message | v1 | v2 |
|---|---|---|
| `/custos/param` | `N, idx:int, val:float, name:str` | `… name, defaultVal:float, numSteps:int, label:str` |

- **`defaultVal`** = `param->getDefaultValue()` (normalised 0..1).
- **`numSteps`** = `param->getNumSteps()` — lets KM render discrete (switch/stepped) vs. continuous
  controls. KM treats the JUCE "continuous" sentinel as a fader.
- **`label`** = `param->getLabel()` (unit string, e.g. `Hz`, `dB`, may be empty).

Appended → the reused `PList`-style parser must **tolerate trailing fields** (positional, additive).
`/custos/params` (ranged request) and `/custos/params/done` are unchanged.

## 6. Area 4 — Facade-size build ladder (build/packaging)

`kFacadeParamCount` (today a single `constexpr` in `Config.h`) becomes a **per-target compile
constant**. Ladder (locked): **1000 / 2000 / 3000 / 4000 / 5000 / 10000** — 1000-steps cover
practically all synths; one 10000 rung catches the rare outliers (OsTIrus ≈ 9035). Over-sizing costs
only `.gig` bytes, never audio; a too-small facade is the only real error, avoided by rounding up.

Each ladder rung is a **separate build target** from one source tree (a `custos_variant(SIZE)`
CMake function in a loop), differing only in:

- `kFacadeParamCount = SIZE`,
- a **unique VST3 component/processor UID** (else hosts and the VST DB collide),
- a **distinct plugin name** (e.g. `Custos 2000`) and output `.vst3`.

(JUCE exports one processor per target, so this is N artifacts, not one module with N classes.)
KM publishes all rungs to GP and, per synth, picks the smallest rung that fits — verified live via
`facadeCap` (§3). Resident 1:1 slots take the tightest rung; flexible swap-slots take a rung with
headroom for their largest possible occupant.

## 7. Version & back-compat

- Bump `kProtoVersion` → **2** in `OscContract.h`.
- **KM migration (coordination note):** v1 KM checks `protoVer == 1` and would now *reject* Custos.
  KM must relax to `protoVer >= 1` and gate the new verbs/fields on `>= 2`. This is the KM-side
  consumption task; it also picks up the already-merged `brand`/window additions in the same pass.
- All Custos→KM additions are appended fields; all KM→Custos additions are new addresses. A v1 KM
  that ignores unknown addresses and trailing fields keeps working against a v2 Custos (minus the
  new features). No behavioural change to load/clear/params/mode/volume/favourites/window.

## 8. Testing

- **OscContract:** builders emit the new arg counts/types (`here` +facadeCap, `loaded` +innerTotal,
  `param` +3, `midi/route` in 16 / feedback 17); `kProtoVersion == 2`.
- **StateCodec:** v3 round-trips the 16-byte map byte-exact; v1/v2 blobs parse with identity map;
  truncated v3 rejected.
- **MIDI apply:** unit-test the buffer rewrite — drop (`0`), remap (e.g. 8→1), identity, and
  non-channel-message pass-through; verify no allocation on the audio thread.
- **Param dump:** a stub inner with discrete + continuous + labelled params dumps correct
  `defaultVal/numSteps/label`.
- **Ladder:** a small-`SIZE` variant reports its `facadeCap`; loading an inner with
  `innerTotal > facadeCap` reports the overflow and clamps `boundCount`.
- **E2E (harness):** drive one running instance over OSC — set a route map, query it back, confirm
  the feedback matches; confirm `here`/`loaded` carry the new fields.

## 9. Open questions (defer to implementation / KM)

- **Matrix UI home:** KM panel vs. Custos editor — same "author in KM vs. call up the synth UI"
  decision as Ficta. Does not affect this wire contract.
- **Per-note vs. per-channel only:** v2 routes channel-voice messages by channel. Splits/zones
  (key-range routing) are explicitly out of scope for v2.
- **Query breadth:** whether `/custos/hello` should also echo the route map, or `midi/query` stays
  the only path (current design: separate query, to keep `here` lean).
