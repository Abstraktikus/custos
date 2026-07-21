# Docked-window on-top mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the docked synth window a second, KM-switchable on-top strategy that follows KM's foreground state instead of floating above every application, without regressing today's always-on-top behaviour.

**Architecture:** A pure decision function `dockOnTopEffective()` reads a small state machine (mode + KM-foreground flag). Mode A returns constant `true` (bit-identical to today); Mode B returns the last foreground state KM reported over a new `/custos/window/ontop` OSC verb, which doubles as a heartbeat. A one-shot watchdog timer, re-armed on every message, drops the window out of on-top if KM goes silent. The docking path in `setSynthWindowRect` calls the decision function in place of its hard-coded `true`.

**Tech Stack:** C++17, JUCE 8 (juce::Timer, juce::Component::setAlwaysOnTop), Catch2 v3, CMake + Ninja (MSVC).

## Global Constraints

- **Mode A is the default and must stay bit-identical to today.** In Mode A the docking path calls `setAlwaysOnTop(true)`, exactly as the current literal does. An instance that never receives `/custos/window/ontop` behaves exactly as it does now.
- **No persistence.** The mode always starts at A on construction; it is never written to or read from plugin state. No state-version bump.
- **No window handle on the wire.** The contract carries an `int` state, never an HWND. Do not add owner/parent window relationships (`SetParent`, `GWLP_HWNDPARENT`) — the spec's rejected-fix section explains why (cross-process input-queue attachment → freeze risk).
- **Timeout fail-safe direction:** watchdog expiry drops the window to *not* on top and stays in Mode B. It does NOT fall back to Mode A.
- **Scope:** only the docked (fit) borderless window. Do not touch `onTopMode` (Off/Custos/Instrument), the titled window, or the Custos panel.
- **Build:** `cmd /c "scripts\build.cmd custos_tests"`. **Run one test file:** `build\tests\custos_tests.exe "<TEST_CASE name>"`. **Run all:** `build\tests\custos_tests.exe`. Reference: `docs/superpowers/specs/2026-07-20-custos-dock-ontop-mode-design.md`.

---

### Task 1: On-top decision + mode state machine

The core, fully-testable logic: the mode enum, the state setter, the pure decision function. No timer, no window yet.

**Files:**
- Modify: `src/CustosProcessor.h` (add `DockOnTopMode` enum after `OnTopMode`; public methods; private members)
- Modify: `src/CustosProcessor.cpp` (implement `setDockOnTopState`, `dockOnTopEffective`, `applyDockOnTop`)
- Create: `tests/DockOnTopTest.cpp`
- Modify: `tests/CMakeLists.txt` (register the new test file)

**Interfaces:**
- Produces:
  - `enum DockOnTopMode { DockOnTopAlways, DockOnTopFollowKm };` (namespace `custos` scope)
  - `void CustosProcessor::setDockOnTopState (int state);` — `state < 0` → Mode A; `state == 0|1` → Mode B with `kmForeground = (state != 0)`
  - `bool CustosProcessor::dockOnTopEffective() const noexcept;` — Mode A: `true`; Mode B: `kmForeground`
  - `DockOnTopMode CustosProcessor::getDockOnTopMode() const noexcept;`
- Consumes: nothing from other tasks. `traceN` and `synthWindow` already exist.

- [ ] **Step 1: Register the new test file**

In `tests/CMakeLists.txt`, add `DockOnTopTest.cpp` to the `add_executable(custos_tests ...)` list, next to `BrowseFeedbackTest.cpp`:

```cmake
    BrowseFeedbackTest.cpp
    DockOnTopTest.cpp
```

- [ ] **Step 2: Write the failing test**

Create `tests/DockOnTopTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"

using namespace custos;

// The docked synth window gets a second on-top strategy, switchable by KM at runtime.
// Mode A (default) = today's unconditional always-on-top. Mode B = follow KM's foreground
// state, reported over /custos/window/ontop. See the 2026-07-20 dock-ontop-mode spec.

TEST_CASE ("dock on-top defaults to Mode A, effective = true")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    REQUIRE (proc.getDockOnTopMode() == DockOnTopAlways);
    REQUIRE (proc.dockOnTopEffective() == true);
}

TEST_CASE ("ontop 0 enters Mode B and drops the effective flag")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setDockOnTopState (0);                       // KM: not foreground
    REQUIRE (proc.getDockOnTopMode() == DockOnTopFollowKm);
    REQUIRE (proc.dockOnTopEffective() == false);
}

TEST_CASE ("ontop 1 enters Mode B with the effective flag raised")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setDockOnTopState (1);                       // KM: foreground
    REQUIRE (proc.getDockOnTopMode() == DockOnTopFollowKm);
    REQUIRE (proc.dockOnTopEffective() == true);
}

TEST_CASE ("ontop toggles within Mode B flip the effective flag both ways")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setDockOnTopState (1);
    REQUIRE (proc.dockOnTopEffective() == true);
    proc.setDockOnTopState (0);
    REQUIRE (proc.dockOnTopEffective() == false);
    proc.setDockOnTopState (1);
    REQUIRE (proc.dockOnTopEffective() == true);
}

TEST_CASE ("ontop -1 returns to Mode A regardless of prior KM state")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setDockOnTopState (0);                       // Mode B, dropped
    REQUIRE (proc.dockOnTopEffective() == false);
    proc.setDockOnTopState (-1);                      // hands off -> Mode A
    REQUIRE (proc.getDockOnTopMode() == DockOnTopAlways);
    REQUIRE (proc.dockOnTopEffective() == true);      // back to unconditional on-top
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmd /c "scripts\build.cmd custos_tests"`
Expected: FAIL to compile — `DockOnTopMode`, `getDockOnTopMode`, `setDockOnTopState`, `dockOnTopEffective` undeclared.

- [ ] **Step 4: Add the enum and declarations to the header**

In `src/CustosProcessor.h`, after the existing `enum OnTopMode { OnTopOff, OnTopCustos, OnTopInstrument };` (currently line 25), add:

```cpp
// Docked-window on-top strategy. Mode A (default) forces always-on-top on the docking path —
// today's behaviour, above EVERYTHING. Mode B follows KM's foreground state (reported over
// /custos/window/ontop) so the docked window no longer floats over other apps. See
// docs/superpowers/specs/2026-07-20-custos-dock-ontop-mode-design.md
enum DockOnTopMode { DockOnTopAlways, DockOnTopFollowKm };
```

In the public section, next to `setOnTopMode`/`getOnTopMode` (currently lines 150-151), add:

```cpp
// Docked-window on-top strategy (message thread). setDockOnTopState is KM's runtime switch AND
// its heartbeat: -1 = hands off (Mode A, today's unconditional on-top); 0/1 = Mode B with KM's
// foreground = (state != 0). dockOnTopEffective() is the flag the docking path applies.
void setDockOnTopState (int state);
bool dockOnTopEffective() const noexcept;
DockOnTopMode getDockOnTopMode() const noexcept { return dockMode; }
```

In the private section, next to `onTopMode` (currently line 227), add:

```cpp
DockOnTopMode dockMode = DockOnTopAlways;   // docked-window on-top strategy (A = today's default)
bool kmForeground = false;                  // Mode B: last foreground state KM reported
bool synthWindowDocked = false;             // the borderless window is currently docked (fit) -> on-top managed
void applyDockOnTop();                      // (re)apply the effective flag to a live docked window
```

- [ ] **Step 5: Implement the methods**

In `src/CustosProcessor.cpp`, add these three methods next to `setOnTopMode` (after its closing brace, currently near line 920). `applyDockOnTop` is a safe no-op until a window exists (Task 5 wires the window side):

```cpp
void CustosProcessor::setDockOnTopState (int state)
{
    if (state < 0)                       // -1 = hands off -> Mode A (today's unconditional on-top)
        dockMode = DockOnTopAlways;
    else                                 // 0/1 = KM takes control -> Mode B
    {
        dockMode = DockOnTopFollowKm;
        kmForeground = (state != 0);
    }
    traceN ("dock on-top state=" + juce::String (state)
            + " mode=" + juce::String (dockMode == DockOnTopAlways ? "A" : "B")
            + " effective=" + juce::String (dockOnTopEffective() ? 1 : 0));
    applyDockOnTop();
}

bool CustosProcessor::dockOnTopEffective() const noexcept
{
    return dockMode == DockOnTopAlways ? true : kmForeground;
}

void CustosProcessor::applyDockOnTop()
{
    if (synthWindow != nullptr && synthWindowDocked)
        synthWindow->setAlwaysOnTop (dockOnTopEffective());
}
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmd /c "scripts\build.cmd custos_tests"` then `build\tests\custos_tests.exe "dock on-top*"`
Expected: the 5 new cases PASS.

- [ ] **Step 7: Run the full suite (no regressions)**

Run: `build\tests\custos_tests.exe`
Expected: All tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/CustosProcessor.h src/CustosProcessor.cpp tests/DockOnTopTest.cpp tests/CMakeLists.txt
git commit -m "feat(dock): on-top decision + KM-follow mode state machine"
```

---

### Task 2: Heartbeat watchdog

A one-shot timer, re-armed on every `setDockOnTopState(0|1)`. On expiry (KM went silent) it sets `kmForeground = false` and re-applies — but stays in Mode B. `-1` stops it.

**Files:**
- Modify: `src/CustosProcessor.h` (timeout field, `kmHeartbeat` timer member, two accessors)
- Modify: `src/CustosProcessor.cpp` (arm/stop in `setDockOnTopState`)
- Modify: `tests/DockOnTopTest.cpp` (append watchdog cases)

**Interfaces:**
- Consumes: `setDockOnTopState`, `dockOnTopEffective`, `getDockOnTopMode`, `applyDockOnTop` from Task 1.
- Produces:
  - `void CustosProcessor::setKmHeartbeatTimeoutMs (int ms) noexcept;` (test seam; default 5000)
  - `bool CustosProcessor::kmHeartbeatArmed() const noexcept;` (true while the watchdog runs)
  - Reuses the existing nested `DebounceTimer` type (one-shot: `stopTimer()` then `cb()`).

- [ ] **Step 1: Write the failing test**

Append to `tests/DockOnTopTest.cpp`:

```cpp
TEST_CASE ("Mode B arms the heartbeat watchdog; Mode A does not")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    REQUIRE (proc.kmHeartbeatArmed() == false);       // Mode A default: no watchdog
    proc.setDockOnTopState (1);
    REQUIRE (proc.kmHeartbeatArmed() == true);         // Mode B: armed
    proc.setDockOnTopState (0);
    REQUIRE (proc.kmHeartbeatArmed() == true);         // each message re-arms
    proc.setDockOnTopState (-1);
    REQUIRE (proc.kmHeartbeatArmed() == false);        // hands off: stopped
}

TEST_CASE ("heartbeat expiry drops the window but stays in Mode B")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setKmHeartbeatTimeoutMs (10);                 // fast for the test
    proc.setDockOnTopState (1);                        // Mode B, foreground, armed 10 ms
    REQUIRE (proc.dockOnTopEffective() == true);

    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);   // let the watchdog fire

    REQUIRE (proc.dockOnTopEffective() == false);      // KM silent -> dropped to not-on-top
    REQUIRE (proc.getDockOnTopMode() == DockOnTopFollowKm);   // fail-safe: NOT back to Mode A
    REQUIRE (proc.kmHeartbeatArmed() == false);        // one-shot: not re-armed on its own
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmd /c "scripts\build.cmd custos_tests"`
Expected: FAIL to compile — `setKmHeartbeatTimeoutMs`, `kmHeartbeatArmed` undeclared.

- [ ] **Step 3: Add the timer state and accessors to the header**

In `src/CustosProcessor.h`, in the private block added in Task 1 (next to `dockMode`), add the timeout field and the timer. The nested `DebounceTimer` type is defined at the `browseDebounce` declaration (currently line 247-248), so place `kmHeartbeat` AFTER that line:

Next to `dockMode`/`kmForeground`/`synthWindowDocked`:
```cpp
int kmHeartbeatTimeoutMs = 5000;   // Mode B watchdog: KM silent this long -> treat as background
```

Immediately after the `browseDebounce` struct/member (currently line 248):
```cpp
DebounceTimer kmHeartbeat;   // Mode B watchdog; re-armed on every /custos/window/ontop, one-shot
```

In the public section next to the Task 1 dock methods:
```cpp
void setKmHeartbeatTimeoutMs (int ms) noexcept { kmHeartbeatTimeoutMs = ms; }   // test seam
bool kmHeartbeatArmed() const noexcept;   // true while the Mode B watchdog is running (diagnostic/test)
```

- [ ] **Step 4: Arm/stop the watchdog in `setDockOnTopState`; implement the accessor**

In `src/CustosProcessor.cpp`, replace the body of `setDockOnTopState` from Task 1 with the arming version:

```cpp
void CustosProcessor::setDockOnTopState (int state)
{
    if (state < 0)                       // -1 = hands off -> Mode A (today's unconditional on-top)
    {
        dockMode = DockOnTopAlways;
        kmHeartbeat.stopTimer();
    }
    else                                 // 0/1 = KM takes control -> Mode B
    {
        dockMode = DockOnTopFollowKm;
        kmForeground = (state != 0);
        kmHeartbeat.cb = [this] { kmForeground = false; applyDockOnTop(); };   // silence -> background
        kmHeartbeat.startTimer (kmHeartbeatTimeoutMs);                         // (re)arm on every message
    }
    traceN ("dock on-top state=" + juce::String (state)
            + " mode=" + juce::String (dockMode == DockOnTopAlways ? "A" : "B")
            + " effective=" + juce::String (dockOnTopEffective() ? 1 : 0));
    applyDockOnTop();
}
```

Add the accessor next to `dockOnTopEffective`:

```cpp
bool CustosProcessor::kmHeartbeatArmed() const noexcept { return kmHeartbeat.isTimerRunning(); }
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmd /c "scripts\build.cmd custos_tests"` then `build\tests\custos_tests.exe "*heartbeat*"`
Expected: both new cases PASS.

- [ ] **Step 6: Run the full suite**

Run: `build\tests\custos_tests.exe`
Expected: All tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/CustosProcessor.h src/CustosProcessor.cpp tests/DockOnTopTest.cpp
git commit -m "feat(dock): heartbeat watchdog drops on-top when KM goes silent"
```

---

### Task 3: OSC verb `/custos/window/ontop`

Parse the new inbound address to a `Command`, and dispatch it to `setDockOnTopState`.

**Files:**
- Modify: `src/CustosOscServer.h` (add `WindowOnTop` to `Command::Kind`; add `int onTopState`)
- Modify: `src/CustosOscServer.cpp` (parse branch; dispatch case)
- Modify: `tests/OscCommandTest.cpp` (parse tests)

**Interfaces:**
- Consumes: `CustosProcessor::setDockOnTopState(int)` from Task 1.
- Produces: `Command::WindowOnTop` with `int onTopState` carrying the wire value verbatim (including `-1`).

- [ ] **Step 1: Write the failing test**

Append to `tests/OscCommandTest.cpp`:

```cpp
TEST_CASE ("parseCommand maps /custos/window/ontop with an int state")
{
    const auto on  = parseCommand (juce::OSCMessage ("/custos/window/ontop", (juce::int32) 1));
    REQUIRE (on.kind == Command::WindowOnTop);
    REQUIRE (on.onTopState == 1);

    const auto off = parseCommand (juce::OSCMessage ("/custos/window/ontop", (juce::int32) 0));
    REQUIRE (off.kind == Command::WindowOnTop);
    REQUIRE (off.onTopState == 0);

    const auto handsOff = parseCommand (juce::OSCMessage ("/custos/window/ontop", (juce::int32) -1));
    REQUIRE (handsOff.kind == Command::WindowOnTop);
    REQUIRE (handsOff.onTopState == -1);
}

TEST_CASE ("parseCommand rejects /custos/window/ontop without an int arg")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/window/ontop")).kind == Command::Unknown);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/window/ontop",
                                             juce::String ("1"))).kind == Command::Unknown);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmd /c "scripts\build.cmd custos_tests"`
Expected: FAIL to compile — `Command::WindowOnTop` and `onTopState` undeclared.

- [ ] **Step 3: Extend the `Command` struct**

In `src/CustosOscServer.h`, add `WindowOnTop` to the `Kind` enum. Put it next to the other window kinds on the `WindowShow, ... WindowRect` line (currently line 13):

```cpp
                WindowShow, WindowTitled, WindowHide, WindowRect, WindowOnTop, MidiRoute, MidiQuery,
```

Add the field next to the other WindowRect fields (after `marginLogical`, currently line 29):

```cpp
    int onTopState = 0;   // WindowOnTop: -1 = Mode A (hands off); 0/1 = Mode B KM-foreground flag
```

- [ ] **Step 4: Parse the address**

In `src/CustosOscServer.cpp`, add a parse branch immediately after the `/custos/window/rect` block (which ends at its `return { Command::Unknown, {} };`, currently line 127). A bare `/custos/window/ontop` (no arg) and a non-int arg both fall through to `Unknown`:

```cpp
    if (addr == "/custos/window/ontop")
    {
        if (msg.size() >= 1 && msg[0].isInt32())
        {
            Command c; c.kind = Command::WindowOnTop; c.onTopState = msg[0].getInt32();
            return c;
        }
        return { Command::Unknown, {} };
    }
```

- [ ] **Step 5: Dispatch the command**

In `src/CustosOscServer.cpp`, add a case next to `Command::WindowRect` in the dispatch switch (currently line 362-364):

```cpp
        case Command::WindowOnTop:
            proc.setDockOnTopState (cmd.onTopState);
            break;
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmd /c "scripts\build.cmd custos_tests"` then `build\tests\custos_tests.exe "*window/ontop*"`
Expected: both new cases PASS.

- [ ] **Step 7: Run the full suite**

Run: `build\tests\custos_tests.exe`
Expected: All tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/CustosOscServer.h src/CustosOscServer.cpp tests/OscCommandTest.cpp
git commit -m "feat(dock): /custos/window/ontop OSC verb -> setDockOnTopState"
```

---

### Task 4: Wire the docking path to the decision (GUI glue — rig-verified)

Replace the hard-coded `true` in `setSynthWindowRect`'s fit branch with `dockOnTopEffective()`, and track whether the window is currently docked so a toggle arriving between rects re-applies immediately.

**This task has no unit test:** `setSynthWindowRect`'s fit branch only runs with a real hosted editor + a live `SynthWindow`, which headless CI has no inner synth to provide (all test inners return `hasEditor() == false`). The decision logic it calls is fully covered by Tasks 1-2. Verify this task on the rig (see the handoff note). Keep the change minimal.

**Files:**
- Modify: `src/CustosProcessor.cpp` (fit branch in `setSynthWindowRect`; `synthWindowDocked` bookkeeping in `showSynthWindow` and `hideSynthWindow`)

**Interfaces:**
- Consumes: `dockOnTopEffective()` (Task 1), `synthWindowDocked` (Task 1 member), `applyDockOnTop()` (Task 1).

- [ ] **Step 1: Replace the literal in the fit branch**

In `src/CustosProcessor.cpp` `setSynthWindowRect`, replace the current fit tail (currently lines 968-969):

```cpp
    if (fit)                                  // docked into a host UI region (KM SYNTH view): keep it above the
        synthWindow->setAlwaysOnTop (true);   // host — onTopMode default is Off, so it would fall behind KM otherwise
```

with:

```cpp
    if (fit)   // docked into a host UI region (KM SYNTH view): keep it above the host. Mode A applies an
    {          // unconditional true (unchanged); Mode B applies KM's foreground state instead.
        synthWindowDocked = true;
        synthWindow->setAlwaysOnTop (dockOnTopEffective());
    }
    else
        synthWindowDocked = false;   // a non-fit placement is no longer docked; leave always-on-top as-is (as today)
```

- [ ] **Step 2: Clear the docked flag when the window is (re)created at natural size**

In `src/CustosProcessor.cpp` `showSynthWindow`, inside the creation block, set the flag right after `windowMode = WinBorderless;` (currently line 870):

```cpp
        windowMode = WinBorderless;
        synthWindowDocked = false;   // freshly shown at natural size -> not docked until a fit rect arrives
```

- [ ] **Step 3: Clear the docked flag on hide**

In `src/CustosProcessor.cpp` `hideSynthWindow` (currently lines 996-1002), add the reset next to `windowMode = WinNone;`:

```cpp
    windowMode = WinNone;
    synthWindowDocked = false;
```

- [ ] **Step 4: Build and run the full suite (no regressions)**

Run: `cmd /c "scripts\build.cmd custos_tests"` then `build\tests\custos_tests.exe`
Expected: All tests pass (this task changes GUI glue only; Mode A path still applies `true`).

- [ ] **Step 5: Commit**

```bash
git add src/CustosProcessor.cpp
git commit -m "feat(dock): docking path applies dockOnTopEffective, tracks docked state"
```

---

### Task 5: Stop the docked re-show from stealing keyboard focus (GUI glue — rig-verified)

`showSynthWindow` brings an already-open window to front *with* keyboard focus (`toFront(true)`). In Mode B that is self-defeating: taking focus for GP makes KM lose foreground, KM then reports `ontop 0`, and the just-docked window drops behind. The borderless window is an auxiliary surface and should not steal focus; the titled window (a separate method) keeps today's focus-taking.

**This task has no unit test:** keyboard-focus behaviour is not observable in headless Catch2. Verify on the rig. The change is one argument.

**Files:**
- Modify: `src/CustosProcessor.cpp` (`showSynthWindow`, the re-show branch)

**Interfaces:** none new.

- [ ] **Step 1: Change the re-show to not steal focus**

In `src/CustosProcessor.cpp` `showSynthWindow` (currently line 863), change:

```cpp
    if (synthWindow != nullptr) { synthWindow->toFront (true); return; }
```

to:

```cpp
    if (synthWindow != nullptr) { synthWindow->toFront (false); return; }   // borderless aux window: never steal focus
                                                                            // (docking Mode B: focus theft makes KM
                                                                            //  lose foreground and drop the window)
```

Leave `showSynthWindowTitled`'s `titledWindow->toFront (true)` unchanged — the titled window legitimately takes focus.

- [ ] **Step 2: Build and run the full suite**

Run: `cmd /c "scripts\build.cmd custos_tests"` then `build\tests\custos_tests.exe`
Expected: All tests pass (no behavioural change observable headlessly).

- [ ] **Step 3: Commit**

```bash
git add src/CustosProcessor.cpp
git commit -m "fix(dock): borderless synth-window re-show no longer steals keyboard focus"
```

---

### Task 6: Documentation

Add the new verb to the contract and correct the `/custos/window/rect` row, which currently claims a fit placement always forces always-on-top — true only in Mode A.

**Files:**
- Modify: `docs/osc-contract.md`

**Interfaces:** none.

- [ ] **Step 1: Add the `/custos/window/ontop` row**

In `docs/osc-contract.md`, in the "KM → Custos" table (§2), add a row immediately after the `/custos/window/rect` row (currently line 49):

```markdown
| `/custos/window/ontop` | `state:int` | docked-window on-top strategy + KM heartbeat. `-1` = **hands off**, use always-on-top (Mode A, default — today's behaviour). `0`/`1` = **follow KM** (Mode B): the docked window is on top only while KM is foreground (`1`) and drops behind when it is not (`0`). KM sends on every focus change **and every 2 s while alive** — the message doubles as a heartbeat; **5 s** of silence drops the window to not-on-top (fail-safe: a crashed KM leaves it reachable) while staying in Mode B, so a restarted KM resumes control with its next send. No window handle crosses the wire; ownership is deliberately not used (cross-process owner input-queue attachment can freeze the host). Older KM builds never send this and stay in Mode A |
```

- [ ] **Step 2: Amend the `/custos/window/rect` on-top claim**

In `docs/osc-contract.md`, in the `/custos/window/rect` row (currently line 49), find the sentence:

```
A fit placement also forces the window **always-on-top** (so it stays above the host UI; the window's on-top mode defaults Off).
```

and replace it with:

```
A fit placement also keeps the window above the host UI. **How** is governed by `/custos/window/ontop`: by default (Mode A) it is forced always-on-top; once KM sends `/custos/window/ontop 0|1` (Mode B) the docked window instead follows KM's foreground state.
```

- [ ] **Step 3: Commit**

```bash
git add docs/osc-contract.md
git commit -m "docs(contract): document /custos/window/ontop and the fit on-top modes"
```

---

## Self-Review

**Spec coverage:**
- Two modes, KM-switchable, Mode A default & unchanged → Task 1 (state machine, default `DockOnTopAlways`) + Task 4 (Mode A applies literal `true`). ✓
- `/custos/window/ontop` contract, `-1`/`0`/`1` semantics → Task 3 (parse/dispatch) + Task 6 (docs). ✓
- Heartbeat doubles as liveness; 5 s timeout; fail-safe drops to not-on-top and stays Mode B → Task 2. ✓
- `dockOnTopEffective()` replaces the literal `true`; re-applies on toggle while docked → Task 4. ✓
- Not persisted → no state read/write added anywhere; default set at member init (Task 1). ✓
- Focus fix required for Mode B, scoped to the docked/borderless path → Task 5. ✓
- Timeout settable for tests → Task 2 (`setKmHeartbeatTimeoutMs`). ✓
- Out-of-scope items (onTopMode, titled window, Custos panel) untouched → confirmed: no task edits `setOnTopMode` or the titled path except leaving it explicitly unchanged (Task 5). ✓
- No cross-process owner / GWLP_HWNDPARENT → nothing in the plan adds one; Global Constraints forbids it. ✓

**Placeholder scan:** No TBD/TODO/"handle appropriately". Every code step shows full code. ✓

**Type consistency:** `DockOnTopMode`/`DockOnTopAlways`/`DockOnTopFollowKm`, `setDockOnTopState(int)`, `dockOnTopEffective()`, `getDockOnTopMode()`, `dockMode`, `kmForeground`, `synthWindowDocked`, `applyDockOnTop()`, `kmHeartbeat`, `kmHeartbeatTimeoutMs`, `setKmHeartbeatTimeoutMs`, `kmHeartbeatArmed()`, `Command::WindowOnTop`, `onTopState` — used identically across tasks. The watchdog reuses the existing nested `DebounceTimer` (one-shot). ✓

**Known coverage gap (intentional, honest):** Tasks 4 and 5 are GUI-thread window operations (`setAlwaysOnTop` on a live HWND, keyboard focus) that headless Catch2 cannot observe — no test inner provides an editor, so no `SynthWindow` exists in the suite. All *decision* logic they invoke is covered by Tasks 1-3. These two tasks are verified on the rig. Flagged here and in the handoff rather than papered over with a vacuous test.
