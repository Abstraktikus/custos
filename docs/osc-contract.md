# Custos External-Control Contract — v4

Authoritative wire contract for driving Custos from outside (Kapellmeister = master).
Design rationale: `docs/superpowers/specs/2026-07-04-custos-external-control-contract-design.md`.
**`protoVer = 4`.** Anything not listed here is not part of the contract yet.

Custos is a VST3 instrument that hosts an arbitrary inner synth and mirrors its parameters 1:1 onto a
fixed facade. **All meta control (load, mode, volume, audio-fold, favorites, window, status, identity, learn) is OSC/MIDI
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
| `/custos/learn/start` | `N:int` | open a Learn window: while open, operator parameter moves stream as `/custos/learn/moved`. Idempotent re-arm. Auto-closes after ~10 s. |
| `/custos/learn/stop` | `N:int` | close the Learn window (emits `/custos/learn/stopped`). |
| `/custos/favorites/begin` | — | start a favorites push |
| `/custos/favorite` | `idx:int, name:string, path:string, favOrder:int, gainDb:float, brand:string, slots:int, controlType:string, paramDown:int, paramUp:int, classId:string` | one favorites entry. `brand` optional 6th arg (UI brand filter). **`slots` optional 7th arg = the synth's param count** — Custos skips/refuses instruments whose `slots` exceed the facade by **more than 10%** (browse, picker, and every load path; a Custos 1000 won't take a 4000-param synth — that class froze GP on 2026-07-19). Overshoot **within 10%** is tolerated: the load proceeds with an ack `warning instrument oversized (slots N > facade M, K params unbound)` — the top K params are unbindable but harmless (live precedent: Jup-8000 V 3058 / Memory V 3168 in 3000 rungs). `0`/omitted = unknown → allowed. Source it from the VstDatabase or learn it from `/custos/loaded`'s `innerTotal`. **`controlType` optional 8th arg** (`PARAM`\|`PC`\|`PRESET`\|`NONE`; default `PRESET`) selects the `/custos/patch/next|prev` method for this instrument — anything other than `PARAM`/`PC` falls back to the preset store. **`paramDown`/`paramUp` optional 9th/10th args** = inner-synth param indices (mirrored 1:1 with the facade in `[0, boundCount)`) momentarily injected (1.0 then release) for `PARAM`-method patch-down/patch-up; ignored for other `controlType`s. **`classId` optional 11th arg** (v4) = the inner synth's stable VST3 key (mirrors what Custos emits on `/custos/loaded`); Custos persists it in `instruments.json` as a durable record but does not otherwise act on it — the round-trip lets the machine config carry the same stable key KM holds. Omitted/empty = unknown |
| `/custos/favorites/end` | `count:int` | commit favorites (Custos writes its config) |
| `/custos/window` | `mode:string` (`show`\|`titled`\|`hide`) | open/close the **inner-synth** window (never the Custos panel). `show` = borderless at natural size; `titled` = native title bar + close; `hide` = close. Once open, the window **persists across loads** and re-shows the newly-loaded synth automatically (so browsing displays each instrument) |
| `/custos/window/rect` | `x,y,w,h:int, movable:int [, clamp:int [, fit:int [, margin:int]]]` | place the synth window at a **desktop-logical px (DIP)** rect — the host may be DPI-unaware (GP is), so JUCE-logical coordinates ARE this unit either way; no physical-px mapping (amended 2026-07-19); `movable`=body-draggable; `clamp` (optional, default 0) constrains it to the monitor work area for config-phase reachable borders. **`fit`** (optional, default 0): when 1, treat `x,y,w,h` as an available **area** and fit the editor into it preserving aspect ratio, centred, leaving a **`margin`** (optional, logical px, default 0) frame on all sides — used for docking the window into a host UI region. A fit placement also keeps the window above the host UI. **How** is governed by `/custos/window/ontop`: by default (Mode A) it is forced always-on-top; once KM sends `/custos/window/ontop 0|1` (Mode B) the docked window instead follows KM's foreground state. `fit` and `clamp` are mutually exclusive (fit wins). Shows the window if hidden. **Not sticky across loads:** an instrument load/switch recreates the window at natural size with on-top per the on-top mode, so re-send `/custos/window/rect fit=1` after each `/custos/loaded` while docked |
| `/custos/window/ontop` | `state:int` | docked-window on-top strategy + KM heartbeat. `-1` = **hands off**, use always-on-top (Mode A, default — today's behaviour). `0`/`1` = **follow KM** (Mode B): the docked window is on top only while KM is foreground (`1`) and drops behind when it is not (`0`). KM sends on every focus change **and every 2 s while alive** — the message doubles as a heartbeat; **5 s** of silence drops the window to not-on-top (fail-safe: a crashed KM leaves it reachable) while staying in Mode B, so a restarted KM resumes control with its next send. No window handle crosses the wire; ownership is deliberately not used (cross-process owner input-queue attachment can freeze the host). Older KM builds never send this and stay in Mode A |
| `/custos/midi/route` | `t1..t16:int` | set the MIDI channel-routing map: for each **input** channel 1..16 (positional, `t1`=input ch 1), `0` = drop that channel's messages, `1..16` = remap to that **output** channel. Default (unset) is identity (`t_i = i`). Applies live; Custos replies with `/custos/midi/route` echoing the applied map (mirrored to KM `:8000` **and** GP `:54344` — see §3) |
| `/custos/midi/query` | — | → replies `/custos/midi/route` with the current map (no state change). Reply reaches KM `:8000` **and** GP `:54344`, so GP can populate its routing map on song-load without KM |
| `/custos/instrument/next` | `scope:int` (optional; `0`\|`1`) | advance the favourites cursor +1 (wrap), skipping entries that don't fit `scope`. Reports only the NAME via `/custos/browsing`; **does NOT load** — the synth loads 400 ms after browsing stops. `scope` omitted/`0` = favourites only (`favOrder >= 1`); `1` = all instruments in the machine config |
| `/custos/instrument/prev` | `scope:int` (optional; `0`\|`1`) | cursor −1 (wrap), same `scope` rule. Same name-report + deferred load |
| `/custos/instrument/set` | `i:int` | jump the cursor to index `i` (clamped). Same name-report + deferred load. No `scope` arg — `i` is an absolute index into the full instrument list |
| `/custos/instrument/load` | `name:string` | load an instrument by its **name** (looked up against the machine config; Custos resolves name→path internally — the caller never needs the path). No match → `/custos/ack error unknown instrument <name>` (not mirrored to GP as browsing/loaded); success is conveyed by `/custos/loaded` as usual |
| `/custos/patch/next` | — | step the **current instrument's patch axis** +1, dispatched by its favourite entry's `controlType` (see the patch-axis semantics below). `PARAM`/`PC` report via `/custos/patch/stepped`; anything else falls back to the preset store (`presetNext`) and reports via `/custos/preset/browsing`+`/custos/preset/loaded` instead — **not** both |
| `/custos/patch/prev` | — | step the patch axis −1. Same dispatch/fallback/report rules |
| `/custos/preset/next` | — | step the loaded synth's **preset** cursor +1 (wrap) over its Custos snapshot store; preview the NAME via `/custos/preset/browsing`, then debounced load |
| `/custos/preset/prev` | — | cursor −1 (wrap); same preview + deferred load |
| `/custos/preset/set` | `index:int` | load the preset at sorted `index` immediately; emits `/custos/preset/loaded` or `/custos/preset/error` |
| `/custos/preset/load` | `name:string` \| `index:int` | load a preset by name (or by sorted index); emits `loaded`/`error` |
| `/custos/preset/save` | `name:string` | save the loaded synth's current state as a named `.cuspreset`; emits `/custos/preset/saved` or `error` |
| `/custos/preset/rename` | `old:string, new:string` | rename a stored preset; emits `renamed`/`error` |
| `/custos/preset/delete` | `name:string` | delete a stored preset; emits `deleted`/`error` |
| `/custos/preset/list` | — | → replies `/custos/preset/list` (`N, count, names`) |
| `/custos/preset/setroot` | `path:string` | set the machine-global data root (presets **and** instrument DB), persisted; echoes `/custos/preset/root`; the favourites DB is adopted from / carried to the new root |
| `/custos/preset/queryroot` | (none) | report the current data root without changing it; echoes `/custos/preset/root` |

---

## 3. Custos → KM  (send to `127.0.0.1:8000`; first arg always `N`)

> **GP mirror:** `/custos/browsing`, `/custos/loaded`, `/custos/here`, `/custos/patch/stepped`,
> `/custos/midi/route`, `/custos/preset/browsing`, `/custos/preset/loaded`, `/custos/preset/error`,
> and **error-only** `/custos/ack` (text starting `error`) are additionally sent to **GP's OSC-in
> `127.0.0.1:54344`** (not just the KM hub), so the **GP-Script can drive the Voice-Selector, the patch
> axis, the preset axis, derive its routing map, _and_ direct instrument load autonomously** without KM
> in the loop: `loaded` = success/ready, `patch/stepped` = patch-axis feedback, `midi/route` = the live
> channel→synth routing map, `preset/browsing`\|`loaded` = preset-axis feedback,
> error-`ack`\|`preset/error` = failure, `here` = liveness/discovery.
> Success acks (`loaded … count=…`, `cleared`, `mode …`) are **not** mirrored (success is already
> conveyed by `/custos/loaded`), and every other reply — notably the `/custos/param` dump stream —
> goes to `:8000` only, to avoid flooding GP's OSC-in.
>
> **Routing-map derivation (KM-free):** `/custos/midi/route` is mirrored so the GP-Script can derive its
> HumanRoutingMap (channel→synth) live from Custos' real route instead of maintaining it statically. GP
> reads only `t_i != 0 ⇒ this synth reacts on input channel i`. The mirror covers both the reply to
> `/custos/midi/query` (after the on-song populate) and the automatic echo after every `/custos/midi/route`
> command (see §2), so GP stays synced on every live route change with **no polling** and **no KM in the
> path**. Writing the route (GP → `/custos/midi/route` at `9100+N`) was always KM-free; this mirror closes
> the read half so GP is autonomous in both directions.

| Address | Args | Meaning |
|---|---|---|
| `/custos/here` | `N, protoVer:int, mode:string, inner:string, boundCount:int, port:int, facadeCap:int` | hello reply / boot announce; `facadeCap` is this build's facade size (see §4 facade ladder) |
| `/custos/ack` | `N, text:string` | command result (see strings below) |
| `/custos/param` | `N, idx:int, val:float, name:string, defaultVal:float, numSteps:int, label:string` | one dumped param; `defaultVal`/`numSteps`/`label` mirror the inner param's metadata (`numSteps` 0/1 = continuous) |
| `/custos/params/done` | `N, start:int, count:int` | end of a dump range; `count` = params **actually sent** (clamped to `boundCount`), `start` echoes the request |
| `/custos/loaded` | `N, path:string, boundCount:int, innerTotal:int, classId:string` | inner changed (OSC **or** UI); empty path = cleared (`boundCount`/`innerTotal` 0, `classId` empty). Carries `boundCount` so KM can dump immediately — no `hello` round-trip. `innerTotal` is the loaded synth's **full** param count, which may exceed `boundCount`/`facadeCap` (top params unbound/uncontrollable). **`classId`** (v4, trailing) = the inner synth's stable VST3 `createIdentifierString()` — the same key that names preset folders and stamps `.cuspreset` files — so KM can build a hard `preset(classId) → instrument(classId)` link; a v3 reader stops at `innerTotal` and ignores it |
| `/custos/window/rect` | `N, x,y,w,h:int, movable:int, contentW,contentH:int` | synth-window position feedback — emitted when the operator **drags** the window (on mouse-up) or when a rect is (re)applied. **Desktop-logical px (DIPs)**, same unit as the command (amended 2026-07-19). `contentW/contentH` (additive, 2026-07-19) = the hosted editor's **achieved** size: larger than `w/h` means the GUI is centre-cropped in the dock (plugin minimum exceeds the fit area) — KM can hint the shortfall to the operator. Lets KM capture the operator-chosen geometry for its settings |
| `/custos/midi/route` | `N, t1..t16:int` | MIDI route feedback — the current input→output channel map; emitted when a `/custos/midi/route` command is applied and in reply to `/custos/midi/query`. **Mirrored to GP `:54344`** (routing-map derivation, see the GP-mirror note above) |
| `/custos/mainlr` | `N, on:int` | Main L/R fold feedback — the current flag (`1` = all inner outputs summed onto stereo Out 1, `0` = inner pairs mapped across the 5 buses). Emitted after an applied `/custos/mainlr` set and in reply to `/custos/mainlr/query` |
| `/custos/learn/started` | `N` | Learn window opened; capture armed |
| `/custos/learn/moved` | `N, facadeIdx:int, value:float, name:string` | one moved facade parameter (coalesced latest-per-idx, ~25 ms, deadband 0.001); `value` normalised 0..1 |
| `/custos/learn/stopped` | `N, reason:string` | Learn window closed; `reason` = `"stop"` (explicit) or `"timeout"` (safety) |
| `/custos/browsing` | `N, index:int, name:string, wrapped:int` | favourite-browse preview — the cursor's favourite NAME while flipping (`next`/`prev`/`set`). **Not loaded yet** — show the name; the actual load lands later as `/custos/loaded`. `wrapped=1` on the step that wrapped past an end of the list |
| `/custos/patch/stepped` | `N, controlType:string, detail:string` | patch-axis feedback — reports the method that ran for the just-processed `/custos/patch/next`\|`prev` (`PARAM`\|`PC`; the `PRESET` fallback reports via `/custos/preset/browsing`+`/custos/preset/loaded` instead, not this address). `detail` is best-effort: `"+"`/`"-"` for `PARAM` (which direction was injected), the resulting program number (as a string) for `PC` |
| `/custos/preset/browsing` | `N, name:string, idx:int, classId:string, synthName:string` | preset-browse preview — the cursor's preset NAME while flipping (`preset/next`\|`prev`); **not loaded yet** (deferred load follows as `/custos/preset/loaded`). Mirrored to GP. **`classId`/`synthName`** (v4, trailing) tag the event with the loaded synth's stable key + human name so GP can bind it without tracking load state |
| `/custos/preset/loaded` | `N, name:string, idx:int, classId:string, synthName:string` | a preset was recalled (its state restored into the loaded synth). Mirrored to GP. `classId`/`synthName` as above |
| `/custos/preset/saved` \| `renamed` \| `deleted` | `N, name:string, idx:int, classId:string, synthName:string` | preset-store edit confirmations (`idx` = new sorted index; `-1` for delete). KM hub only. `classId`/`synthName` (v4, trailing) as above |
| `/custos/preset/error` | `N, reason:string` | preset op failed (no synth / not found / wrong synth / write\|rename\|delete failed / index out of range). Mirrored to GP |
| `/custos/preset/list` | `N, count:int, names:string…` | reply to `/custos/preset/list` — the loaded synth's preset names, alphabetical |
| `/custos/preset/root` | `N, path:string` | reply to `/custos/preset/setroot` — the applied preset-storage root |

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
- **The machine config file is `instruments.json`**, superseding the old `favorites.json` name — it now
  holds every pushed instrument, not just favourites (see `scope` below). **It now lives under the
  unified data root** (`<root>/instruments.json`, set/queried via `/custos/preset/setroot` \|
  `/custos/preset/queryroot`), with the legacy `%APPDATA%/Custos/instruments.json` (and, before that,
  `favorites.json`) kept as a read-only fallback when no root is set yet or the root has no DB of its
  own. **One-time migration:** on boot, if `<root>/instruments.json` doesn't exist yet, Custos reads the
  legacy file instead (first `%APPDATA%/Custos/instruments.json`, else `favorites.json`) so an upgraded
  install keeps working with no data loss; if a root is set, the legacy data is also self-healed into
  `<root>/instruments.json` once. Until then, repeated boots keep reading the legacy file — this is
  intentional (no silent rewrite of a file KM didn't touch).
- **Boot DB-read robustness:** if the resolved instruments file EXISTS but yields no entries at boot
  (cloud-storage placeholder not hydrated yet / sync lock — observed with the OneDrive root), Custos
  re-reads it every 2 s, up to 5 attempts (each read also re-triggers hydration). Late success acks
  `instrument DB loaded late: <n> entries (attempt <k>)`; final failure acks
  `error instrument DB empty: <path>` (error-ack, so it also mirrors to GP `:54344`). The retry stands
  down silently if favourites arrive another way first (KM push / `setroot` adopt). A missing file
  (first configuration) stays quiet — that is a legitimate state, not a fault.
- **Empty browse sets are reported, never silently ignored.** `/custos/instrument/next|prev|set` on an
  empty instrument list acks `error instrument list empty`. `next|prev` where NO entry passes the
  scope+fit filter (e.g. scope `0` with zero favourites, or every candidate oversized for this facade)
  acks `error no browsable instrument (scope <s>, facade <cap>)` — the cursor does not move, no
  `/custos/browsing` is emitted, and no deferred load is armed. (Previously an exhausted skip loop
  still emitted `/custos/browsing` with `wrapped=1` for an arbitrary non-matching entry.) Both are
  error-acks and mirror to GP.
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
- **The preset store is per-synth Custos snapshots** — distinct from the synth's native patches (the
  Patch axis). `/custos/preset/*` operate on `.cuspreset` files keyed by the loaded synth's class id,
  under the machine-global root (`/custos/preset/setroot`, default `~/Documents/CustosPresets`). Preset
  browsing/recall/errors mirror to GP (so GP drives the preset axis KM-less); store edits
  (`save`/`rename`/`delete`/`list`/`root`) go to the KM hub only.
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

- **Learn window for parameter capture.** KM opens a short window on one Custos with `/custos/learn/start`,
  which emits `/custos/learn/started`. While open, Custos streams each facade parameter the operator moves
  (`/custos/learn/moved`, coalesced ~25 ms, sub-0.001 changes dropped, value normalised 0..1). KM picks
  the intended parameter (largest movement, or a candidate list) and derives Min/Max from the value sweep,
  then binds its macro to that `facadeIdx`. The window closes on `/custos/learn/stop` or a ~10 s safety
  timeout; both emit `/custos/learn/stopped N reason` where `reason` is `"stop"` (explicit) or `"timeout"`
  (safety). Only externally-observable parameter moves of the *inner* synth are reported (no facade-only
  or internal moves). The feature is hub-only (not mirrored to GP) and does not change `protoVer`.
  Internal-vs-user attribution is intentionally not attempted — the short window is the guard.

---

## 5. Responsibilities

- **Custos:** everything in §2/§3; a UI field to set `N` (with a visible warning on bind failure / `N`
  collision); reads the operator-set `N` from its VST state and binds `BASE+N`; persistence (per-instance
  VST state incl. `N`; machine-level instrument config in `instruments.json`, migrated once from
  `favorites.json`).
- **Kapellmeister:** addresses `BASE+N`; keeps the `N ↔ channel/role` map (operator aligns each Custos's
  `N` to match); pushes favorites/volumes; re-binds macros on `/custos/loaded`; checks `protoVer`.
- **GP-Script:** **no identity role** — identity is set manually in the Custos UI.
