# Custos External-Control Contract — v3

Authoritative wire contract for driving Custos from outside (Kapellmeister = master).
Design rationale: `docs/superpowers/specs/2026-07-04-custos-external-control-contract-design.md`.
**`protoVer = 3`.** Anything not listed here is not part of the contract yet.

Custos is a VST3 instrument that hosts an arbitrary inner synth and mirrors its parameters 1:1 onto a
fixed facade. **All meta control (load, mode, volume, audio-fold, favorites, window, status, identity) is OSC/MIDI
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
| `/custos/mainlr` | `on:int` (`0`\|`1`) | audio-fold: `1` sums all inner outputs onto stereo Out 1; `0` maps inner pairs across the 5 stereo out buses (local editor mirror; persisted in state v4) |
| `/custos/mainlr/query` | — | → replies `/custos/mainlr` with the current fold flag (no state change) |
| `/custos/favorites/begin` | — | start a favorites push |
| `/custos/favorite` | `idx:int, name:string, path:string, favOrder:int, gainDb:float, brand:string, slots:int, controlType:string, paramDown:int, paramUp:int` | one favorites entry. `brand` optional 6th arg (UI brand filter). **`slots` optional 7th arg = the synth's param count** — Custos skips favourites whose `slots > facadeCap` when browsing/in the picker (a Custos 1000 won't browse a 4000-param synth). `0`/omitted = unknown → allowed. Source it from the VstDatabase or learn it from `/custos/loaded`'s `innerTotal`. **`controlType` optional 8th arg** (`PARAM`\|`PC`\|`PRESET`\|`NONE`; default `PRESET`) selects the `/custos/patch/next|prev` method for this instrument — anything other than `PARAM`/`PC` falls back to the preset store. **`paramDown`/`paramUp` optional 9th/10th args** = inner-synth param indices (mirrored 1:1 with the facade in `[0, boundCount)`) momentarily injected (1.0 then release) for `PARAM`-method patch-down/patch-up; ignored for other `controlType`s |
| `/custos/favorites/end` | `count:int` | commit favorites (Custos writes its config) |
| `/custos/window` | `mode:string` (`show`\|`titled`\|`hide`) | open/close the **inner-synth** window (never the Custos panel). `show` = borderless at natural size; `titled` = native title bar + close; `hide` = close. Once open, the window **persists across loads** and re-shows the newly-loaded synth automatically (so browsing displays each instrument) |
| `/custos/window/rect` | `x,y,w,h:int, movable:int [, clamp:int]` | place the synth window at a **physical-pixel** rect (DPI-mapped); `movable`=body-draggable; `clamp` (optional, default 0) constrains it to the monitor work area for config-phase reachable borders. Shows the window if hidden |
| `/custos/midi/route` | `t1..t16:int` | set the MIDI channel-routing map: for each **input** channel 1..16 (positional, `t1`=input ch 1), `0` = drop that channel's messages, `1..16` = remap to that **output** channel. Default (unset) is identity (`t_i = i`). Applies live; Custos replies with `/custos/midi/route` echoing the applied map |
| `/custos/midi/query` | — | → replies `/custos/midi/route` with the current map (no state change) |
| `/custos/instrument/next` | `scope:int` (optional; `0`\|`1`) | advance the favourites cursor +1 (wrap), skipping entries that don't fit `scope`. Reports only the NAME via `/custos/browsing`; **does NOT load** — the synth loads 400 ms after browsing stops. `scope` omitted/`0` = favourites only (`favOrder >= 1`); `1` = all instruments in the machine config |
| `/custos/instrument/prev` | `scope:int` (optional; `0`\|`1`) | cursor −1 (wrap), same `scope` rule. Same name-report + deferred load |
| `/custos/instrument/set` | `i:int` | jump the cursor to index `i` (clamped). Same name-report + deferred load. No `scope` arg — `i` is an absolute index into the full instrument list |
| `/custos/instrument/load` | `name:string` | load an instrument by its **name** (looked up against the machine config; Custos resolves name→path internally — the caller never needs the path). No match → `/custos/ack error unknown instrument <name>` (not mirrored to GP as browsing/loaded); success is conveyed by `/custos/loaded` as usual |
| `/custos/patch/next` | — | step the **current instrument's patch axis** +1, dispatched by its favourite entry's `controlType` (see the patch-axis semantics below). `PARAM`/`PC` report via `/custos/patch/stepped`; anything else falls back to the preset store (`presetNext`) and reports via `/custos/preset/browsing`+`/custos/preset/loaded` instead — **not** both |
| `/custos/patch/prev` | — | step the patch axis −1. Same dispatch/fallback/report rules |

---

## 3. Custos → KM  (send to `127.0.0.1:8000`; first arg always `N`)

> **GP mirror:** `/custos/browsing`, `/custos/loaded`, `/custos/here`, `/custos/patch/stepped`, and
> **error-only** `/custos/ack` (text starting `error`) are additionally sent to **GP's OSC-in
> `127.0.0.1:54344`** (not just the KM hub), so the **GP-Script can drive the Voice-Selector, the patch
> axis, _and_ direct instrument load autonomously** without KM in the loop: `loaded` = success/ready,
> `patch/stepped` = patch-axis feedback, error-`ack` = load failure, `here` = liveness/discovery.
> Success acks (`loaded … count=…`, `cleared`, `mode …`) are **not** mirrored (success is already
> conveyed by `/custos/loaded`), and every other reply — notably the `/custos/param` dump stream —
> goes to `:8000` only, to avoid flooding GP's OSC-in.

| Address | Args | Meaning |
|---|---|---|
| `/custos/here` | `N, protoVer:int, mode:string, inner:string, boundCount:int, port:int, facadeCap:int` | hello reply / boot announce; `facadeCap` is this build's facade size (see §4 facade ladder) |
| `/custos/ack` | `N, text:string` | command result (see strings below) |
| `/custos/param` | `N, idx:int, val:float, name:string, defaultVal:float, numSteps:int, label:string` | one dumped param; `defaultVal`/`numSteps`/`label` mirror the inner param's metadata (`numSteps` 0/1 = continuous) |
| `/custos/params/done` | `N, start:int, count:int` | end of a dump range; `count` = params **actually sent** (clamped to `boundCount`), `start` echoes the request |
| `/custos/loaded` | `N, path:string, boundCount:int, innerTotal:int` | inner changed (OSC **or** UI); empty path = cleared (`boundCount`/`innerTotal` 0). Carries `boundCount` so KM can dump immediately — no `hello` round-trip. `innerTotal` is the loaded synth's **full** param count, which may exceed `boundCount`/`facadeCap` (top params unbound/uncontrollable) |
| `/custos/window/rect` | `N, x,y,w,h:int, movable:int` | synth-window position feedback — emitted when the operator **drags** the window (on mouse-up) or when a rect is (re)applied. Physical px. Lets KM capture the operator-chosen geometry for its settings |
| `/custos/midi/route` | `N, t1..t16:int` | MIDI route feedback — the current input→output channel map; emitted when a `/custos/midi/route` command is applied and in reply to `/custos/midi/query` |
| `/custos/mainlr` | `N, on:int` | Main L/R fold feedback — the current flag (`1` = all inner outputs summed onto stereo Out 1, `0` = inner pairs mapped across the 5 buses). Emitted after an applied `/custos/mainlr` set and in reply to `/custos/mainlr/query` |
| `/custos/browsing` | `N, index:int, name:string, wrapped:int` | favourite-browse preview — the cursor's favourite NAME while flipping (`next`/`prev`/`set`). **Not loaded yet** — show the name; the actual load lands later as `/custos/loaded`. `wrapped=1` on the step that wrapped past an end of the list |
| `/custos/patch/stepped` | `N, controlType:string, detail:string` | patch-axis feedback — reports the method that ran for the just-processed `/custos/patch/next`\|`prev` (`PARAM`\|`PC`; the `PRESET` fallback reports via `/custos/preset/browsing`+`/custos/preset/loaded` instead, not this address). `detail` is best-effort: `"+"`/`"-"` for `PARAM` (which direction was injected), the resulting program number (as a string) for `PC` |

**Ack strings:** `loaded <path> count=<n>` · `cleared` · `mode <m> (applies after reload)` ·
`error <msg>`.

---

## 4. Semantics KM must know

- **Param dump is ranged and bound-only.** Only the `0..boundCount` mirrored params are dumped (not the
  full facade — see the facade-ladder note below for `facadeCap`). Page with `start/count`; re-request
  ranges if UDP drops. Line shape matches the GP `PList` harness so the same parser works.
- **Facade index is stable, not semantically stable.** `custos_5` maps to inner param 5 of *whatever*
  synth is loaded → after a swap it points at a different parameter. **KM must re-bind macros on
  `/custos/loaded`.**
- **Mode is a reload-time setting.** The host caches the param view at load; `resident` (effective
  count) only takes effect on the next plugin instantiation. `replace` (fixed `facadeCap`) is the
  default and the practical mode for glitch-prone hosts like GP.
- **A runtime swap is invisible in the host's param list** (the host caches it). **Verify swaps by the
  `/custos/ack` count, not by the GP `PList` harness.**
- **Volume is a dB trim** applied inside Custos; `/custos/volume` overrides the per-synth default that
  arrived via the favorites push.
- **Trim precedence:** on every load, Custos first applies the loaded instrument's own `gainDb` from the
  machine config (the favorites/instruments list). A subsequently **delivered** `/custos/volume` outranks
  that list value — it overwrites the live trim until the next load re-applies the list default. There is
  no persistent per-instrument override; a manual `/custos/volume` is a one-shot live nudge, not a config
  write.
- **Main L/R only is an audio-fold**: `/custos/mainlr 1` sums all inner outputs onto stereo Out 1; `0`
  maps inner pairs across the 5 stereo out buses. Persisted in Custos state v4; the local editor toggle mirrors
  the same flag. **Observable:** an applied set emits `/custos/mainlr <N, on>` feedback, and
  `/custos/mainlr/query` returns the current flag on demand. The editor toggle does **not** emit — KM
  re-queries to resync after an operator-driven UI flip.
- **Custos loads a synth on a manual UI pick too**, and reports it via `/custos/loaded` — KM stays in
  sync either way.
- **Favorites are machine-level and shared.** Push once to any one reachable instance (`favorites/begin
  … end`); Custos writes the shared config and every instance uses it (the receiver immediately, others
  on next boot / when their picker opens). **No per-instance fan-out.**
- **The machine config file is `instruments.json`** (`%APPDATA%/Custos/instruments.json`), superseding
  the old `favorites.json` name — it now holds every pushed instrument, not just favourites (see `scope`
  below). **One-time migration:** on boot, if `instruments.json` doesn't exist yet, Custos reads the
  legacy `favorites.json` instead so an upgraded install keeps working with no data loss. The migration
  is read-only at boot; the on-disk file only becomes `instruments.json` once something pushes via
  `favorites/begin … end` again (which always writes the new filename). Until then, repeated boots keep
  reading the legacy file — this is intentional (no silent rewrite of a file KM didn't touch).
- **`scope` splits the browse list into favourites vs. everything.** Each pushed entry's `favOrder`
  decides membership: `favOrder >= 1` = a favourite. `/custos/instrument/next|prev` with `scope` omitted
  or `0` cycles favourites only; `scope 1` cycles the full instrument list (favourites and non-favourites
  alike). `/custos/instrument/set` and `/custos/instrument/load` are unaffected by `scope` — they address
  the full list directly (by absolute index / by name).
- **Patch axis dispatch is per-instrument, via `controlType` on its favourite entry.** `/custos/patch/next|prev`
  looks up the currently-loaded instrument's `controlType` (`PARAM`\|`PC`\|`PRESET`\|`NONE`, default
  `PRESET`) and dispatches explicitly: `PARAM` momentarily injects the entry's `paramDown`/`paramUp`
  facade index (1.0 then release ~150 ms later); `PC` sends/advances a wrapped 0..127 program-change
  counter. **Anything else (`PRESET`, `NONE`, empty, unrecognized) falls back to the preset store**
  (`presetNext`/`presetPrev`) — the only implicit method, always available even for instruments with no
  `controlType` configured. PC is never inferred; it must be set explicitly.
- **No heartbeat.** Custos announces `/custos/here` on bind and answers `/custos/hello`, but sends no
  keepalive. Re-probing dead/absent instances is KM's job.
- **N collision:** if two instances share an `N`, the second fails to bind `BASE+N`, shows a **visible
  UI warning** ("port in use — pick another N"), and does **not** fall back to another port (that would
  break deterministic addressing). It stays uncontrollable until the operator fixes `N`; KM should
  surface "expected N not answering".
- **Persisted state is format v3.** It adds a 16-byte MIDI route map (one target channel per input
  channel, 0 = drop) after the v2 fields. Loading an older blob upgrades in place: v1 blobs get
  `identityN = 0` **and** an identity route; v2 blobs get an identity route. The route is written back
  on next save.
- **Custos ships as a facade-size ladder of build variants** — `1000 / 2000 / 3000 / 4000 / 5000 / 10000`
  facade params, each a separate binary. Every variant reports its own facade size as `facadeCap` in
  `/custos/here`. Combined with `innerTotal` in `/custos/loaded`, KM can detect when a loaded synth's
  real param count exceeds the running variant's `facadeCap` (top params silently unbound) and suggest a
  bigger variant.

---

## 5. Responsibilities

- **Custos:** everything in §2/§3; a UI field to set `N` (with a visible warning on bind failure / `N`
  collision); reads the operator-set `N` from its VST state and binds `BASE+N`; persistence (per-instance
  VST state incl. `N`; machine-level instrument config in `instruments.json`, migrated once from
  `favorites.json`).
- **Kapellmeister:** addresses `BASE+N`; keeps the `N ↔ channel/role` map (operator aligns each Custos's
  `N` to match); pushes favorites/volumes; re-binds macros on `/custos/loaded`; checks `protoVer`.
- **GP-Script:** **no identity role** — identity is set manually in the Custos UI.
