# Custos Volume Trim Implementation Plan (Phase C, Feature F5)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give Kapellmeister a uniform per-instance volume trim — `/custos/volume <gainDb>` sets a dB gain that Custos applies to its output, so every synth can be levelled to a common loudness regardless of where its "real" volume sits.

**Architecture:** The processor holds an atomic linear `masterGain` (default unity). `/custos/volume <gainDb:float>` sets it (dB→linear). `processBlock` applies it to the buffer after the inner synth (or silence during a swap). It is an OSC-only meta control — **not** a facade parameter. Message parsing/routing reuses the existing `Command`/`parseCommand`/OSC-server seam.

**Tech Stack:** C++17, CMake, JUCE 8.0.14, Catch2 v3.15.1, VST3 (Windows). Builds on `master` (addressing core + param dump).

## Global Constraints

- **Contract:** `docs/osc-contract.md` v1. This plan implements only **F5 volume**.
- **Inbound:** `/custos/volume <gainDb:float>` (KM → `127.0.0.1:BASE+N`). No ack, no readback (transient live override — KM tracks what it set; volume is not in `/custos/here`).
- **Meta = OSC:** the trim is applied in DSP, **never** exposed as a facade parameter.
- **dB→linear** via `juce::Decibels::decibelsToGain`. Default = **0 dB (unity)**.
- **Scope note:** the *per-synth default from the machine config* (contract §5.3, "on load applies the config default") is **deferred to F4** (the config file doesn't exist yet). F5 here is the gain + `/custos/volume`; the live gain is **transient** (persists across loads until F4 wires per-synth defaults; not saved in VST state).
- **Build the `Custos_VST3` target.** Build/test: `.\scripts\ci.cmd`.

---

## File Structure

```
src/
  CustosOscServer.h      # MODIFY: Command gains Volume kind + float gainDb
  CustosOscServer.cpp    # MODIFY: parseCommand maps /custos/volume; oscMessageReceived routes Volume -> setVolumeDb
  CustosProcessor.h/.cpp # MODIFY: atomic masterGain + setVolumeDb(); processBlock applies the gain
tests/
  OscContractTest.cpp    # MODIFY: parseCommand(/custos/volume) tests
  VolumeTest.cpp         # NEW: gain applied to the output
tests/CMakeLists.txt     # MODIFY: add VolumeTest.cpp
```

---

## Task 1: parseCommand(/custos/volume)

**Files:**
- Modify: `src/CustosOscServer.h`, `src/CustosOscServer.cpp`, `tests/OscContractTest.cpp`

**Interfaces:**
- Produces: `custos::Command` gains `Volume` and `float gainDb`; `parseCommand` maps `/custos/volume <gainDb:float>` (one float arg required, else `Unknown`).

- [ ] **Step 1: Write the failing tests — append to tests/OscContractTest.cpp**

```cpp
TEST_CASE ("parseCommand maps /custos/volume with a dB float")
{
    juce::OSCMessage msg ("/custos/volume", -6.0f);
    const auto c = parseCommand (msg);
    REQUIRE (c.kind == Command::Volume);
    REQUIRE (c.gainDb == -6.0f);   // -6.0f is exactly representable
}

TEST_CASE ("parseCommand rejects /custos/volume without a float")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/volume")).kind == Command::Unknown);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/volume", 3)).kind == Command::Unknown);  // int, not float
}
```

- [ ] **Step 2: Run to verify failure**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: FAIL — `Command::Volume` and `.gainDb` don't exist.

- [ ] **Step 3: Extend Command — src/CustosOscServer.h**

Replace:
```cpp
struct Command
{
    enum Kind { Load, Clear, Hello, Params, Unknown } kind = Unknown;
    juce::String path;
    int start = 0, count = 0;   // Params
};
```
with:
```cpp
struct Command
{
    enum Kind { Load, Clear, Hello, Params, Volume, Unknown } kind = Unknown;
    juce::String path;
    int start = 0, count = 0;   // Params
    float gainDb = 0.0f;        // Volume
};
```

- [ ] **Step 4: Map /custos/volume — src/CustosOscServer.cpp**

In `parseCommand`, before the final `return { Command::Unknown, {} };`, add:
```cpp
    if (addr == "/custos/volume")
    {
        if (msg.size() >= 1 && msg[0].isFloat32())
            return { Command::Volume, {}, 0, 0, msg[0].getFloat32() };
        return { Command::Unknown, {} };
    }
```

- [ ] **Step 5: Build + test**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: the two new tests PASS; build pristine.

- [ ] **Step 6: Commit**

```bash
git add src/CustosOscServer.h src/CustosOscServer.cpp tests/OscContractTest.cpp
git commit -m "Volume: parseCommand(/custos/volume) + Command Volume/gainDb"
```

---

## Task 2: masterGain + setVolumeDb + processBlock

**Files:**
- Modify: `src/CustosProcessor.h`, `src/CustosProcessor.cpp`
- Create: `tests/VolumeTest.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `void CustosProcessor::setVolumeDb(float db)` (sets the atomic linear gain, message thread); `processBlock` applies `masterGain` to the output.

- [ ] **Step 1: Write the failing test — tests/VolumeTest.cpp**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"

using namespace custos;
using Catch::Approx;

TEST_CASE ("master volume trims the output (dB gain applied after the inner)")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.prepareToPlay (48000.0, 8);
    proc.attachInner (std::make_unique<custos::test::FakeInnerProcessor> (1));  // fills ch0 with 0.5

    juce::AudioBuffer<float> buf (2, 8);
    juce::MidiBuffer midi;

    // Default = unity gain (0 dB).
    buf.clear(); proc.processBlock (buf, midi);
    REQUIRE (buf.getSample (0, 0) == Approx (0.5f));

    // ~ -6.02 dB = 0.5 linear -> output halved.
    proc.setVolumeDb (juce::Decibels::gainToDecibels (0.5f));
    buf.clear(); proc.processBlock (buf, midi);
    REQUIRE (buf.getSample (0, 0) == Approx (0.25f));

    // ~ +6.02 dB = 2.0 linear -> output doubled.
    proc.setVolumeDb (juce::Decibels::gainToDecibels (2.0f));
    buf.clear(); proc.processBlock (buf, midi);
    REQUIRE (buf.getSample (0, 0) == Approx (1.0f));
}
```

- [ ] **Step 2: Register the test + run to verify failure**

Add `VolumeTest.cpp` to the `add_executable(custos_tests ...)` list in `tests/CMakeLists.txt`.
Run (PowerShell): `.\scripts\ci.cmd`
Expected: FAIL — `setVolumeDb` is not a member.

- [ ] **Step 3: Declare masterGain + setVolumeDb — src/CustosProcessor.h**

Add the include near the others (after `#include <functional>`): `#include <atomic>`.

In the public section, after `dumpParams`, add:
```cpp
    // F5: uniform output trim. dB -> linear; applied in processBlock. Message thread.
    void setVolumeDb (float db);
```
In `private:`, near `lastBindOk`, add:
```cpp
    std::atomic<float> masterGain { 1.0f };   // linear; 1.0 = unity (0 dB)
```

- [ ] **Step 4: Define setVolumeDb + apply in processBlock — src/CustosProcessor.cpp**

Add the method (e.g. after `dumpParams`):
```cpp
void CustosProcessor::setVolumeDb (float db)
{
    masterGain.store (juce::Decibels::decibelsToGain (db));
}
```
Replace `processBlock`:
```cpp
void CustosProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const juce::SpinLock::ScopedTryLockType tl (swapLock);
    if (tl.isLocked() && inner != nullptr)
        inner->processBlock (buffer, midi);   // MIDI in -> stereo out, straight through
    else
        buffer.clear();                        // no synth, or a swap is in progress -> silence
}
```
with (apply the trim after, releasing the swap lock first):
```cpp
void CustosProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    {
        const juce::SpinLock::ScopedTryLockType tl (swapLock);
        if (tl.isLocked() && inner != nullptr)
            inner->processBlock (buffer, midi);   // MIDI in -> stereo out, straight through
        else
            buffer.clear();                        // no synth, or a swap is in progress -> silence
    }
    buffer.applyGain (masterGain.load());          // F5 uniform trim (1.0 = unity)
}
```

- [ ] **Step 5: Build + test**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: all tests PASS (prior + the new Volume test). Build pristine.

- [ ] **Step 6: Commit**

```bash
git add src/CustosProcessor.h src/CustosProcessor.cpp tests/VolumeTest.cpp tests/CMakeLists.txt
git commit -m "Volume: atomic masterGain + setVolumeDb + processBlock trim"
```

---

## Task 3: Route /custos/volume through the OSC server

**Files:**
- Modify: `src/CustosOscServer.cpp`

**Interfaces:**
- Consumes: `parseCommand` (`Command::Volume`, `gainDb`), `CustosProcessor::setVolumeDb` (Task 2).

- [ ] **Step 1: Handle Volume — src/CustosOscServer.cpp**

In `oscMessageReceived`, add a case before `case Command::Unknown:`:
```cpp
        case Command::Volume:
            proc.setVolumeDb (cmd.gainDb);
            break;
```

- [ ] **Step 2: Build + test**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: all tests PASS (count unchanged — transport). Build pristine, `Custos.vst3` produced.

- [ ] **Step 3: Commit**

```bash
git add src/CustosOscServer.cpp
git commit -m "Volume: route /custos/volume -> setVolumeDb"
```

---

## Task 4: Autonomous E2E

**Goal:** confirm `/custos/volume` reaches a live instance and is accepted without disruption. (Audio level is not measurable over OSC — the DSP is proven by the Task 2 unit test; the E2E confirms liveness + an aural check by the operator.) Record in `experiments/volume.md`.

- [ ] **Step 1: Deploy + bind an instance**

`.\scripts\deploy.cmd` → restart GP (relaunch the open gig; path from the GP process command line) → confirm `/custos/hello :9109` → `/custos/here [9,…]` with a synth loaded.

- [ ] **Step 2: Set volume, confirm liveness**

Send `/custos/volume -6.0` → `:9109`. Expected: no crash; `/custos/hello :9109` still answers `/custos/here`. Send `/custos/volume 0.0` to restore unity. The operator confirms the level change aurally while playing.

- [ ] **Step 3: Record + commit**

Write `experiments/volume.md` with the observed liveness (and the operator's aural note), then:
```bash
git add experiments/volume.md
git commit -m "Volume: autonomous E2E (liveness) + aural note"
```

---

## Self-Review

**Spec coverage (contract §5.3 F5 + §3):**
- `/custos/volume <gainDb>` inbound → Task 1 (`parseCommand`) + Task 3 (route). ✓
- dB→linear trim applied in DSP, unity default → Task 2 (`setVolumeDb`, `processBlock`, `masterGain{1.0}`). ✓
- Meta = OSC, not a facade param → Task 2 (a private atomic, no `addParameter`). ✓
- Transient (not persisted), per-synth config default deferred to F4 → noted in Global Constraints. ✓

**Placeholder scan:** every code step complete; no TBD/TODO. The audio E2E limitation is stated explicitly (unit test is the proof).

**Type consistency:** `Command{…, gainDb:float}` (Task 1) consumed unchanged in Task 3. `setVolumeDb(float)` (Task 2) is the only method Task 3 calls. `masterGain` is `std::atomic<float>` written in `setVolumeDb` and read in `processBlock`. Each task compiles green on its own (Task 2's `setVolumeDb` is self-contained; Task 3 only consumes it).
