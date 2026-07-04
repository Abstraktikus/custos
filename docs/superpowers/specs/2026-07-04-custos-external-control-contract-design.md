# Custos External-Control Contract — Instance Addressing + OSC/MIDI Surface (Design)

**Date:** 2026-07-04
**Status:** Approved (brainstorming complete, ready for planning)
**Scope:** The **contract** by which Kapellmeister (KM) drives multiple Custos instances from
outside — instance addressing, the MIDI identity-injection, and the full OSC verb surface. This is
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
synth into the right channel/layer, set its volume, etc.). A VST3 plugin cannot know its host slot,
so we inject an identity **in-band over MIDI** (which GP delivers to plugin blocks reliably, unlike
SysEx), and derive the OSC address from it.

### 2.1 Identity is an opaque port offset
- KM assigns each Custos instance a small integer **`N`** (its role/slot index in KM's model;
  `N ∈ 0..15`, i.e. up to 16 instances — the MIDI-channel ceiling).
- **`N` is opaque to Custos** — it carries no musical meaning inside the plugin. Its *only* effect:
  **Custos binds its OSC receiver to `BASE + N`** (`BASE = 9100` → ports `9100..9115`).
- KM maps `N ↔ channel/layer/role` in **its own** model. Custos stays logic-free about it.

### 2.2 Identity injection — CC-pair over MIDI
GP-Script injects the identity into each Custos block as an adjacent **two-CC handshake** (undefined
MIDI CCs, low false-trigger risk):

| Message | Meaning |
|---|---|
| `CC #118 = 124` (`0x7C`) | **arm** marker |
| `CC #119 = N` (immediately following) | **payload** — the instance's `N` |

- Custos scans its incoming `MidiBuffer` in `processBlock`. On `CC#118==124` it **arms**; if the very
  next MIDI event is `CC#119`, it adopts `N = value`, **consumes both** (does **not** forward them to
  the inner synth), and defers the `(re)bind BASE+N` to the **message thread**. Any other event while
  armed → **disarm** and pass through untouched.
- **Channel-agnostic:** GP delivers the CC-pair to exactly the intended block (block-targeted
  injection, or a channel only that block receives). Custos accepts it on any channel — the block
  routing is the addressing, not the MIDI channel.
- **Host-derived, not persisted:** GP-Script re-injects on every GP boot (and at placement). Custos
  persists **nothing** about its identity — the identity is always the current injection. Before the
  first injection Custos has **no OSC-in** (no bound port); the injection is the trigger. On binding
  `BASE+N`, Custos proactively announces `/custos/here` (§3.3) so KM learns it is alive without polling.

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
| `/custos/params/done` | `N, start:int, count:int` | end of a dump range |
| `/custos/loaded` | `N, path:string` | **event** — inner changed by OSC **or** UI; empty path = cleared. Keeps KM's model in sync. |

---

## 4. Persistence split

- **Per-instance** (in the VST state, lives in the gig): loaded synth **path** + inner **state chunk**
  + **mode** flag. State format **`CUS2`** = `CUS1` (magic + ver + path + inner chunk) **+ 1 byte mode**
  (0=replace default, 1=resident). `CUS1` blobs still parse (mode→replace). Different per instance.
- **Machine-level** (one Custos config file, shared by all instances): the **favorites list** +
  **per-synth volume defaults**. These are identical machine-wide, so they are **not** duplicated into
  every instance's VST state. Written on `/custos/favorites/end`, read on boot. Location: a
  user-writable app-data path (e.g. `userApplicationDataDirectory/Custos/favorites.json`), overridable.

---

## 5. Feature surfaces (contract-level only; each feature = its own Phase-C spec)

### 5.1 F1 — Param dump (ranged, bound-only)
`/custos/params <start> <count>` streams `/custos/param` for **only the bound** params (0..boundCount,
not 5000) and ends with `/custos/params/done`. Line shape mirrors the GP `PList` harness so KM reuses
its parser. Ranged so KM can page and re-request UDP drops. Custos stays dumb — it emits its current
params; the macro-binding architecture (ctrlmap, batch-update when a `*.ctrlmap.txt` exists) is 100%
KM-side. **Note for KM:** facade index is stable but **not** semantically stable across synth swaps —
`custos_5` points at a *different* parameter after a swap → KM must re-bind macros on inner change.

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
so KM stays in sync. No VstDatabase parser in Custos (data arrives via OSC).

### 5.5 F3 / F6 — Window control
Verbs reserved (§3.2): show Custos-GUI / plugin-GUI / both / hide, and pixel-exact rect + movable
flag. The **behavioural details** (coordinate space, DPI, multi-monitor, whether a non-resizable inner
editor is scaled/clipped/positioned) are deferred to a **dedicated F3/F6 sub-spec** — out of scope for
this contract beyond reserving the surface.

---

## 6. UI addition (this contract)
The Custos editor shows, **discreetly**, the assigned identity `N` and the bound port (e.g.
`id 3 · :9103`) — a troubleshooting aid so a human can see which instance is which at a glance. Shows
"unassigned" before the first injection.

---

## 7. Error handling
- Unknown OSC address → `/custos/ack <N> "error unknown <addr>"`, no crash.
- Malformed args → ignored with an error ack.
- Port bind failure (range exhausted / clash) → logged, no OSC-in, no crash; reported in `/custos/here`
  is then absent (KM sees no reply on that port).
- CC-pair: only an adjacent `#118==124` → `#119` pair adopts `N`; anything else disarms and passes
  through. The pair is never forwarded to the inner synth.
- Load failure keeps the current inner (M3).

---

## 8. GP-side requirements (cross-project handoff — for the GP session, not Custos code)
- GP-Script must inject `CC#118=124` + `CC#119=N` (adjacent) into each Custos block, **block-targeted**,
  on GP boot and at placement. KM assigns `N` per role and coordinates the mapping.
- **Assumption to confirm:** GP-Script can inject block-targeted CCs to a specific plugin block. Low
  risk (normal MIDI path; unlike SysEx this is guaranteed delivery), but confirm on the GP side before
  Phase-C addressing work is E2E'd. Deliver as a copy-paste requirement to the GP session.

---

## 9. Testing strategy
- **Unit (hermetic):** extend `parseCommand` for the new verbs; a pure CC-pair parser
  (arm/adopt/disarm/consume/passthrough) tested against synthetic `MidiBuffer`s; `StateCodec` `CUS2`
  round-trip + `CUS1` back-compat; config read/write round-trip; volume gain math. Unit tests bind no
  UDP port and inject no MIDI identity.
- **E2E (autonomous, driven from outside a live GP):** direct OSC is already proven (M3). New to spike:
  CC-pair injection actually adopted + `BASE+N` (re)bind + the pair filtered from passthrough; two
  instances → two distinct ports, each independently addressable; dump paging; `/custos/loaded` on a UI
  pick. Verify swaps by ack/count (not the host param list).

---

## 10. Decomposition — Phase C order (each its own spec + plan)
1. **Addressing core** — CC-pair injection + `BASE+N` bind + N-tagged replies + `/custos/here`
   (versioned) + `/custos/hello`. *Unblocks all KM addressing.*
2. **F1** params dump (ranged).
3. **F2** mode (`CUS2` + `/custos/mode`).
4. **F5** volume (gain + config defaults + `/custos/volume`) — depends on the config file.
5. **F4** favorites (config file + push + UI picker + `/custos/loaded` on UI pick).
6. **F3/F6** window control — its own sub-spec first.

---

## 11. Open items / assumptions
- GP-side block-targeted CC injection — confirm with the GP session (§8). Low risk.
- `protoVer = 1`; `BASE = 9100`, up to 16 instances (`N ∈ 0..15`, MIDI-channel ceiling) → ports `9100..9115`.
- Config file exact location/format finalised in the F4 spec (default: app-data JSON).
- F3/F6 window semantics deferred to their own sub-spec.
