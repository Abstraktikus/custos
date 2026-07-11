# Custos Learn — Parameter Capture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let KM bind a macro by moving the knob — while a short KM-opened "Learn" window is active, Custos streams which facade parameter(s) the operator moves and to what value.

**Architecture:** KM sends `/custos/learn/start|stop` to a specific Custos. While active, the inner synth's `audioProcessorParameterChanged` callback (RT-safe) enqueues `{innerIdx, value}` into a lock-free FIFO; a ~25 ms message-thread timer drains it, coalesces to the latest value per index, applies a deadband, maps the inner index to the facade index + name, and emits `/custos/learn/moved` to the KM hub. A safety timeout auto-closes. All emission goes through the existing `outboundSink`.

**Tech Stack:** C++17, JUCE (AudioProcessor / AbstractFifo / Timer / OSC), Catch2 v3, CMake + Ninja + MSVC.

## Global Constraints

- **protoVer unchanged: `kProtoVersion` stays 3.** `/custos/here` is not touched; verbs are purely additive.
- **Hub-only.** `learn/*` is NOT added to `gpMirrorsFeedback` — it must not mirror to GP `:54344`.
- **RT-safety.** `audioProcessorParameterChanged` may fire on the audio thread. It must not allocate or send OSC — only enqueue into the lock-free FIFO. All OSC emission happens on the message thread (`drainLearn`, `startLearn`, `stopLearn`).
- **Value semantics.** `value` is the normalised parameter value (0..1), carried as an OSC float.
- **Index semantics.** The callback's index is the inner host-order index; it equals the facade index for `idx < boundCount`. Emit the facade index. Ignore `idx >= boundCount` (unbound facade tail).
- **Deadband:** ε = `0.001f` (normalised), compared against the last *emitted* value for that index.
- **Timers:** drain interval `25 ms`; safety timeout `10000 ms` (auto-close with reason `"timeout"`).
- **First arg of every outbound message is `N` (identity).** Inbound `learn/start|stop` carry `N` too (the port already encodes identity; `N` is accepted but not required for dispatch, matching the other inbound verbs).
- **Build:** load VS env then the VS-bundled cmake against `C:\dev\custos\build`:
  ```
  call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
  "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build C:\dev\custos\build --target custos_tests
  ```
  Ninja auto-reconfigures when `CMakeLists.txt` changes. Run tests: `C:\dev\custos\build\tests\custos_tests.exe "<catch2-name-filter>"`. From Git Bash, wrap in a scratchpad `.cmd` and call `cmd //c "<wrapper.cmd>" "<filter>"` (double-slash avoids MSYS path mangling).

---

## File Structure

- **Create** `tests/LearnProcessorTest.cpp` — processor-level Learn tests (lifecycle + capture).
- **Modify** `src/OscContract.h` — three builders: `buildLearnStarted`, `buildLearnMoved`, `buildLearnStopped`.
- **Modify** `src/CustosProcessor.h` / `src/CustosProcessor.cpp` — Learn state (flag, FIFO, timers, last-emitted map) and methods `startLearn` / `stopLearn` / `drainLearn` / `learnRecord`; fill the empty `audioProcessorParameterChanged`.
- **Modify** `src/CustosOscServer.h` — add `LearnStart`, `LearnStop` to `Command::Kind`.
- **Modify** `src/CustosOscServer.cpp` — `parseCommand` mapping + dispatch handlers.
- **Modify** `tests/OscContractTest.cpp` — builder tests + `gpMirrorsFeedback` hub-only regression.
- **Modify** `tests/OscCommandTest.cpp` — `parseCommand` mapping tests.
- **Modify** `tests/CMakeLists.txt` — register `LearnProcessorTest.cpp`.
- **Modify** `docs/osc-contract.md` — document the Learn verbs + windowed-capture model.

---

## Task 1: OSC contract builders

**Files:**
- Modify: `src/OscContract.h` (add after `buildPatchStepped`, before the closing `}` of `namespace custos`)
- Test: `tests/OscContractTest.cpp`

**Interfaces:**
- Produces:
  - `juce::OSCMessage buildLearnStarted (int n)` → `("/custos/learn/started", n)`
  - `juce::OSCMessage buildLearnMoved (int n, int facadeIdx, float value, const juce::String& name)` → `("/custos/learn/moved", n, facadeIdx, value, name)`
  - `juce::OSCMessage buildLearnStopped (int n, const juce::String& reason)` → `("/custos/learn/stopped", n, reason)`

- [ ] **Step 1: Write the failing tests**

Append to `tests/OscContractTest.cpp` (it already `#include`s `<catch2/catch_test_macros.hpp>` and `"OscContract.h"`; add `#include <catch2/catch_approx.hpp>` and `using Catch::Approx;` near the top if not present):

```cpp
TEST_CASE ("buildLearnStarted carries N")
{
    const auto m = buildLearnStarted (7);
    REQUIRE (m.getAddressPattern().toString() == "/custos/learn/started");
    REQUIRE (m.size() == 1);
    REQUIRE (m[0].getInt32() == 7);
}

TEST_CASE ("buildLearnMoved carries N, facadeIdx, value, name")
{
    const auto m = buildLearnMoved (7, 2373, 0.63f, "Filter Cutoff");
    REQUIRE (m.getAddressPattern().toString() == "/custos/learn/moved");
    REQUIRE (m.size() == 4);
    REQUIRE (m[0].getInt32() == 7);
    REQUIRE (m[1].getInt32() == 2373);
    REQUIRE (m[2].getFloat32() == Approx (0.63f));
    REQUIRE (m[3].getString() == "Filter Cutoff");
}

TEST_CASE ("buildLearnStopped carries N and reason")
{
    const auto m = buildLearnStopped (7, "timeout");
    REQUIRE (m.getAddressPattern().toString() == "/custos/learn/stopped");
    REQUIRE (m.size() == 2);
    REQUIRE (m[0].getInt32() == 7);
    REQUIRE (m[1].getString() == "timeout");
}
```

- [ ] **Step 2: Run tests to verify they fail**

Build: `cmake --build C:\dev\custos\build --target custos_tests` (via the vcvars wrapper).
Expected: FAIL to compile — `buildLearnStarted` / `buildLearnMoved` / `buildLearnStopped` are undeclared.

- [ ] **Step 3: Add the builders**

In `src/OscContract.h`, immediately before the final `}` that closes `namespace custos`:

```cpp
// Learn feedback (Custos -> KM hub, hub-only). N first. `started`/`stopped` bracket a KM-opened
// capture window; `moved` reports one facade parameter the operator moved (idx into the facade,
// normalised value, current name) so KM can bind a macro by wiggling the knob. reason = "stop"|"timeout".
inline juce::OSCMessage buildLearnStarted (int n)
{
    return juce::OSCMessage ("/custos/learn/started", n);
}

inline juce::OSCMessage buildLearnMoved (int n, int facadeIdx, float value, const juce::String& name)
{
    return juce::OSCMessage ("/custos/learn/moved", n, facadeIdx, value, name);
}

inline juce::OSCMessage buildLearnStopped (int n, const juce::String& reason)
{
    return juce::OSCMessage ("/custos/learn/stopped", n, reason);
}
```

- [ ] **Step 4: Run tests to verify they pass**

Build, then: `custos_tests.exe "buildLearn*"`
Expected: PASS (3 test cases).

- [ ] **Step 5: Commit**

```bash
git add src/OscContract.h tests/OscContractTest.cpp
git commit -m "feat(custos): OSC builders for /custos/learn/{started,moved,stopped}"
```

---

## Task 2: Learn window lifecycle (start/stop/acks + safety timeout)

**Files:**
- Modify: `src/CustosProcessor.h` (public method decls + private members)
- Modify: `src/CustosProcessor.cpp` (definitions)
- Create: `tests/LearnProcessorTest.cpp`
- Modify: `tests/CMakeLists.txt` (register the new test)

**Interfaces:**
- Consumes: `buildLearnStarted`, `buildLearnStopped` (Task 1); existing `outboundSink`, `identityN`, `DebounceTimer` (one-shot; defined in `CustosProcessor.h`).
- Produces (public):
  - `void startLearn();` — open window: emit `/custos/learn/started`, reset capture state, arm drain + safety timers. Idempotent re-arm.
  - `void stopLearn (const juce::String& reason);` — close window: stop timers, final `drainLearn()`, emit `/custos/learn/stopped N reason`. No-op if already closed.
  - `void drainLearn();` — declared here, implemented in Task 3 as a no-op stub for now (so the timer/link resolves).
  - `bool learnActiveForTest() const noexcept;` — test observer.
- Produces (private): `std::atomic<bool> learnActive`, a repeating drain timer, a one-shot safety timer, plus the FIFO/last-emitted members added in Task 3.

- [ ] **Step 1: Write the failing tests**

Create `tests/LearnProcessorTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"
#include <vector>

using namespace custos;

TEST_CASE ("startLearn emits /custos/learn/started with N")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setIdentity (4);
    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.startLearn();

    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/learn/started");
    REQUIRE (sent[0][0].getInt32() == 4);
    REQUIRE (proc.learnActiveForTest());

    proc.stopLearn ("stop");   // stop timers before the processor is torn down
}

TEST_CASE ("stopLearn emits /custos/learn/stopped with the reason")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setIdentity (4);
    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.startLearn();
    sent.clear();
    proc.stopLearn ("stop");

    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/learn/stopped");
    REQUIRE (sent[0][0].getInt32() == 4);
    REQUIRE (sent[0][1].getString() == "stop");
    REQUIRE_FALSE (proc.learnActiveForTest());
}

TEST_CASE ("stopLearn is a no-op when the window was never opened")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setIdentity (4);
    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.stopLearn ("stop");
    REQUIRE (sent.empty());
}

TEST_CASE ("re-entrant startLearn re-emits started and stays a single window")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setIdentity (4);
    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.startLearn();
    proc.startLearn();

    REQUIRE (sent.size() == 2);              // started emitted each time (idempotent for KM UI)
    REQUIRE (proc.learnActiveForTest());

    sent.clear();
    proc.stopLearn ("stop");
    REQUIRE (sent.size() == 1);              // single window -> one stopped
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/learn/stopped");
}
```

Register it in `tests/CMakeLists.txt` by adding this line inside the `add_executable(custos_tests ...)` list (e.g. after `PatchStepTest.cpp`):

```cmake
    LearnProcessorTest.cpp
```

- [ ] **Step 2: Run tests to verify they fail**

Build.
Expected: FAIL to compile — `startLearn` / `stopLearn` / `learnActiveForTest` are not members of `CustosProcessor`.

- [ ] **Step 3: Declare the Learn API and members in `CustosProcessor.h`**

Add to the **public** section (e.g. right after `emitMainLR();` around line 153):

```cpp
    // Learn mode (message thread). KM opens a short window; while open, inner-parameter moves are
    // captured and streamed as /custos/learn/moved so KM can bind a macro by wiggling the knob.
    // Outside the window nothing is emitted. Spec: docs/superpowers/specs/2026-07-11-custos-learn-param-capture-design.md
    void startLearn();                            // emit /custos/learn/started; arm drain + safety timers
    void stopLearn (const juce::String& reason);  // final drain; emit /custos/learn/stopped N reason; no-op if closed
    void drainLearn();                            // coalesce+emit queued moves (timer-driven; public for tests)
    void learnRecord (int innerIdx, float value) noexcept;   // RT-safe enqueue; gated on learnActive
    bool learnActiveForTest() const noexcept { return learnActive.load(); }
```

Add to the **private** section (e.g. near the other timers, after the `patchInjectTimer` block around line 223). Also add `#include <unordered_map>` to the top includes:

```cpp
    // ---- Learn: windowed inner-parameter capture ----
    std::atomic<bool> learnActive { false };
    static constexpr int   kLearnFifoCap  = 8192;
    static constexpr float kLearnDeadband = 0.001f;
    static constexpr int   kLearnDrainMs  = 25;
    static constexpr int   kLearnSafetyMs = 10000;
    juce::AbstractFifo learnFifo { kLearnFifoCap };
    std::array<int,   (size_t) kLearnFifoCap> learnIdxRing {};
    std::array<float, (size_t) kLearnFifoCap> learnValRing {};
    std::unordered_map<int, float> learnLastEmitted;   // per facade idx: last emitted value (deadband)
    struct RepeatingTimer : juce::Timer { std::function<void()> cb;
        void timerCallback() override { if (cb) cb(); } } learnDrainTimer;
    DebounceTimer learnSafetyTimer;   // one-shot; fires stopLearn("timeout")
```

- [ ] **Step 4: Define `startLearn` / `stopLearn` and a temporary `drainLearn` stub in `CustosProcessor.cpp`**

Add near `emitMainLR()` / the other emit helpers:

```cpp
void CustosProcessor::startLearn()
{
    learnLastEmitted.clear();
    learnFifo.reset();
    learnActive.store (true);
    if (outboundSink) outboundSink (buildLearnStarted (identityN));

    learnDrainTimer.cb = [this] { drainLearn(); };
    learnDrainTimer.startTimer (kLearnDrainMs);

    learnSafetyTimer.cb = [this] { stopLearn ("timeout"); };
    learnSafetyTimer.startTimer (kLearnSafetyMs);   // one-shot (DebounceTimer stops itself)
}

void CustosProcessor::stopLearn (const juce::String& reason)
{
    if (! learnActive.exchange (false)) return;   // already closed -> no duplicate stopped
    learnDrainTimer.stopTimer();
    learnSafetyTimer.stopTimer();
    drainLearn();                                 // flush anything still queued
    if (outboundSink) outboundSink (buildLearnStopped (identityN, reason));
}

void CustosProcessor::drainLearn()
{
    // Implemented in Task 3. Stub for now so the timer callback links.
}
```

Ensure `CustosProcessor.cpp` sees the builders — it already includes `CustosProcessor.h`, which includes `OscContract.h` transitively via the other build helpers; if not, add `#include "OscContract.h"` at the top of `CustosProcessor.cpp` (it is already used there for `buildLoaded`/`buildParam`, so no new include is needed).

- [ ] **Step 5: Run tests to verify they pass**

Build, then: `custos_tests.exe "*Learn*"`
Expected: PASS (4 lifecycle cases). The `stopLearn` final-drain path calls the stub (no-op) and still emits `stopped`.

- [ ] **Step 6: Commit**

```bash
git add src/CustosProcessor.h src/CustosProcessor.cpp tests/LearnProcessorTest.cpp tests/CMakeLists.txt
git commit -m "feat(custos): Learn window lifecycle — start/stop + started/stopped acks + safety timeout"
```

---

## Task 3: Parameter capture (record / drain / coalesce / deadband / gate)

**Files:**
- Modify: `src/CustosProcessor.cpp` (implement `learnRecord`, replace the `drainLearn` stub, fill `audioProcessorParameterChanged`)
- Modify: `src/CustosProcessor.h` (change the `audioProcessorParameterChanged` override from `{}` to a declaration)
- Modify: `tests/LearnProcessorTest.cpp` (append capture tests)

**Interfaces:**
- Consumes: `buildLearnMoved` (Task 1); `learnActive`, `learnFifo`, `learnIdxRing`/`learnValRing`, `learnLastEmitted`, `kLearnDeadband` (Task 2); existing `boundCount`, `facade` (`std::vector<FacadeParameter*>`, `getName(int)`), `identityN`, `outboundSink`.
- Produces: `learnRecord` enqueues moves; `drainLearn` emits `/custos/learn/moved` per coalesced, supra-deadband, in-range index. `audioProcessorParameterChanged` routes inner moves into `learnRecord`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/LearnProcessorTest.cpp` (add `#include <catch2/catch_approx.hpp>` and `using Catch::Approx;` near the top). `FakeInnerProcessor (5)` names its params `"Fake 0".."Fake 4"` and binds facade `0..4` (`boundCount == 5`):

```cpp
namespace {
// Build a loaded, learning processor. Caller must have a live juce::ScopedJuceInitialiser_GUI in scope.
// boundCount = 5 (FakeInnerProcessor with 5 params). Sink is set AFTER attach so the /custos/loaded
// emit is not captured; the started ack is cleared before returning.
std::unique_ptr<CustosProcessor> makeLoadedLearning (std::vector<juce::OSCMessage>& sent)
{
    auto proc = std::make_unique<CustosProcessor>();
    proc->prepareToPlay (48000.0, 64);
    proc->setIdentity (4);
    proc->attachInner (std::make_unique<custos::test::FakeInnerProcessor> (5));
    proc->outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };
    proc->startLearn();
    sent.clear();
    return proc;
}
} // namespace

TEST_CASE ("learnRecord is ignored while the window is closed")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.prepareToPlay (48000.0, 64);
    proc.setIdentity (4);
    proc.attachInner (std::make_unique<custos::test::FakeInnerProcessor> (5));
    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.learnRecord (2, 0.8f);   // not learning -> dropped
    proc.drainLearn();
    REQUIRE (sent.empty());
}

TEST_CASE ("drainLearn coalesces to the latest value per index and names it")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::vector<juce::OSCMessage> sent;
    auto proc = makeLoadedLearning (sent);

    proc->learnRecord (2, 0.1f);
    proc->learnRecord (2, 0.5f);
    proc->learnRecord (2, 0.9f);
    proc->drainLearn();

    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/learn/moved");
    REQUIRE (sent[0][0].getInt32() == 4);
    REQUIRE (sent[0][1].getInt32() == 2);
    REQUIRE (sent[0][2].getFloat32() == Approx (0.9f));
    REQUIRE (sent[0][3].getString() == "Fake 2");

    proc->stopLearn ("stop");
}

TEST_CASE ("drainLearn drops sub-deadband changes after the first emit")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::vector<juce::OSCMessage> sent;
    auto proc = makeLoadedLearning (sent);

    proc->learnRecord (2, 0.5f);
    proc->drainLearn();
    REQUIRE (sent.size() == 1);          // first move always emits
    sent.clear();

    proc->learnRecord (2, 0.5005f);      // < 0.001 from last emitted -> dropped
    proc->drainLearn();
    REQUIRE (sent.empty());

    proc->learnRecord (2, 0.7f);         // > deadband -> emits
    proc->drainLearn();
    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0][2].getFloat32() == Approx (0.7f));

    proc->stopLearn ("stop");
}

TEST_CASE ("drainLearn ignores indices at or beyond boundCount")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::vector<juce::OSCMessage> sent;
    auto proc = makeLoadedLearning (sent);

    proc->learnRecord (5, 0.9f);         // == boundCount (unbound tail)
    proc->learnRecord (99, 0.9f);
    proc->drainLearn();
    REQUIRE (sent.empty());

    proc->learnRecord (4, 0.9f);         // last bound index
    proc->drainLearn();
    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0][1].getInt32() == 4);

    proc->stopLearn ("stop");
}

TEST_CASE ("stopLearn flushes queued moves before the stopped ack")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::vector<juce::OSCMessage> sent;
    auto proc = makeLoadedLearning (sent);

    proc->learnRecord (3, 0.6f);         // queued, not yet drained
    proc->stopLearn ("stop");

    REQUIRE (sent.size() == 2);
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/learn/moved");
    REQUIRE (sent[0][1].getInt32() == 3);
    REQUIRE (sent[1].getAddressPattern().toString() == "/custos/learn/stopped");
    REQUIRE (sent[1][1].getString() == "stop");
}
```

- [ ] **Step 2: Run tests to verify they fail**

Build, then: `custos_tests.exe "*Learn*"`
Expected: FAIL — the coalesce/deadband/boundCount/flush cases fail because `drainLearn` is still the no-op stub (`learnRecord` links but emits nothing).

- [ ] **Step 3: Implement `learnRecord` and `drainLearn`; fill `audioProcessorParameterChanged`**

In `src/CustosProcessor.h`, change the listener override from the empty body to a declaration:

```cpp
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override;
```

In `src/CustosProcessor.cpp`, add `#include <cmath>` at the top, then implement (replace the Task-2 `drainLearn` stub):

```cpp
void CustosProcessor::learnRecord (int innerIdx, float value) noexcept
{
    if (! learnActive.load (std::memory_order_relaxed)) return;   // gate: only while a window is open

    int start1, size1, start2, size2;
    learnFifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0)
    {
        learnIdxRing[(size_t) start1] = innerIdx;
        learnValRing[(size_t) start1] = value;
        learnFifo.finishedWrite (1);
    }
    // FIFO full -> drop (coalescing tolerates loss; a human sweep won't overflow between 25 ms drains)
}

void CustosProcessor::drainLearn()
{
    // Pull everything queued, coalescing to the latest value per index (FIFO order = chronological).
    std::unordered_map<int, float> latest;
    int start1, size1, start2, size2;
    const int ready = learnFifo.getNumReady();
    learnFifo.prepareToRead (ready, start1, size1, start2, size2);
    for (int i = 0; i < size1; ++i) latest[learnIdxRing[(size_t) (start1 + i)]] = learnValRing[(size_t) (start1 + i)];
    for (int i = 0; i < size2; ++i) latest[learnIdxRing[(size_t) (start2 + i)]] = learnValRing[(size_t) (start2 + i)];
    learnFifo.finishedRead (size1 + size2);

    if (! outboundSink) return;

    for (const auto& kv : latest)
    {
        const int   idx = kv.first;
        const float val = kv.second;
        if (idx < 0 || idx >= boundCount) continue;    // unbound facade tail -> not bindable, ignore

        const auto prev = learnLastEmitted.find (idx);
        if (prev != learnLastEmitted.end() && std::abs (val - prev->second) < kLearnDeadband)
            continue;                                  // sub-deadband since last emit -> drop dither

        learnLastEmitted[idx] = val;
        outboundSink (buildLearnMoved (identityN, idx, val, facade[(size_t) idx]->getName (128)));
    }
}

void CustosProcessor::audioProcessorParameterChanged (juce::AudioProcessor*, int index, float newValue)
{
    // Per-value moves are only interesting while a Learn window is open; learnRecord self-gates and
    // is RT-safe (enqueue only). The parameterInfoChanged re-bind path stays in audioProcessorChanged.
    learnRecord (index, newValue);
}
```

- [ ] **Step 4: Run tests to verify they pass**

Build, then: `custos_tests.exe "*Learn*"`
Expected: PASS (all lifecycle + capture cases).

- [ ] **Step 5: Run the full suite (no regressions)**

Run: `custos_tests.exe`
Expected: PASS — all pre-existing tests plus the new Learn cases. (The filled `audioProcessorParameterChanged` is inert unless `learnActive`, so passthrough/swap/rebind tests are unaffected.)

- [ ] **Step 6: Commit**

```bash
git add src/CustosProcessor.h src/CustosProcessor.cpp tests/LearnProcessorTest.cpp
git commit -m "feat(custos): Learn capture — RT-safe enqueue, coalesce+deadband drain, gated param listener"
```

---

## Task 4: OSC parse + dispatch

**Files:**
- Modify: `src/CustosOscServer.h` (`Command::Kind` enum)
- Modify: `src/CustosOscServer.cpp` (`parseCommand` + dispatch)
- Modify: `tests/OscCommandTest.cpp` (parse mapping)
- Modify: `tests/OscContractTest.cpp` (`gpMirrorsFeedback` hub-only)

**Interfaces:**
- Consumes: `Command` struct; `proc.startLearn()` / `proc.stopLearn(const juce::String&)` (Task 2); `gpMirrorsFeedback` (unchanged — learn/* falls through to `false`).
- Produces: `Command::LearnStart`, `Command::LearnStop`; dispatch that opens/closes the window.

- [ ] **Step 1: Write the failing tests**

Append to `tests/OscCommandTest.cpp`:

```cpp
TEST_CASE ("parseCommand maps /custos/learn/start and /custos/learn/stop")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/learn/start")).kind == Command::LearnStart);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/learn/stop")).kind  == Command::LearnStop);
}
```

Append to `tests/OscContractTest.cpp`:

```cpp
TEST_CASE ("gpMirrorsFeedback keeps learn/* hub-only")
{
    REQUIRE_FALSE (gpMirrorsFeedback ("/custos/learn/started", {}));
    REQUIRE_FALSE (gpMirrorsFeedback ("/custos/learn/moved",   {}));
    REQUIRE_FALSE (gpMirrorsFeedback ("/custos/learn/stopped", {}));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Build.
Expected: FAIL to compile — `Command::LearnStart` / `Command::LearnStop` are not enum members. (The `gpMirrorsFeedback` test would already pass, but the file won't compile until the enum exists.)

- [ ] **Step 3: Add the enum values**

In `src/CustosOscServer.h`, extend the `Command::Kind` enum (add before `PatchNext, PatchPrev, Unknown`):

```cpp
    enum Kind { Load, Clear, Hello, Params, Volume, FavBegin, FavEntry, FavEnd,
                WindowShow, WindowTitled, WindowHide, WindowRect, MidiRoute, MidiQuery,
                BrowseNext, BrowsePrev, BrowseSet, InstrumentLoad,
                PresetSetRoot, PresetSave, PresetList, PresetLoad, PresetNext, PresetPrev,
                PresetSet, PresetRename, PresetDelete, MainLR, MainLRQuery,
                LearnStart, LearnStop,
                PatchNext, PatchPrev, Unknown } kind = Unknown;
```

- [ ] **Step 4: Map the addresses in `parseCommand`**

In `src/CustosOscServer.cpp`, in `parseCommand`, add next to the other address checks (e.g. after the `/custos/mainlr/query` block around line 52):

```cpp
    if (addr == "/custos/learn/start")
        return { Command::LearnStart, {} };
    if (addr == "/custos/learn/stop")
        return { Command::LearnStop, {} };
```

- [ ] **Step 5: Add the dispatch handlers**

In `src/CustosOscServer.cpp`, in `oscMessageReceived`'s `switch`, add cases (e.g. after `case Command::MainLRQuery:`):

```cpp
        case Command::LearnStart:
            proc.startLearn();
            break;
        case Command::LearnStop:
            proc.stopLearn ("stop");
            break;
```

- [ ] **Step 6: Run tests to verify they pass**

Build, then: `custos_tests.exe "parseCommand maps /custos/learn*" "gpMirrorsFeedback keeps learn*"`
Expected: PASS. Then run the full suite `custos_tests.exe` — Expected: PASS (no regressions; the new enum values are handled, everything else unchanged).

- [ ] **Step 7: Commit**

```bash
git add src/CustosOscServer.h src/CustosOscServer.cpp tests/OscCommandTest.cpp tests/OscContractTest.cpp
git commit -m "feat(custos): parse + dispatch /custos/learn/start|stop -> startLearn/stopLearn"
```

---

## Task 5: Document the Learn verbs

**Files:**
- Modify: `docs/osc-contract.md`

**Interfaces:**
- Consumes: nothing (documentation only).

- [ ] **Step 1: Read the contract doc to match its section layout**

Run: open `docs/osc-contract.md`; find the inbound-verbs table (§2), the feedback table (§3), and the prose section (§4) so the new rows match the existing column shapes (mainlr/query and the feedback rows are the nearest templates).

- [ ] **Step 2: Add the inbound rows (§2)**

Add to the KM → Custos table:

```markdown
| `/custos/learn/start` | `N:int` | open a Learn window: while open, operator parameter moves stream as `/custos/learn/moved`. Idempotent re-arm. Auto-closes after ~10 s. |
| `/custos/learn/stop`  | `N:int` | close the Learn window (emits `/custos/learn/stopped`). |
```

- [ ] **Step 3: Add the feedback rows (§3)**

Add to the Custos → KM table (hub-only; not GP-mirrored):

```markdown
| `/custos/learn/started` | `N` | Learn window opened; capture armed |
| `/custos/learn/moved`   | `N, facadeIdx:int, value:float, name:string` | one moved facade parameter (coalesced latest-per-idx, ~25 ms, deadband 0.001); `value` normalised 0..1 |
| `/custos/learn/stopped` | `N, reason:string` | Learn window closed; `reason` = `"stop"` (explicit) or `"timeout"` (safety) |
```

- [ ] **Step 4: Add a short prose subsection (§4)**

```markdown
### Learn (bind a macro by moving the knob)

KM opens a short window on one Custos with `/custos/learn/start`. While open, Custos streams each facade
parameter the operator moves (`/custos/learn/moved`, coalesced ~25 ms, sub-0.001 changes dropped). KM
picks the intended parameter (largest movement, or a candidate list) and derives Min/Max from the value
sweep, then binds its macro to that `facadeIdx`. The window closes on `/custos/learn/stop` or a ~10 s
safety timeout; both emit `/custos/learn/stopped N reason`. Only externally-observable parameter moves
of the *inner* synth are reported; the feature is hub-only (not mirrored to GP) and does not change
`protoVer`. Internal-vs-user attribution is intentionally not attempted — the short window is the guard.
```

- [ ] **Step 5: Commit**

```bash
git add docs/osc-contract.md
git commit -m "docs(custos): document the /custos/learn/* verbs + windowed-capture model"
```

---

## Self-Review Notes

- **Spec coverage:** §Contract additions → Tasks 1, 4; §Behaviour (gating, threading, coalescing, deadband, index range, safety timeout, no-inner, re-entrant) → Tasks 2–3; §Components touched → Tasks 1–5; §Testing → Tasks 1–4. All covered.
- **No-inner behaviour** (started ack, no moved): implicitly covered — `startLearn` emits `started` unconditionally, and with no inner `boundCount == 0` so every `drainLearn` index is out of range. (Not a dedicated test; the boundCount-guard test exercises the same code path.)
- **Type consistency:** `startLearn()`, `stopLearn(const juce::String&)`, `drainLearn()`, `learnRecord(int,float)`, `learnActiveForTest()` used identically across header, cpp, tests, and OSC dispatch. Builders `buildLearnStarted/Moved/Stopped` match arg order in both `OscContract.h` and every test.
- **Not sample-accurate / RT-safe:** enforced by FIFO-only enqueue in `learnRecord` and message-thread-only emit in `drainLearn`.
