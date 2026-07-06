# Custos OSC Contract v2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend Custos's external-control OSC surface (protoVer 1→2) with facade-capacity/overflow reporting, a persisted 16→16 MIDI channel routing matrix, a richer parameter dump, and a compile-time facade-size build ladder.

**Architecture:** All changes are additive to the existing `OscContract.h` builders / `parseCommand` dispatch / `CustosProcessor` / `StateCodec`. MIDI routing is a pure function applied in `processBlock` over a lock-free per-channel atomic map, persisted in the versioned VST state. The facade ladder is N JUCE build targets from one source, differing only in a compile constant + VST3 id + name. Custos remains a pure OSC receiver — no `VstDatabase.txt` awareness.

**Tech Stack:** C++17, JUCE 8.0.14, Catch2 v3.15.1, CMake ≥3.22.

## Global Constraints

- **protoVer = 2** (`kProtoVersion` in `src/OscContract.h`).
- **Additive/back-compat only:** Custos→KM changes append fields; KM→Custos changes add new addresses. No existing message's leading args change order.
- **Strict OSC typing:** `parseCommand` must reject malformed messages (wrong arg count/type) → `Command::Unknown`, matching the existing favourites/window pattern.
- **No audio-thread allocation** in the MIDI-routing hot path — reuse a preallocated scratch `MidiBuffer`.
- **State format:** `CUS1` magic + version byte; add **v3**; v1/v2 blobs must still parse (route defaults to identity).
- **MIDI route encoding:** 16 ints, arg *i* = input channel *i* (1-based, in order); value `0` = drop, `1..16` = target output channel. Identity = `1,2,…,16`.
- **Repo language:** English (code, comments, commit messages).
- Run tests from the build dir with `ctest --output-on-failure` or the built `custos_tests` binary. Build: `cmake --build build --target custos_tests`.

---

### Task 1: Bump protoVer to 2

**Files:**
- Modify: `src/OscContract.h:10`
- Test: `tests/OscContractTest.cpp`

**Interfaces:**
- Produces: `custos::kProtoVersion == 2`.

- [ ] **Step 1: Write the failing test** — append to `tests/OscContractTest.cpp`:

```cpp
TEST_CASE ("protoVer is 2 for contract v2")
{
    REQUIRE (custos::kProtoVersion == 2);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target custos_tests && ./build/tests/custos_tests "protoVer is 2 for contract v2"`
Expected: FAIL — `kProtoVersion == 1`.

- [ ] **Step 3: Bump the constant** — `src/OscContract.h:10`:

```cpp
constexpr int kProtoVersion = 2;
```

- [ ] **Step 4: Run to verify it passes**

Run: `./build/tests/custos_tests "protoVer is 2 for contract v2"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/OscContract.h tests/OscContractTest.cpp
git commit -m "Contract v2: bump protoVer 1->2"
```

---

### Task 2: Report facadeCap in /custos/here and innerTotal in /custos/loaded

**Files:**
- Modify: `src/OscContract.h:18-22` (`buildHere`), `:29-32` (`buildLoaded`)
- Modify: `src/CustosProcessor.h` (add `innerParamTotal()`), `src/CustosProcessor.cpp:105-109` (`emitLoaded`)
- Modify: `src/CustosOscServer.cpp:135-140` (`announceHere`)
- Test: `tests/OscContractTest.cpp:21-46`

**Interfaces:**
- Produces: `buildHere(int n, String mode, String inner, int boundCount, int port, int facadeCap)` → 7 args; `buildLoaded(int n, String path, int boundCount, int innerTotal)` → 4 args; `CustosProcessor::innerParamTotal() const noexcept`.

- [ ] **Step 1: Update the failing tests** — replace the `buildHere`/`buildLoaded` cases in `tests/OscContractTest.cpp` (lines 21-46):

```cpp
TEST_CASE ("buildHere carries N, protoVer, mode, inner, count, port, facadeCap")
{
    const auto m = buildHere (3, "replace", "CS-80 V4", 2797, 9103, 5000);
    REQUIRE (m.getAddressPattern().toString() == "/custos/here");
    REQUIRE (m.size() == 7);
    REQUIRE (m[4].getInt32() == 2797);
    REQUIRE (m[5].getInt32() == 9103);
    REQUIRE (m[6].getInt32() == 5000);   // facadeCap
}

TEST_CASE ("buildLoaded carries N, path, boundCount, innerTotal")
{
    const auto l = buildLoaded (5, "C:/x/Diva.vst3", 3124, 9035);
    REQUIRE (l.getAddressPattern().toString() == "/custos/loaded");
    REQUIRE (l.size() == 4);
    REQUIRE (l[2].getInt32() == 3124);
    REQUIRE (l[3].getInt32() == 9035);   // innerTotal (full inner param count)
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target custos_tests`
Expected: FAIL — compile error (too many args to `buildHere`/`buildLoaded`).

- [ ] **Step 3: Extend the builders** — `src/OscContract.h`:

```cpp
inline juce::OSCMessage buildHere (int n, const juce::String& mode, const juce::String& inner,
                                   int boundCount, int port, int facadeCap)
{
    return juce::OSCMessage ("/custos/here", n, kProtoVersion, mode, inner, boundCount, port, facadeCap);
}
```
```cpp
inline juce::OSCMessage buildLoaded (int n, const juce::String& path, int boundCount, int innerTotal)
{
    return juce::OSCMessage ("/custos/loaded", n, path, boundCount, innerTotal);
}
```

- [ ] **Step 4: Add the inner-total getter** — in `src/CustosProcessor.h`, public section (after `boundParamCount()` at line 90):

```cpp
    // Full parameter count of the loaded inner synth (0 if none). May exceed facadeSize():
    // then the top (innerParamTotal - facadeSize) params are unbound / uncontrollable.
    int innerParamTotal() const noexcept { return inner != nullptr ? (int) inner->getParameters().size() : 0; }
```

- [ ] **Step 5: Update the two emit call sites**

`src/CustosProcessor.cpp:108` (`emitLoaded`):
```cpp
        outboundSink (buildLoaded (identityN, currentSynthPath, boundCount, innerParamTotal()));
```
`src/CustosOscServer.cpp:138-139` (`announceHere`):
```cpp
        ackSender.send (buildHere (currentN, proc.modeString(), proc.innerSynthName(),
                                   proc.boundParamCount(), oscPortForIdentity (currentN), proc.facadeSize()));
```

- [ ] **Step 6: Run to verify it passes**

Run: `cmake --build build --target custos_tests && ./build/tests/custos_tests "[.]" ; ./build/tests/custos_tests "buildHere*" "buildLoaded*"`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/OscContract.h src/CustosProcessor.h src/CustosProcessor.cpp src/CustosOscServer.cpp tests/OscContractTest.cpp
git commit -m "Contract v2: /custos/here +facadeCap, /custos/loaded +innerTotal"
```

---

### Task 3: Enrich /custos/param with defaultVal, numSteps, label

**Files:**
- Modify: `src/OscContract.h:34-37` (`buildParam`)
- Modify: `src/CustosProcessor.cpp:118-123` (`dumpParams` loop)
- Test: `tests/OscContractTest.cpp:48-61`, `tests/ParamDumpTest.cpp`

**Interfaces:**
- Produces: `buildParam(int n, int idx, float val, String name, float defaultVal, int numSteps, String label)` → 7 args.

- [ ] **Step 1: Update the failing test** — replace the `buildParam` half of the case at `tests/OscContractTest.cpp:48-61`:

```cpp
TEST_CASE ("buildParam carries N, idx, val, name, defaultVal, numSteps, label")
{
    const auto p = buildParam (7, 5, 0.5f, "Cutoff", 0.25f, 128, "Hz");
    REQUIRE (p.getAddressPattern().toString() == "/custos/param");
    REQUIRE (p.size() == 7);
    REQUIRE (p[1].getInt32()  == 5);
    REQUIRE (p[3].getString() == "Cutoff");
    REQUIRE (p[4].getFloat32() == 0.25f);
    REQUIRE (p[5].getInt32()  == 128);
    REQUIRE (p[6].getString() == "Hz");
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target custos_tests`
Expected: FAIL — compile error (arg count).

- [ ] **Step 3: Extend the builder** — `src/OscContract.h`:

```cpp
inline juce::OSCMessage buildParam (int n, int idx, float val, const juce::String& name,
                                    float defaultVal, int numSteps, const juce::String& label)
{
    return juce::OSCMessage ("/custos/param", n, idx, val, name, defaultVal, numSteps, label);
}
```

- [ ] **Step 4: Feed the extra fields from the facade parameter** — `src/CustosProcessor.cpp:120-121`:

```cpp
        auto* fp = facade[(size_t) i];
        outboundSink (buildParam (identityN, i, fp->getValue(), fp->getName (128),
                                  fp->getDefaultValue(), fp->getNumSteps(), fp->getLabel()));
```

- [ ] **Step 5: Update ParamDumpTest** — find the assertion in `tests/ParamDumpTest.cpp` that captures a `/custos/param` message and add checks that the message now has `size() == 7` and that arg 5 (`getInt32`, numSteps) is present. (Read the file; the captured messages are collected into a vector — assert `msg.size() == 7` on the first non-`done` message.)

- [ ] **Step 6: Run to verify it passes**

Run: `./build/tests/custos_tests "buildParam*" "[paramdump]"`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/OscContract.h src/CustosProcessor.cpp tests/OscContractTest.cpp tests/ParamDumpTest.cpp
git commit -m "Contract v2: enrich /custos/param with defaultVal/numSteps/label"
```

---

### Task 4: Parse /custos/midi/route and /custos/midi/query

**Files:**
- Modify: `src/CustosOscServer.h:9-20` (`Command`)
- Modify: `src/CustosOscServer.cpp` (`parseCommand`, near the window cases ~line 60-95)
- Test: `tests/OscCommandTest.cpp`

**Interfaces:**
- Produces: `Command::MidiRoute` (with `std::array<int,16> route`), `Command::MidiQuery`.

- [ ] **Step 1: Write the failing test** — append to `tests/OscCommandTest.cpp`:

```cpp
TEST_CASE ("parseCommand maps /custos/midi/route with 16 ints")
{
    juce::OSCMessage m ("/custos/midi/route");
    for (int i = 1; i <= 16; ++i) m.addInt32 (i);   // identity
    const auto c = parseCommand (m);
    REQUIRE (c.kind == Command::MidiRoute);
    REQUIRE (c.route[0] == 1);
    REQUIRE (c.route[15] == 16);

    juce::OSCMessage remap ("/custos/midi/route");
    for (int i = 0; i < 16; ++i) remap.addInt32 (i == 7 ? 1 : 0);   // input8 -> out1, rest dropped
    REQUIRE (parseCommand (remap).route[7] == 1);
}

TEST_CASE ("parseCommand rejects /custos/midi/route without 16 ints")
{
    juce::OSCMessage m ("/custos/midi/route");
    for (int i = 0; i < 15; ++i) m.addInt32 (1);
    REQUIRE (parseCommand (m).kind == Command::Unknown);
}

TEST_CASE ("parseCommand maps /custos/midi/query")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/midi/query")).kind == Command::MidiQuery);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target custos_tests`
Expected: FAIL — `MidiRoute`/`MidiQuery` not members of `Command::Kind`.

- [ ] **Step 3: Extend the Command struct** — `src/CustosOscServer.h`:

```cpp
#include <array>
// ...
struct Command
{
    enum Kind { Load, Clear, Hello, Params, Volume, FavBegin, FavEntry, FavEnd,
                WindowShow, WindowHide, WindowRect, MidiRoute, MidiQuery, Unknown } kind = Unknown;
    juce::String path;
    int start = 0, count = 0;
    float gainDb = 0.0f;
    Favorite fav;
    int rx = 0, ry = 0, rw = 0, rh = 0;
    bool movable = false;
    bool clamp = false;
    std::array<int, 16> route {};   // MidiRoute: target output per input channel (0 = drop)
};
```

- [ ] **Step 4: Add the parse branches** — in `parseCommand` (`src/CustosOscServer.cpp`), after the `/custos/window/rect` block:

```cpp
    if (addr == "/custos/midi/route")
    {
        if (msg.size() == 16)
        {
            Command c; c.kind = Command::MidiRoute;
            for (int i = 0; i < 16; ++i)
            {
                if (! msg[i].isInt32()) return { Command::Unknown, {} };
                c.route[(size_t) i] = msg[i].getInt32();
            }
            return c;
        }
        return { Command::Unknown, {} };
    }
    if (addr == "/custos/midi/query")
        return { Command::MidiQuery, {} };
```

- [ ] **Step 5: Run to verify it passes**

Run: `./build/tests/custos_tests "*midi/route*" "*midi/query*"`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/CustosOscServer.h src/CustosOscServer.cpp tests/OscCommandTest.cpp
git commit -m "Contract v2: parse /custos/midi/route (16 ints) + /custos/midi/query"
```

---

### Task 5: Add the buildMidiRoute feedback builder

**Files:**
- Modify: `src/OscContract.h` (add builder + `#include <array>`)
- Test: `tests/OscContractTest.cpp`

**Interfaces:**
- Produces: `buildMidiRoute(int n, const std::array<int,16>& route)` → `/custos/midi/route` with 17 args (leading `n`).

- [ ] **Step 1: Write the failing test** — append to `tests/OscContractTest.cpp`:

```cpp
TEST_CASE ("buildMidiRoute carries N first, then 16 targets")
{
    std::array<int, 16> r {}; for (int i = 0; i < 16; ++i) r[(size_t) i] = i + 1;
    const auto m = buildMidiRoute (9, r);
    REQUIRE (m.getAddressPattern().toString() == "/custos/midi/route");
    REQUIRE (m.size() == 17);
    REQUIRE (m[0].getInt32() == 9);
    REQUIRE (m[1].getInt32() == 1);
    REQUIRE (m[16].getInt32() == 16);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target custos_tests`
Expected: FAIL — `buildMidiRoute` undeclared.

- [ ] **Step 3: Add the builder** — `src/OscContract.h` (add `#include <array>` at top; builder near `buildWindowRect`):

```cpp
// MIDI route feedback (Custos -> KM). N first, then 16 targets (input i -> value; 0 = dropped).
// Same address as the inbound verb; the 17-arg form with leading N marks it a report.
inline juce::OSCMessage buildMidiRoute (int n, const std::array<int, 16>& route)
{
    juce::OSCMessage m ("/custos/midi/route");
    m.addInt32 (n);
    for (int t : route) m.addInt32 (t);
    return m;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `./build/tests/custos_tests "buildMidiRoute*"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/OscContract.h tests/OscContractTest.cpp
git commit -m "Contract v2: add buildMidiRoute feedback builder"
```

---

### Task 6: Pure applyMidiRoute() function

**Files:**
- Create: `src/MidiRoute.h`, `src/MidiRoute.cpp`
- Modify: `CMakeLists.txt:18-28` (add `src/MidiRoute.cpp` to `custos_core`)
- Create: `tests/MidiRouteTest.cpp`; Modify: `tests/CMakeLists.txt:8-22`

**Interfaces:**
- Produces: `void custos::applyMidiRoute(juce::MidiBuffer& midi, const std::array<uint8_t,16>& route, juce::MidiBuffer& scratch)` — rewrites/drops channel-voice messages per `route`; `route[ch-1] == 0` drops, else remaps the channel; non-channel messages pass through; result left in `midi` (via `swapWith(scratch)`), no allocation after warmup.

- [ ] **Step 1: Write the failing test** — `tests/MidiRouteTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "MidiRoute.h"
#include <array>

using namespace custos;

static std::array<uint8_t, 16> identity()
{
    std::array<uint8_t, 16> r {}; for (int i = 0; i < 16; ++i) r[(size_t) i] = (uint8_t) (i + 1); return r;
}

TEST_CASE ("applyMidiRoute passes identity unchanged")
{
    juce::MidiBuffer midi, scratch;
    midi.addEvent (juce::MidiMessage::noteOn (8, 60, (juce::uint8) 100), 0);
    applyMidiRoute (midi, identity(), scratch);
    bool found = false;
    for (const auto meta : midi) if (meta.getMessage().getChannel() == 8) found = true;
    REQUIRE (found);
}

TEST_CASE ("applyMidiRoute remaps input channel to target")
{
    auto r = identity(); r[7] = 1;   // input ch 8 -> out ch 1
    juce::MidiBuffer midi, scratch;
    midi.addEvent (juce::MidiMessage::noteOn (8, 60, (juce::uint8) 100), 0);
    applyMidiRoute (midi, r, scratch);
    for (const auto meta : midi) REQUIRE (meta.getMessage().getChannel() == 1);
}

TEST_CASE ("applyMidiRoute drops a channel mapped to 0")
{
    auto r = identity(); r[8] = 0;   // input ch 9 dropped
    juce::MidiBuffer midi, scratch;
    midi.addEvent (juce::MidiMessage::noteOn (9, 60, (juce::uint8) 100), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    applyMidiRoute (midi, r, scratch);
    int n = 0; for (const auto meta : midi) { juce::ignoreUnused (meta); ++n; }
    REQUIRE (n == 1);   // ch9 dropped, ch1 kept
}

TEST_CASE ("applyMidiRoute passes non-channel messages through")
{
    juce::MidiBuffer midi, scratch;
    midi.addEvent (juce::MidiMessage::midiClock(), 0);
    applyMidiRoute (midi, identity(), scratch);
    int n = 0; for (const auto meta : midi) { juce::ignoreUnused (meta); ++n; }
    REQUIRE (n == 1);
}
```

- [ ] **Step 2: Register the new files** — add `src/MidiRoute.cpp` to `custos_core` (`CMakeLists.txt`) and `MidiRouteTest.cpp` to `custos_tests` (`tests/CMakeLists.txt`).

- [ ] **Step 3: Run to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target custos_tests`
Expected: FAIL — `MidiRoute.h` not found.

- [ ] **Step 4: Write the header** — `src/MidiRoute.h`:

```cpp
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cstdint>

namespace custos
{
// Rewrite `midi` in place per `route`: for each channel-voice message on input channel c (1..16),
// route[c-1] == 0 drops it, else remaps the channel to route[c-1]. Non-channel messages pass through.
// `scratch` is a caller-owned reusable buffer (avoids audio-thread allocation); its contents are clobbered.
void applyMidiRoute (juce::MidiBuffer& midi, const std::array<std::uint8_t, 16>& route, juce::MidiBuffer& scratch);
}
```

- [ ] **Step 5: Write the implementation** — `src/MidiRoute.cpp`:

```cpp
#include "MidiRoute.h"

namespace custos
{
void applyMidiRoute (juce::MidiBuffer& midi, const std::array<std::uint8_t, 16>& route, juce::MidiBuffer& scratch)
{
    scratch.clear();   // keeps capacity across blocks -> no per-block allocation after warmup
    for (const auto meta : midi)
    {
        auto m = meta.getMessage();
        const int ch = m.getChannel();               // 1..16 for channel-voice; 0 otherwise
        if (ch > 0)
        {
            const int t = route[(size_t) (ch - 1)];
            if (t == 0) continue;                    // dropped
            if (t != ch) m.setChannel (t);           // remapped
        }
        scratch.addEvent (m, meta.samplePosition);
    }
    midi.swapWith (scratch);
}
}
```

- [ ] **Step 6: Run to verify it passes**

Run: `./build/tests/custos_tests "applyMidiRoute*"`
Expected: PASS (4 cases).

- [ ] **Step 7: Commit**

```bash
git add src/MidiRoute.h src/MidiRoute.cpp CMakeLists.txt tests/MidiRouteTest.cpp tests/CMakeLists.txt
git commit -m "Contract v2: pure applyMidiRoute() (remap/drop/passthrough)"
```

---

### Task 7: Processor route state + processBlock wiring

**Files:**
- Modify: `src/CustosProcessor.h` (member, methods, `#include <array>`)
- Modify: `src/CustosProcessor.cpp` (ctor default, `processBlock`, `setMidiRoute`/`getMidiRoute`)
- Test: `tests/MidiRouteTest.cpp` (round-trip)

**Interfaces:**
- Consumes: `applyMidiRoute()` (Task 6).
- Produces: `void CustosProcessor::setMidiRoute(const std::array<int,16>&)`; `std::array<int,16> CustosProcessor::getMidiRoute() const`; default = identity; applied in `processBlock` before `inner->processBlock`.

- [ ] **Step 1: Write the failing test** — append to `tests/MidiRouteTest.cpp`:

```cpp
#include "CustosProcessor.h"

TEST_CASE ("CustosProcessor MIDI route defaults to identity and round-trips")
{
    custos::CustosProcessor proc;
    auto def = proc.getMidiRoute();
    REQUIRE (def[0] == 1);
    REQUIRE (def[15] == 16);

    std::array<int, 16> r {}; for (int i = 0; i < 16; ++i) r[(size_t) i] = 16 - i;
    proc.setMidiRoute (r);
    REQUIRE (proc.getMidiRoute() == r);

    std::array<int, 16> bad {}; bad[0] = 99; bad[1] = -1;   // clamped to 0..16
    proc.setMidiRoute (bad);
    REQUIRE (proc.getMidiRoute()[0] == 16);
    REQUIRE (proc.getMidiRoute()[1] == 0);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target custos_tests`
Expected: FAIL — `setMidiRoute`/`getMidiRoute` undeclared.

- [ ] **Step 3: Declare state + methods** — `src/CustosProcessor.h`: add `#include <array>`; in the public section add:

```cpp
    // MIDI channel routing (message thread writes, audio thread reads). Persisted (state v3).
    void setMidiRoute (const std::array<int, 16>& route);   // values clamped to 0..16
    std::array<int, 16> getMidiRoute() const;
```
In the private section add:
```cpp
    std::array<std::atomic<std::uint8_t>, 16> midiRoute;   // target output per input channel; 0 = drop
    juce::MidiBuffer routeScratch;                          // reused by applyMidiRoute (no RT alloc)
```

- [ ] **Step 4: Initialise identity in the ctor** — in `CustosProcessor::CustosProcessor(...)` (`src/CustosProcessor.cpp`, near line 18 where the facade is built):

```cpp
    for (int i = 0; i < 16; ++i) midiRoute[(size_t) i].store ((std::uint8_t) (i + 1), std::memory_order_relaxed);
```

- [ ] **Step 5: Implement the accessors** — add to `src/CustosProcessor.cpp`:

```cpp
void CustosProcessor::setMidiRoute (const std::array<int, 16>& route)
{
    for (int i = 0; i < 16; ++i)
        midiRoute[(size_t) i].store ((std::uint8_t) juce::jlimit (0, 16, route[(size_t) i]),
                                     std::memory_order_relaxed);
}

std::array<int, 16> CustosProcessor::getMidiRoute() const
{
    std::array<int, 16> out {};
    for (int i = 0; i < 16; ++i) out[(size_t) i] = midiRoute[(size_t) i].load (std::memory_order_relaxed);
    return out;
}
```

- [ ] **Step 6: Apply in processBlock** — `src/CustosProcessor.cpp:203-213`, snapshot the atomics then route before the inner call:

```cpp
void CustosProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    std::array<std::uint8_t, 16> snap {};
    for (int i = 0; i < 16; ++i) snap[(size_t) i] = midiRoute[(size_t) i].load (std::memory_order_relaxed);
    applyMidiRoute (midi, snap, routeScratch);

    {
        const juce::SpinLock::ScopedTryLockType tl (swapLock);
        if (tl.isLocked() && inner != nullptr)
            inner->processBlock (buffer, midi);
        else
            buffer.clear();
    }
    buffer.applyGain (masterGain.load());
}
```
Add `#include "MidiRoute.h"` to `src/CustosProcessor.cpp`.

- [ ] **Step 7: Run to verify it passes**

Run: `./build/tests/custos_tests "CustosProcessor MIDI route*" "[passthrough]"`
Expected: PASS (route round-trip + existing passthrough still green).

- [ ] **Step 8: Commit**

```bash
git add src/CustosProcessor.h src/CustosProcessor.cpp tests/MidiRouteTest.cpp
git commit -m "Contract v2: processor MIDI route state + processBlock application"
```

---

### Task 8: Persist the route map in state (format v3)

**Files:**
- Modify: `src/StateCodec.h` (signature + `PersistedState`), `src/StateCodec.cpp`
- Modify: `src/CustosProcessor.cpp:309-335` (`getStateInformation`/`setStateInformation`)
- Test: `tests/StateCodecTest.cpp`

**Interfaces:**
- Consumes: `getMidiRoute()`/`setMidiRoute()` (Task 7).
- Produces: `serializeState(path, innerState, identityN, const std::array<std::uint8_t,16>& route)`; `PersistedState.route` (`std::array<std::uint8_t,16>`, identity default for v1/v2).

- [ ] **Step 1: Write the failing test** — append to `tests/StateCodecTest.cpp`:

```cpp
TEST_CASE ("state v3 round-trips the MIDI route map")
{
    std::array<std::uint8_t, 16> r {}; for (int i = 0; i < 16; ++i) r[(size_t) i] = (std::uint8_t) (16 - i);
    juce::MemoryBlock inner; inner.append ("xyz", 3);
    const auto blob = custos::serializeState ("C:/x/Diva.vst3", inner, 7, r);

    custos::PersistedState ps;
    REQUIRE (custos::parseState (blob.getData(), (int) blob.getSize(), ps));
    REQUIRE (ps.identityN == 7);
    REQUIRE (ps.route == r);
}

TEST_CASE ("v2 blob parses with identity route (back-compat)")
{
    // Build a v2 blob by hand: CUS1, ver=2, path, inner, identityN — no route bytes.
    juce::MemoryBlock mb; juce::MemoryOutputStream os (mb, false);
    os.write ("CUS1", 4); os.writeByte (2);
    const char* p = "C:/a.vst3"; const int pl = (int) std::strlen (p);
    os.writeInt (pl); os.write (p, (size_t) pl);
    os.writeInt (0); os.writeInt (4); os.flush();   // empty inner, identityN=4

    custos::PersistedState ps;
    REQUIRE (custos::parseState (mb.getData(), (int) mb.getSize(), ps));
    REQUIRE (ps.identityN == 4);
    for (int i = 0; i < 16; ++i) REQUIRE (ps.route[(size_t) i] == (std::uint8_t) (i + 1));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target custos_tests`
Expected: FAIL — `serializeState` 4-arg form / `PersistedState.route` undeclared.

- [ ] **Step 3: Extend the struct + signature** — `src/StateCodec.h`:

```cpp
#include <array>
#include <cstdint>
struct PersistedState
{
    juce::String path; juce::MemoryBlock innerState; int identityN = 0;
    std::array<std::uint8_t, 16> route { {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16} };
};
juce::MemoryBlock serializeState (const juce::String& path, const juce::MemoryBlock& innerState,
                                  int identityN, const std::array<std::uint8_t, 16>& route);
```

- [ ] **Step 4: Write v3, parse v1/v2/v3** — `src/StateCodec.cpp`:

In `serializeState` add the `route` param, write version **3**, and after `os.writeInt(identityN);`:
```cpp
    os.write (route.data(), route.size());   // 16 bytes
```
Change `os.writeByte (2);` → `os.writeByte (3);`.

In `parseState`, accept version 3 and read the route when present:
```cpp
    if (version != 1 && version != 2 && version != 3) return false;
    // ... existing path/inner/identityN reads (identityN guarded by version >= 2) ...
    std::array<std::uint8_t, 16> route { {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16} };
    if (version >= 3)
    {
        if (is.getNumBytesRemaining() < 16) return false;
        is.read (route.data(), 16);
    }
    out.route = route;
```

- [ ] **Step 5: Wire into the processor state** — `src/CustosProcessor.cpp`:

`getStateInformation` (line ~314): build the route array and pass it:
```cpp
    std::array<std::uint8_t, 16> route {};
    for (int i = 0; i < 16; ++i) route[(size_t) i] = (std::uint8_t) getMidiRoute()[(size_t) i];
    dest = serializeState (currentSynthPath, innerChunk, identityN, route);
```
`setStateInformation` (after `identityN = ps.identityN;`):
```cpp
    { std::array<int, 16> r {}; for (int i = 0; i < 16; ++i) r[(size_t) i] = ps.route[(size_t) i]; setMidiRoute (r); }
```

- [ ] **Step 6: Run to verify it passes**

Run: `./build/tests/custos_tests "[statecodec]" "state v3*" "v2 blob*"`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/StateCodec.h src/StateCodec.cpp src/CustosProcessor.cpp tests/StateCodecTest.cpp
git commit -m "Contract v2: persist MIDI route map (state format v3, v1/v2 back-compat)"
```

---

### Task 9: OSC apply — route set/query + feedback

**Files:**
- Modify: `src/CustosProcessor.h`/`.cpp` (add `emitMidiRoute()`)
- Modify: `src/CustosOscServer.cpp:150-196` (switch cases)
- Test: `tests/OscCommandTest.cpp` (or a small integration test capturing `outboundSink`)

**Interfaces:**
- Consumes: `Command::MidiRoute`/`MidiQuery` (Task 4), `buildMidiRoute` (Task 5), `setMidiRoute`/`getMidiRoute` (Task 7).
- Produces: `CustosProcessor::emitMidiRoute()` — sends `buildMidiRoute(identityN, getMidiRoute())` via `outboundSink` (no-op if null).

- [ ] **Step 1: Write the failing test** — append to `tests/OscCommandTest.cpp` (capture emissions via `outboundSink`):

```cpp
TEST_CASE ("emitMidiRoute sends the current map with N first")
{
    custos::CustosProcessor proc;
    proc.setIdentity (9);
    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    std::array<int, 16> r {}; for (int i = 0; i < 16; ++i) r[(size_t) i] = i == 7 ? 1 : 0;
    proc.setMidiRoute (r);
    proc.emitMidiRoute();

    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/midi/route");
    REQUIRE (sent[0].size() == 17);
    REQUIRE (sent[0][0].getInt32() == 9);
    REQUIRE (sent[0][8].getInt32() == 1);   // input ch 8 (arg index 8) -> out 1
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target custos_tests`
Expected: FAIL — `emitMidiRoute` undeclared.

- [ ] **Step 3: Add emitMidiRoute** — `src/CustosProcessor.h` (public) `void emitMidiRoute();`; `src/CustosProcessor.cpp`:

```cpp
void CustosProcessor::emitMidiRoute()
{
    if (outboundSink) outboundSink (buildMidiRoute (identityN, getMidiRoute()));
}
```

- [ ] **Step 4: Handle the commands** — `src/CustosOscServer.cpp`, add cases before `Command::Unknown`:

```cpp
        case Command::MidiRoute:
        {
            std::array<int, 16> r {}; for (int i = 0; i < 16; ++i) r[(size_t) i] = cmd.route[(size_t) i];
            proc.setMidiRoute (r);
            proc.emitMidiRoute();   // confirm the applied map
            break;
        }
        case Command::MidiQuery:
            proc.emitMidiRoute();
            break;
```

- [ ] **Step 5: Run to verify it passes**

Run: `./build/tests/custos_tests "emitMidiRoute*"`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/CustosProcessor.h src/CustosProcessor.cpp src/CustosOscServer.cpp tests/OscCommandTest.cpp
git commit -m "Contract v2: OSC apply for /custos/midi/route + /custos/midi/query with feedback"
```

---

### Task 10: Facade-size build ladder

**Files:**
- Modify: `src/Config.h:6-8` (make `kFacadeParamCount` overridable)
- Modify: `CMakeLists.txt:58-79` (replace the single `juce_add_plugin(Custos ...)` with a `custos_variant()` function over the ladder)
- Modify: `docs/osc-contract.md` (fold in v2 — see Task 11)

**Interfaces:**
- Produces: N VST3 targets (`Custos 1000` … `Custos 10000`) reporting distinct `facadeCap`; default build (no override) stays 5000.

- [ ] **Step 1: Make the constant overridable** — `src/Config.h`:

```cpp
    #ifndef CUSTOS_FACADE_PARAM_COUNT
     #define CUSTOS_FACADE_PARAM_COUNT 5000
    #endif
    inline constexpr int kFacadeParamCount = CUSTOS_FACADE_PARAM_COUNT;
```

- [ ] **Step 2: Add a variant function + ladder** — replace the `juce_add_plugin(Custos ...)` block in `CMakeLists.txt` with:

```cmake
# One target per facade rung. Same sources; differ only in kFacadeParamCount + VST3 id + name.
function(custos_variant SIZE CODE)
    set(tgt "Custos${SIZE}")
    juce_add_plugin(${tgt}
        PRODUCT_NAME "Custos ${SIZE}"
        COMPANY_NAME "Kapellmeister"
        PLUGIN_MANUFACTURER_CODE Kplm
        PLUGIN_CODE ${CODE}
        FORMATS VST3
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Instrument Synth)
    target_sources(${tgt} PRIVATE src/PluginEntry.cpp)
    target_compile_definitions(${tgt} PRIVATE CUSTOS_FACADE_PARAM_COUNT=${SIZE})
    target_link_libraries(${tgt} PRIVATE custos_core juce::juce_recommended_config_flags)
    target_compile_definitions(${tgt} PUBLIC
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0)
endfunction()

# PLUGIN_CODE must be unique per variant (4 chars). Ladder locked in the design spec.
custos_variant(1000  Cs10)
custos_variant(2000  Cs20)
custos_variant(3000  Cs30)
custos_variant(4000  Cs40)
custos_variant(5000  Cs50)
custos_variant(10000 CsX0)
```

**Note:** `kFacadeParamCount` lives in `custos_core` (shared static lib), so its constant is fixed once per build of the lib. Because each variant needs a *different* `CUSTOS_FACADE_PARAM_COUNT`, move the facade-count define onto the per-variant target and ensure `custos_core` reads it — simplest: compile `src/CustosProcessor.cpp` **into each plugin target** rather than the shared lib, OR keep `custos_core` header-only for `Config.h` (it already is — `Config.h` is included, not compiled). Since `Config.h` is header-only and `kFacadeParamCount` is `constexpr`, the value is baked into each *translation unit that includes it*. Verify `CustosProcessor.cpp` (which builds the facade) picks up the per-target define: if `CustosProcessor.cpp` is in `custos_core`, the define must be on `custos_core`, which can't vary per target. **Therefore:** move `CustosProcessor.cpp` (and any TU that reads `kFacadeParamCount`) out of `custos_core` into a per-variant `target_sources`, keeping only the synth/binding/codec helpers in `custos_core`. Confirm with `grep -rl kFacadeParamCount src/` (expect `CustosProcessor.cpp` + `Config.h`) and place exactly those TUs per-target.

- [ ] **Step 3: Restructure so the count define is per-target** — per the note: in `CMakeLists.txt`, remove `src/CustosProcessor.cpp` from the `custos_core` `add_library(...)` list and add it to each variant via `target_sources(${tgt} PRIVATE src/PluginEntry.cpp src/CustosProcessor.cpp)`. Keep `custos_core` for the count-independent helpers. Note `custos_tests` links `custos_core` and also needs `CustosProcessor.cpp` at the default 5000 → add `src/CustosProcessor.cpp` to the `custos_tests` sources in `tests/CMakeLists.txt` (it will compile with the default `CUSTOS_FACADE_PARAM_COUNT`).

- [ ] **Step 4: Configure + build two rungs to verify**

Run:
```bash
cmake -S . -B build
cmake --build build --target Custos2000 Custos10000 custos_tests
```
Expected: all three link; `custos_tests` still passes (`./build/tests/custos_tests`). The default-5000 test binary confirms the constant plumbing didn't regress.

- [ ] **Step 5: Commit**

```bash
git add src/Config.h CMakeLists.txt tests/CMakeLists.txt
git commit -m "Contract v2: facade-size build ladder (1000..10000 variants)"
```

---

### Task 11: Update the wire-contract doc

**Files:**
- Modify: `docs/osc-contract.md`

**Interfaces:** none (documentation).

- [ ] **Step 1: Fold v2 into the contract** — in `docs/osc-contract.md`: set `protoVer = 2`; update the `/custos/here`, `/custos/loaded`, `/custos/param` rows with the appended fields; add `/custos/midi/route` (in: 16 ints) and `/custos/midi/query` to the KM→Custos table; add the `/custos/midi/route` feedback (N + 16 ints) to the Custos→KM table; note state format v3 and the facade ladder / `facadeCap` semantics in §4.

- [ ] **Step 2: Commit**

```bash
git add docs/osc-contract.md
git commit -m "Contract v2: update osc-contract.md wire tables"
```

---

## Self-Review

**Spec coverage:**
- §3 facadeCap/innerTotal → Task 2 ✅
- §4 MIDI matrix (wire in/query/feedback) → Tasks 4, 5, 9 ✅; (apply) Task 6, 7 ✅; (persist v3) Task 8 ✅
- §5 richer param dump → Task 3 ✅
- §6 facade ladder → Task 10 ✅
- §7 protoVer bump + doc → Task 1, Task 11 ✅
- Already-merged brand/window → out of scope here (KM-side consumption; Custos already implements them).

**Placeholder scan:** Task 3 Step 5 and Task 11 describe edits against files whose exact current contents the implementer must read (ParamDumpTest assertions; the contract-doc tables) rather than pasting a full rewrite — acceptable (targeted edits to read-first files), not code placeholders. All code steps show complete code.

**Type consistency:** `route` is `std::array<int,16>` at the OSC/Command boundary and `std::array<std::uint8_t,16>` in the hot path/state; conversions are explicit at Tasks 7/8/9. `applyMidiRoute` signature matches across Tasks 6/7. `buildMidiRoute(int, std::array<int,16>)` matches Tasks 5/9. `serializeState` 4-arg matches Tasks 8. `facadeSize()`/`innerParamTotal()`/`boundParamCount()` names match the header.

**Risk note (Task 10):** the per-target `kFacadeParamCount` requires moving `CustosProcessor.cpp` out of the shared `custos_core` lib — Step 3 handles this and re-adds it to `custos_tests`. If any other TU is found to read `kFacadeParamCount` (`grep -rl` in Step 2's note), it must move per-target too.
