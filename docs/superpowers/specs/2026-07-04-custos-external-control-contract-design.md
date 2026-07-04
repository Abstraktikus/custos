# Custos External-Control Contract — Instance Addressing + OSC/MIDI Surface (Design)

**Date:** 2026-07-04
**Status:** Approved (brainstorming complete, ready for planning)
**Scope:** The **contract** by which Kapellmeister (KM) drives multiple Custos instances from
outside — instance addressing, the identity model, and the full OSC verb surface. This is
**Phase A** of the Custos-2.0 roadmap. It does **not** implement the features; each feature gets its
own spec + plan in **Phase C**. Its purpose is to freeze a versioned contract so KM can develop in
parallel (**Phase B**).

Builds on M1–M3 (facade + 1:1 mirror + `SynthLoader`/`InnerBinding` + safe runtime swap via OSC +
self-contained persistence, all on `master`).

---

## 1. Guiding invariant — "Meta = OSC"

The facade stays a **pure 1:1 mirror** of the inner synth's parameters. **Every Custos-level meta
concern** — load/clear, mode, volume, favorites, window, status, identity — travels over the
**OSC/MIDI control channel, never over facade parameters.** No reserved facade slots, no param
overloading. This keeps the facade honest and the contract in one place.

Corollary from the M3 E2E: GP caches facade param info and never re-queries live, so a runtime swap
is invisible via the host's param view. That is a **host quirk that applies to any plugin**, handled
by KM orchestration (change → GP restart, with warning), **not** by Custos code. Custos never calls
`updateHostDisplay`. Swaps are verified by OSC ack/count, not by the host param list.

---

## 2. Instance addressing — the core

Multiple Custos instances run inside one GP process. KM must reach a **specific** instance (load a
synth into the right channel/layer, set its volume, etc.). A VST3 plugin cannot know its host slot.
An in-band identity injection was explored (MIDI CC-pair, then a reserved facade param) but proved
either unreliable in practice or an unwanted exception to "Meta = OSC". Since the rig's slots never
change, identity is set **once, by hand** — the simplest bulletproof option.

### 2.1 Identity is an operator-set, persisted port offset
- **`N` is set by the operator in the Custos UI** and **persisted in the instance's own VST state**
  (it lives in the gig). It is a **one-time** assignment per instance (slots don't move). The VST state
  is the only per-instance store — two identical Custos blocks can't be told apart by a shared file.
- **`N` is opaque to Custos** — no musical meaning. Its *only* effect: **Custos binds its OSC receiver
  to `BASE + N`** (`BASE = 9100`; `N ∈ 1..15`, 1-based; `base+0` = port 9100 unused). On load Custos
  reads `N` from its state, binds `BASE+N`, and proactively announces `/custos/here` (§3.3) so KM learns
  it is alive without polling. **Unset → no OSC-in** (the UI shows "unassigned").
- KM maps `N ↔ channel/layer/role` in **its own** model; the operator aligns each Custos's `N` to match.
  Custos stays logic-free about it.

### 2.2 No identity injection
There is **no** MIDI/param identity handshake, and **GP-Script has no identity role**. Nothing is
injected on boot; nothing needs re-arming. Custos simply restores its operator-set `N` from state.

### 2.3 Standalone (no KM / no GP-Script)
No injection → no OSC address, and none is needed: the user drives Custos through its own UI
(favorites picker, §5.4). OSC is purely the KM-master path.

---

## 3. OSC contract

### 3.1 Transport
- **Inbound (KM → Custos):** UDP `127.0.0.1:BASE+N`.
- **Outbound (Custos → KM hub):** UDP `CUSTOS_ACK_HOST:CUSTOS_ACK_PORT` (default `127.0.0.1:8000`,
  compile-defs as in M3).
- **Demux:** all instances reply to the one hub port, so **every Custos→KM message carries `N` as its
  first argument**. (New vs M3, whose ack had no `N`.)
- **Versioning:** `/custos/here` reports `protoVer` (starts at **1**); KM checks compatibility.

### 3.2 KM → Custos (on `BASE+N`)
| Address | Args | Effect |
|---|---|---|
| `/custos/hello` | — | liveness/status → replies `/custos/here` |
| `/custos/load` | `path:string` | load/replace inner (safe swap, M3) |
| `/custos/clear` | — | unload inner → silent facade |
| `/custos/params` | `start:int, count:int` | **F1** ranged dump of the **bound** params (§5.1) |
| `/custos/mode` | `mode:string` ∈ {`replace`,`resident`} | **F2** set persisted mode flag (§5.2) |
| `/custos/volume` | `gainDb:float` | **F5** live trim override (§5.3) |
| `/custos/favorites/begin` | — | **F4** start a favorites push (§5.4) |
| `/custos/favorite` | `idx:int, name:string, path:string, favOrder:int, gainDb:float` | one favorites entry |
| `/custos/favorites/end` | `count:int` | commit favorites → write config |
| `/custos/window` | `target:string` ∈ {`custos`,`plugin`,`both`,`hide`} | **F3** (own sub-spec) |
| `/custos/window/rect` | `x,y,w,h:int, movable:int` | **F6** (own sub-spec) |

### 3.3 Custos → KM (hub, first arg `N`)
| Address | Args | Meaning |
|---|---|---|
| `/custos/here` | `N, protoVer:int, mode:string, inner:string, boundCount:int, port:int` | hello reply / boot announce (liveness + contract version + status) |
| `/custos/ack` | `N, text:string` | command ack: `"loaded <path> count=<n>"` \| `"cleared"` \| `"error <msg>"` \| `"mode <m> (applies after reload)"` |
| `/custos/param` | `N, idx:int, val:float, name:string` | one dumped param (F1 stream) |
| `/custos/params/done` | `N, start:int, count:int` | end of a dump range; `count` = params **actually sent** (clamped to `boundCount`); `start` echoes the request |
| `/custos/loaded` | `N, path:string, boundCount:int` | **event** — inner changed by OSC **or** UI; empty path = cleared (`boundCount` 0). Carries `boundCount` so KM dumps immediately (no `hello` round-trip). Keeps KM's model in sync. |

---

## 4. Persistence split

- **Per-instance** (in the VST state, lives in the gig): loaded synth **path** + inner **state chunk**
  + **mode** flag + **identity `N`**. State format **`CUS3`** = `CUS1` (magic + ver + path + inner chunk)
  **+ 1 byte mode** (0=replace default, 1=resident) **+ 1 byte `N`** (0 = unassigned). `CUS1`/`CUS2`
  blobs still parse (missing fields default: mode→replace, `N`→unassigned). Different per instance.
- **Machine-level** (one Custos config file, shared by all instances): the **favorites list** +
  **per-synth volume defaults**. These are identical machine-wide, so they are **not** duplicated into
  every instance's VST state. Written on `/custos/favorites/end`, read on boot. Location: a
  user-writable app-data path (e.g. `userApplicationDataDirectory/Custos/favorites.json`), overridable.
  **Push-once/shared:** KM pushes to any one reachable instance; that instance writes the shared config
  and reflects it immediately, other instances read it on boot / when their picker opens — no
  per-instance fan-out.

---

## 5. Feature surfaces (contract-level only; each feature = its own Phase-C spec)

### 5.1 F1 — Param dump (ranged, bound-only)
`/custos/params <start> <count>` streams `/custos/param` for **only the bound** params (0..boundCount,
not 5000) and ends with `/custos/params/done`. Line shape mirrors the GP `PList` harness so KM reuses
its parser. Ranged so KM can page and re-request UDP drops. Custos stays dumb — it emits its current
params; the macro-binding architecture (ctrlmap, batch-update when a `*.ctrlmap.txt` exists) is 100%
KM-side. **Note for KM:** facade index is stable but **not** semantically stable across synth swaps —
`custos_5` points at a *different* parameter after a swap → KM must re-bind macros on inner change. The
dump **clamps** the requested range to `[0, boundCount)`; `/custos/params/done` reports the **actual**
count sent (not an echo of the request) so KM can verify completeness and detect UDP drops.

### 5.2 F2 — Mode (Joker/replace ↔ Resident)
Default = **replace** (fixed 5000 facade; the workaround that lets a swap happen at all in
glitch-prone hosts). `resident` = report the effective inner count. Per the E2E, GP reads the count
once at load → **mode is a persisted flag that only takes effect on the next plugin instantiation**;
the ack says so (`"mode <m> (applies after reload)"`). UI also offers the toggle. Resident's payoff
needs a host that re-reads on reload; for GP, replace stays the practical mode.

### 5.3 F5 — Uniform volume trim
Custos applies a **gain** (dB→linear) in `processBlock` (currently pure passthrough). On load it
applies the loaded synth's **config default** (§4). `/custos/volume <gainDb>` is a **transient live
override** of the current trim. This gives KM a uniform volume regardless of where the synth's "real"
volume sits. Meta = OSC: the trim is **not** a facade param.

### 5.4 F4 — Favorites + UI
KM pushes the favorites list over OSC (§3.2); Custos persists it to the machine config (§4) and shows
it in its editor as a ranked picker (by `favOrder`). **Custos loads a synth only on a manual UI pick**
— before that, pure external-control focus, no eager loading. A UI pick emits `/custos/loaded` (§3.3)
so KM stays in sync. No VstDatabase parser in Custos (data arrives via OSC). The favorites store is
machine-level and shared (§4): one push reaches all instances — no per-instance fan-out.

### 5.5 F3 / F6 — Window control
Verbs reserved (§3.2): show Custos-GUI / plugin-GUI / both / hide, and pixel-exact rect + movable
flag. The **behavioural details** (coordinate space, DPI, multi-monitor, whether a non-resizable inner
editor is scaled/clipped/positioned) are deferred to a **dedicated F3/F6 sub-spec** — out of scope for
this contract beyond reserving the surface.

---

## 6. UI addition (this contract)
The Custos editor has a small **field to set the identity `N`** (1..15) and shows the bound port (e.g.
`id 3 · :9103`). The operator assigns `N` here once; it persists in the VST state. Shows "unassigned"
until set. Doubles as a troubleshooting aid (which instance is which at a glance). On bind failure
(`N` collision — another instance already holds `BASE+N`) it shows a **visible warning** ("port in use
— pick another `N`"); Custos does **not** auto-fall-back to another port.

---

## 7. Error handling
- Unknown OSC address → `/custos/ack <N> "error unknown <addr>"`, no crash.
- Malformed args → ignored with an error ack.
- **`N` collision / port bind failure** → logged **+ a visible UI warning** ("port in use — pick
  another N"); no OSC-in, **no auto-fallback** (that would break deterministic addressing), no crash.
  `/custos/here` is then absent → KM sees no reply on `BASE+N` and surfaces "expected N not answering".
- **No heartbeat (v1):** Custos announces `/custos/here` on bind and answers `/custos/hello`; it sends
  no keepalive. KM re-probes; a dead/absent instance is detected on the next probe.
- Load failure keeps the current inner (M3).

---

## 8. GP-side requirements (cross-project handoff — for the GP session, not Custos code)
- **None.** Identity is set manually in the Custos UI; **GP-Script has no identity role**.
- History (2026-07-04): an earlier draft had GP inject a block-targeted CC-pair to carry `N`. GP
  confirmed block-targeted `SendNow` is feasible, but per-instance CC injection proved unreliable in
  practice, so the approach was dropped in favour of manual UI assignment. **The GP session should stand
  down on any Custos identity-injection work.**

---

## 9. Testing strategy
- **Unit (hermetic):** extend `parseCommand` for the new verbs; `StateCodec` `CUS3` round-trip (incl.
  `N`) + `CUS1`/`CUS2` back-compat; config read/write round-trip; volume gain math. Unit tests bind no
  UDP port.
- **E2E (autonomous, driven from outside a live GP):** direct OSC is already proven (M3). New to verify:
  setting `N` in the UI binds `BASE+N` and survives a gig reload; two instances with different `N` → two
  distinct ports, each independently addressable; dump paging; `/custos/loaded` on a UI pick. Verify
  swaps by ack/count (not the host param list).

---

## 10. Decomposition — Phase C order (each its own spec + plan)
1. **Addressing core** — UI field to set `N` + persist `N` in state + `BASE+N` bind + N-tagged
   replies + `/custos/here` (versioned) + `/custos/hello`. *Unblocks all KM addressing.* (StateCodec
   version bump decided in this feature's plan.)
2. **F1** params dump (ranged).
3. **F2** mode (persist mode flag + `/custos/mode`).
4. **F5** volume (gain + config defaults + `/custos/volume`) — depends on the config file.
5. **F4** favorites (config file + push + UI picker + `/custos/loaded` on UI pick).
6. **F3/F6** window control — its own sub-spec first.

---

## 11. Open items / assumptions
- Identity is operator-set in the Custos UI (no GP injection) — see §2 / §8.
- `protoVer = 1`; `BASE = 9100`, `N ∈ 1..15` (1-based; `base+0`=9100 unused) → ports `9101..9115`.
- Config file exact location/format finalised in the F4 spec (default: app-data JSON).
- F3/F6 window semantics deferred to their own sub-spec.
