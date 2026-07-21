# KM handoff — /custos/window/ontop sender (docked-window Mode B) + browse exit signal

**From:** Custos session 2026-07-21 (PRs #20 and #21 open against master, NOT merged — both await
rig verification; a combined rig build of both is deployed and live since 06:52).
**To:** a Kapellmeister session (`C:\dev\kapellmeister`).
**Prior handoff still applies:** `2026-07-19-km-window-contract-and-arturia-workflow.md` (DIP rect
feedback, contentW/H, oversize + boot-DB acks). This adds one new **outbound** verb KM must send,
plus one **inbound** shape KM should recognise.

---

Paste everything below into a fresh Kapellmeister session.

---

## Context

Custos gained a second on-top strategy for the **docked** synth window. Today the docking path
forces the window always-on-top — which means above **every** app, not just above KM: switch to
another application and the borderless, title-bar-less, taskbar-less synth window floats over it,
and it lives in GP's process so a departed KM can't close it. (A real cross-process OWNER window
was evaluated and rejected — it attaches the two processes' input queues and a hung KM could
freeze GP's UI. Details in the custos repo: `docs/superpowers/specs/2026-07-20-custos-dock-ontop-mode-design.md`.)

The fix keeps today's behaviour as the default (**Mode A**) and adds **Mode B**, where the docked
window follows KM's foreground state. **Custos does not know when KM is foreground — KM must tell
it.** That sender is your job. Authoritative wire contract: `docs/osc-contract.md` in the custos
repo (row `/custos/window/ontop`).

## The new outbound verb KM must send

`/custos/window/ontop  state:int`  → send to each Custos instance at `127.0.0.1:(9100 + N)`
(same addressing as `/custos/window/rect`; you already send that near `lib.rs:3246`).

| state | meaning | effect on the docked window |
|---|---|---|
| `1` | KM **is** the foreground app | on top |
| `0` | KM is **not** foreground | ordered normally (drops behind other apps) |
| `-1` | hands off | back to Mode A (unconditional always-on-top — today's behaviour) |

**`state` MUST be an OSC int32.** Custos rejects a non-int arg. `-1` is a normal int32 (`0xffffffff`).

### Two things that make this work — both mandatory

1. **The message doubles as a heartbeat.** Send it on **every KM focus change** AND **every ~2 s
   while KM is alive**, even when the state hasn't changed. If Custos hears nothing for **5 s** it
   assumes KM is gone and drops the window out of on-top (fail-safe — see below). A one-shot `1`
   therefore holds the window up for only ~5 s. Keep the 2 s heartbeat running.

2. **It is opt-in and per-instance.** Sending `0`/`1` switches **that instance** into Mode B; an
   instance that never receives the verb stays in Mode A forever. So send to **every instance you
   manage** (the rig is N=1..10), not just the one currently docked — a Custos that isn't docked
   yet still records the mode and applies it when its next `fit` rect arrives. This is safe:
   Custos only touches a window that is actually docked.

### Foreground detection (KM side)

"KM is foreground" = KM's own top-level window (the SYNTH view / main window) is the active
Windows foreground window. Win32: `GetForegroundWindow()` compared against KM's own HWND(s). In
Tauri, drive it from the window focus/blur events plus a 2 s tick:
- on focus/blur change → send the new state immediately to all managed instances (minimises the
  visible flicker), and
- a 2 s timer → re-send the current state to all managed instances (the heartbeat).

### Fail-safe direction (important, and it simplifies KM)

On timeout Custos drops the window to **not** on top and **stays in Mode B** — it does NOT revert
to Mode A. So a **crashed** KM leaves the docked window *behind* other windows, reachable. This is
the orphan case the `246cdb1` "broadcast `/custos/window hide` on shutdown" workaround was papering
over — and it only ever covered clean exits. With Mode B you can **retire that shutdown broadcast**
for docked windows: on clean exit just stop sending; the watchdog drops them within 5 s. A
restarted KM resumes control with its next heartbeat; nothing needs resetting. (Sending `-1` on a
deliberate "I want the old always-on-top back" is still valid — that's a mode choice, not a
shutdown step.)

### What NOT to do

- **No HWND on the wire.** The contract carries only the int state. Do not try to pass KM's window
  handle — the rejected owner-window approach is exactly what that would reintroduce.
- Don't gate sends on "is this instance docked?" — you can't know reliably, and Custos handles the
  not-docked case itself.

### Reference implementation

The custos repo ships a rig-test driver that speaks this exact contract:
`tools/kmontop.py` (on branch `feat/dock-ontop-mode` / PR #21). It shows the port math, the int32
typing, the 2 s cadence, `-1` handoff, and — in its `auto` command — a `GetForegroundWindow`
polling loop that sends `1` while a chosen window is frontmost, else `0`. That `auto` loop is
essentially the behaviour KM must implement natively. (The operator is using this script to verify
Mode B on the rig before your sender exists — so the plugin side is already testable and live.)

## One inbound shape to recognise (from PR #20, also live on the rig)

`/custos/browsing` can now arrive as a bare **exit signal**, not only as a browse preview: when a
browse lands on an empty/unbrowsable set, Custos emits `/custos/browsing  N, index, name, wrapped`
with **`name = "(none)"`, `wrapped = 1`, and `index` = the unmoved cursor (`-1` if never seeded)**.
It also still sends the matching error-ack (`error instrument list empty` /
`error no browsable instrument (...)`). If KM renders the browse preview anywhere, treat a
`/custos/browsing` whose name is the literal `(none)` as "nothing to browse" — do **not** show it
as an instrument called "(none)". If KM ignores `/custos/browsing` entirely, no action needed.

## Status / coordination

- Both changes are **live on the rig** (combined build deployed 2026-07-21 06:52) but the PRs are
  **not merged** — they wait on rig verification. Mode A is unchanged and default, so nothing
  regresses until KM actively sends `/custos/window/ontop`.
- Custos-side contract is authoritative in `docs/osc-contract.md`. If you need a wire detail this
  handoff doesn't cover, that file wins.
- Expect a brief flicker on app-switch (the OSC round-trip window). Sending on focus-change events
  — not only on the 2 s tick — keeps it minimal.
