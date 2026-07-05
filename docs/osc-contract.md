# Custos External-Control Contract — v1

Authoritative wire contract for driving Custos from outside (Kapellmeister = master).
Design rationale: `docs/superpowers/specs/2026-07-04-custos-external-control-contract-design.md`.
**`protoVer = 1`.** Anything not listed here is not part of the contract yet.

Custos is a VST3 instrument that hosts an arbitrary inner synth and mirrors its parameters 1:1 onto a
fixed facade. **All meta control (load, mode, volume, favorites, window, status, identity) is OSC/MIDI
— never facade parameters.**

---

## 1. Transport & addressing

- **Inbound (KM → Custos):** UDP `127.0.0.1:BASE+N`, `BASE = 9100`, `N ∈ 1..15` (1-based; up to 15
  instances; `base+0` = port 9100 is intentionally unused).
- **Outbound (Custos → KM hub):** UDP `127.0.0.1:8000`.
- **Demux:** every Custos→KM message carries **`N` as its first argument**.
- **Identity `N` is set by the operator in the Custos UI** and **persisted in the instance's own VST
  state** (it lives in the gig). Slots never change, so it is a **one-time** assignment per instance —
  and the VST state is the only per-instance store (two identical Custos blocks can't be told apart by a
  shared file). On load Custos reads `N` from its state, binds `BASE+N`, and announces `/custos/here` so
  KM learns it is alive without polling. Unset → **no OSC-in** (the UI shows "unassigned"). `N` is
  otherwise opaque to Custos — just the port offset.
- **No identity injection.** There is no MIDI/param handshake; **GP-Script has no identity role**. KM
  addresses `BASE+N`; the operator aligns each Custos's `N` with the role/channel KM expects for that
  slot.

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
| `/custos/favorite` | `idx:int, name:string, path:string, favOrder:int, gainDb:float, brand:string` | one favorites entry (`brand` optional 6th arg — omit for none; used for the UI brand filter) |
| `/custos/favorites/end` | `count:int` | commit favorites (Custos writes its config) |
| `/custos/window` | `mode:string` (`show`\|`hide`) | show/hide the **inner-synth** window (borderless; never the Custos panel) |
| `/custos/window/rect` | `x,y,w,h:int, movable:int [, clamp:int]` | place the synth window at a **physical-pixel** rect (DPI-mapped); `movable`=body-draggable; `clamp` (optional, default 0) constrains it to the monitor work area for config-phase reachable borders. Shows the window if hidden |

---

## 3. Custos → KM  (send to `127.0.0.1:8000`; first arg always `N`)

| Address | Args | Meaning |
|---|---|---|
| `/custos/here` | `N, protoVer:int, mode:string, inner:string, boundCount:int, port:int` | hello reply / boot announce |
| `/custos/ack` | `N, text:string` | command result (see strings below) |
| `/custos/param` | `N, idx:int, val:float, name:string` | one dumped param |
| `/custos/params/done` | `N, start:int, count:int` | end of a dump range; `count` = params **actually sent** (clamped to `boundCount`), `start` echoes the request |
| `/custos/loaded` | `N, path:string, boundCount:int` | inner changed (OSC **or** UI); empty path = cleared (`boundCount` 0). Carries `boundCount` so KM can dump immediately — no `hello` round-trip |
| `/custos/window/rect` | `N, x,y,w,h:int, movable:int` | synth-window position feedback — emitted when the operator **drags** the window (on mouse-up) or when a rect is (re)applied. Physical px. Lets KM capture the operator-chosen geometry for its settings |

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
- **Favorites are machine-level and shared.** Push once to any one reachable instance (`favorites/begin
  … end`); Custos writes the shared config and every instance uses it (the receiver immediately, others
  on next boot / when their picker opens). **No per-instance fan-out.**
- **No heartbeat.** Custos announces `/custos/here` on bind and answers `/custos/hello`, but sends no
  keepalive. Re-probing dead/absent instances is KM's job.
- **N collision:** if two instances share an `N`, the second fails to bind `BASE+N`, shows a **visible
  UI warning** ("port in use — pick another N"), and does **not** fall back to another port (that would
  break deterministic addressing). It stays uncontrollable until the operator fixes `N`; KM should
  surface "expected N not answering".

---

## 5. Responsibilities

- **Custos:** everything in §2/§3; a UI field to set `N` (with a visible warning on bind failure / `N`
  collision); reads the operator-set `N` from its VST state and binds `BASE+N`; persistence (per-instance
  VST state incl. `N`; machine-level favorites/volume config).
- **Kapellmeister:** addresses `BASE+N`; keeps the `N ↔ channel/role` map (operator aligns each Custos's
  `N` to match); pushes favorites/volumes; re-binds macros on `/custos/loaded`; checks `protoVer`.
- **GP-Script:** **no identity role** — identity is set manually in the Custos UI.
