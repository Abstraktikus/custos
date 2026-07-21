# Custos — Docked-window on-top mode: KM-follow instead of always-on-top (design)

Date: 2026-07-20 · Status: approved (Martin: "Passt, schreib die Spec")

## Problem

A docked synth window can make the machine effectively unusable on stage.

`CustosProcessor::setSynthWindowRect` forces the window always-on-top on the docking path:

```cpp
if (fit)                                  // docked into a host UI region (KM SYNTH view): keep it above the
    synthWindow->setAlwaysOnTop (true);   // host — onTopMode default is Off, so it would fall behind KM otherwise
```

The comment's goal is right — the window must sit above KM or docking is pointless. But
always-on-top means *above everything*, not *above KM*. Confirmed by Martin: switching to another
application leaves the borderless synth window floating over it. No title bar to grab, no taskbar
entry, no way to get at it.

Worse, the window belongs to **GP's** process. If KM exits there is nobody left to close it. KM
has papered over this since commit 246cdb1 by broadcasting `/custos/window hide` to all ten
instances on shutdown — which does nothing if KM *crashes*.

## The rejected fix, and why

The obvious repair is to replace always-on-top with a real OWNER relationship —
`SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, kmHwnd)`. Owned windows render above their owner, follow
it in z-order against foreign windows, and are destroyed with it. That would solve every symptom
at once.

**Rejected: it is a documented hang risk, and the precedent is exact.**

The owner would live in KM's process and the window in GP's. Raymond Chen, naming owner/owned
explicitly — not just parent/child:

> "Creating a cross-thread parent/child or **owner/owned** window relationship implicitly attaches
> the input queues of the threads which those windows belong to, and this attachment is transitive."
> — <https://devblogs.microsoft.com/oldnewthing/20130412-00/?p=4683>

Mozilla shipped this exact design — a separate process setting Firefox's top-level HWND as the
owner of its dialog, for our reason — and had to remove it. The explicit escape hatch,
`AttachThreadInput(..., FALSE)`, **did not hold**: Windows silently re-attached the queues via IME
windows, after which a bare `PeekMessage` was enough to deadlock.
(<https://dblohm7.ca/blog/2013/02/15/plugin-hang-ui-on-aurora/>,
<https://bugzilla.mozilla.org/show_bug.cgi?id=834127>)

Microsoft's own windowing engineers describe a second, owner-specific hazard that is worse for a
live rig — a user clicking a background window triggers a synchronous kernel call across the
process boundary:

> "**there's no way out other than to either have one thread be unlocked on its own, or to kill
> that process. Un-ownering the window after-the-fact will not repair the situation.**"
> — <https://groups.google.com/a/chromium.org/g/chromium-dev/c/_9jq1ovNF9o>

And the benefit degrades exactly when it is needed: Windows does not promote an unresponsive
window in z-order, so a hung KM loses the ownership guarantee anyway.

This also answers the standing question of why always-on-top was chosen originally. Treat that
line as load-bearing, not accidental.

The pattern IE/Edge adopted instead — "Alternate Owner Window": no OS-level owner, z-order
reproduced in user mode — is what this design implements.

## Decision

Two mechanisms, switchable by KM at runtime. Mode A is unchanged and remains the default, so this
round cannot regress the working rig.

- **Mode A (default, today's behaviour):** the docking path sets always-on-top unconditionally.
- **Mode B (new):** the docking path follows KM's foreground state, which KM reports over OSC. No
  window handle crosses the process boundary and no ownership is established.

Mode B carries a watchdog: if KM goes silent, Custos drops out of always-on-top on its own.

## Contract

New inbound address (KM → Custos):

| Address | Args | Effect |
|---|---|---|
| `/custos/window/ontop` | `state:int` | `1` = KM is foreground → docked window on top · `0` = KM is not foreground → docked window ordered normally · `-1` = hands off, return to Mode A |

Receiving `0` or `1` switches Custos into Mode B. KM sends on every focus change **and every 2 s
while idle** — the state message doubles as the heartbeat, so there is one address and one timer on
the KM side, and the message is idempotent.

**Why a periodic send and not just focus changes:** if any KM traffic counted as a heartbeat, the
watchdog would also starve while KM is *foreground but idle* — the operator simply looking at KM
without moving anything. Custos would drop the window mid-performance. KM must therefore assert
liveness actively.

**Timeout: 5 s.** At a 2 s send interval that tolerates two lost UDP packets before acting.

**On timeout Custos falls to "not on top", NOT to Mode A.** This is the fail-safe direction: a
crashed KM leaves the window *behind* other windows, reachable — which is the orphan problem the
246cdb1 workaround exists for. Custos stays in Mode B, so a restarted KM resumes control with its
next heartbeat and nobody has to reset anything.

No HWND, therefore no 64-bit handle on the wire and no truncation failure mode. `/custos/window/rect`
is untouched — the handle does not ride along on the message that flies on every drag.

**Back-compat:** older KM builds never send this address and stay in Mode A forever.

## Design

### State (`CustosProcessor`)

Three members plus one derived decision:

| Member | Meaning |
|---|---|
| `dockOnTopMode` | `DockOnTopAlways` (A, default) \| `DockOnTopFollowKm` (B) |
| `kmForeground` | last state KM reported; only meaningful in Mode B |
| `kmHeartbeat` | timer, re-armed on every `/custos/window/ontop`; on fire sets `kmForeground = false` |

```cpp
bool dockOnTopEffective() const   // A: constant true — bit-identical to today
{
    return dockOnTopMode == DockOnTopAlways ? true : kmForeground;
}
```

That expression replaces the literal `true` in the `fit` branch of `setSynthWindowRect`. Nothing
else about the docking path changes.

`setDockOnTopState(int)` handles the three wire values: `-1` returns to Mode A and stops the
watchdog; `0`/`1` enter Mode B, set `kmForeground`, and re-arm the watchdog. Every call re-applies
the effective value to a currently docked window, so a toggle arriving while docked takes effect
immediately rather than at the next rect.

**Not persisted.** The mode always starts at A and is re-established by KM at runtime. A plugin
restart falls back to today's behaviour rather than to a stored experimental state, and no state
version bump is needed.

Out of scope, deliberately: `onTopMode` (Off/Custos/Instrument), the titled window, and the Custos
panel. Those are set by the operator on purpose, and they have a title bar and a taskbar entry —
the stage risk does not exist there.

### Focus (required for Mode B, not cosmetic)

`showSynthWindow` calls `toFront (true)` when the window already exists — **with** keyboard focus.
In Mode B that breaks docking outright: the call steals focus for GP, KM loses foreground, KM's own
handler reports `ontop 0`, and the just-docked window drops behind. Mode B cannot work while this
stands.

The docking path therefore shows the window without taking keyboard focus. The fix hangs off the
call path, not globally off the window: "Open" and the titled window keep today's behaviour, where
taking focus is correct.

This `toFront (false)` change is unconditional — it applies to every instance's re-show of an
already-open borderless window, including one that never enters Mode B — and is a deliberate,
narrow deviation from the "bit-identical" wording above: it is limited to keyboard focus on an
explicit re-show of an already-open window, does not affect z-order, and the normal browse/load
path destroys and recreates the window, so it never hits this branch.

### Testing

Unit-testable: the mode transitions including the `-1` return path, `dockOnTopEffective()` across
all four combinations, and watchdog expiry.

The regression guard for this round is that an instance which never receives `/custos/window/ontop`
docks exactly as it does today — always-on-top, unconditionally. Note this is *not* "Mode A ignores
toggles": a `0` arriving in Mode A does switch to Mode B and does drop the window, which is the
intended meaning of KM taking control. Mode A is the state before KM ever speaks, and the state
`-1` returns to.

The heartbeat timeout becomes a settable value (default 5000 ms) rather than a constant, so tests
can drive it in milliseconds instead of waiting five real seconds.

**What unit tests do not prove:** that Windows actually orders the windows the way we expect. Mode
B reproduces z-order in user mode; only the rig can confirm it. Expect a brief flicker during app
switching, on the order of the OSC round trip, where the ordering is still stale. That is the price
of not using ownership — and it buys immunity from the freeze risk above.

### Docs

`docs/osc-contract.md`: add `/custos/window/ontop` to the KM → Custos table, and amend the
`/custos/window/rect` row, which currently states flatly that a fit placement "forces the window
always-on-top" — true only in Mode A.

## Open at hand-off

The KM side is Martin's to build against this contract: send `/custos/window/ontop` on every focus
change and every 2 s while alive; send `-1` to hand control back.
