# Custos External-Control Contract — v1

Authoritative wire contract for driving Custos from outside (Kapellmeister = master).
Design rationale: `docs/superpowers/specs/2026-07-04-custos-external-control-contract-design.md`.
**`protoVer = 1`.** Anything not listed here is not part of the contract yet.

Custos is a VST3 instrument that hosts an arbitrary inner synth and mirrors its parameters 1:1 onto a
fixed facade. **All meta control (load, mode, volume, favorites, window, status, identity) is OSC/MIDI
— never facade parameters.**

---

## 1. Transport & addressing

- **Inbound (KM → Custos):** UDP `127.0.0.1:BASE+N`, `BASE = 9100`, `N ∈ 0..15` (up to 16 instances).
- **Outbound (Custos → KM hub):** UDP `127.0.0.1:8000`.
- **Demux:** every Custos→KM message carries **`N` as its first argument**.
- **Identity `N`** is an opaque integer assigned by KM. It only determines the OSC port (`BASE+N`); it
  carries no meaning inside Custos. KM maps `N ↔ channel/layer/role` in its own model.

### Identity injection (MIDI, host-derived)
GP-Script injects `N` into each Custos block as an **adjacent CC-pair** on the block's MIDI input:

```
CC #118 = 124   (arm marker, 0x7C)
CC #119 = N     (immediately following; N = 0..15)
```

- Block-targeted: GP delivers the pair to exactly the intended Custos block (channel-agnostic).
- Custos consumes the pair (never forwards it to the inner synth), binds `BASE+N`, and announces
  `/custos/here`.
- **Re-injected on every GP boot** — Custos persists nothing about its identity. Before the first
  injection Custos has no OSC-in.

---

## 2. KM → Custos  (send to `127.0.0.1:BASE+N`)

| Address | Args | Effect |
|---|---|---|
| `/custos/hello` | — | → replies `/custos/here` |
| `/custos/load` | `path:string` | load/replace the inner synth |
| `/custos/clear` | — | unload → silent facade |
| `/custos/params` | `start:int, count:int` | dump bound params in `[start, start+count)` |
| `/custos/mode` | `mode:string` (`replace`\|`resident`) | set persisted mode (takes effect on next reload) |
| `/custos/volume` | `gainDb:float` | live trim override |
| `/custos/favorites/begin` | — | start a favorites push |
| `/custos/favorite` | `idx:int, name:string, path:string, favOrder:int, gainDb:float` | one favorites entry |
| `/custos/favorites/end` | `count:int` | commit favorites (Custos writes its config) |
| `/custos/window` | `target:string` (`custos`\|`plugin`\|`both`\|`hide`) | window visibility *(details TBD in F3/F6 spec)* |
| `/custos/window/rect` | `x,y,w,h:int, movable:int` | window geometry *(details TBD in F3/F6 spec)* |

---

## 3. Custos → KM  (send to `127.0.0.1:8000`; first arg always `N`)

| Address | Args | Meaning |
|---|---|---|
| `/custos/here` | `N, protoVer:int, mode:string, inner:string, boundCount:int, port:int` | hello reply / boot announce |
| `/custos/ack` | `N, text:string` | command result (see strings below) |
| `/custos/param` | `N, idx:int, val:float, name:string` | one dumped param |
| `/custos/params/done` | `N, start:int, count:int` | end of a dump range |
| `/custos/loaded` | `N, path:string` | inner changed (by OSC **or** UI); empty path = cleared |

**Ack strings:** `loaded <path> count=<n>` · `cleared` · `mode <m> (applies after reload)` ·
`error <msg>`.

---

## 4. Semantics KM must know

- **Param dump is ranged and bound-only.** Only the `0..boundCount` mirrored params are dumped (not the
  full 5000 facade). Page with `start/count`; re-request ranges if UDP drops. Line shape matches the GP
  `PList` harness so the same parser works.
- **Facade index is stable, not semantically stable.** `custos_5` maps to inner param 5 of *whatever*
  synth is loaded → after a swap it points at a different parameter. **KM must re-bind macros on
  `/custos/loaded`.**
- **Mode is a reload-time setting.** The host caches the param view at load; `resident` (effective
  count) only takes effect on the next plugin instantiation. `replace` (fixed 5000) is the default and
  the practical mode for glitch-prone hosts like GP.
- **A runtime swap is invisible in the host's param list** (the host caches it). **Verify swaps by the
  `/custos/ack` count, not by the GP `PList` harness.**
- **Volume is a dB trim** applied inside Custos; `/custos/volume` overrides the per-synth default that
  arrived via the favorites push.
- **Custos loads a synth on a manual UI pick too**, and reports it via `/custos/loaded` — KM stays in
  sync either way.

---

## 5. Responsibilities

- **Custos:** everything in §2/§3, the CC-pair consumption, `BASE+N` bind, persistence (per-instance
  VST state; machine-level favorites/volume config).
- **Kapellmeister:** assigns `N` per role; addresses `BASE+N`; keeps the `N ↔ channel/role` map; pushes
  favorites/volumes; re-binds macros on `/custos/loaded`; checks `protoVer`.
- **GP-Script:** injects the CC-pair (`#118=124` → `#119=N`) into each Custos block, block-targeted, on
  boot and at placement. *(Assumption to confirm on the GP side: block-targeted CC injection — normal
  MIDI path, guaranteed delivery, unlike SysEx.)*
