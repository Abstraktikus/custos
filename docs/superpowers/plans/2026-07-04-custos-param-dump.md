# Custos Param Dump Implementation Plan (Phase C, Feature F1)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let Kapellmeister read a Custos instance's currently-bound parameters directly over OSC (`/custos/params <start> <count>` → a stream of `/custos/param` + a `/custos/params/done` marker), so KM can bind macros without going through the GP `PList` harness.

**Architecture:** A pure dispatch (`parseCommand`) maps `/custos/params <start> <count>` to a `Command`; the processor's `dumpParams(start, count)` reads the **bound** facade params (`0..boundCount`, clamped), emits one `/custos/param <N> <idx> <val> <name>` each via the existing `outboundSink`, then a `/custos/params/done <N> <start> <sent>`. Message bodies are pure builders (unit-testable without a socket); the OSC server is a thin adapter that routes the command to `dumpParams`.

**Tech Stack:** C++17, CMake, JUCE 8.0.14 (`juce_osc`), Catch2 v3.15.1, VST3 (Windows). Builds on the addressing core (`master`): `outboundSink`, `OscContract.h`, `identity()`, the `Command`/`parseCommand` seam.

## Global Constraints

- **Contract:** `docs/osc-contract.md` v1 (`protoVer = 1`). This plan implements only **F1 param dump**.
- **Inbound:** `/custos/params <start:int> <count:int>` (KM → `127.0.0.1:BASE+N`).
- **Outbound (each carries `N` first, to hub `127.0.0.1:8000`):** `/custos/param <N:int> <idx:int> <val:float> <name:string>` per param, then `/custos/params/done <N:int> <start:int> <count:int>`.
- **Bound-only:** dump only the `0..boundCount` mirrored params (NOT the full 5000 facade).
- **Clamp:** the requested range is clamped to `[0, boundCount)`. `/custos/params/done` reports the **actually-sent** count (not an echo of the request); `start` **echoes the request**.
- **No ack:** the stream + done marker IS the response (no separate `/custos/ack`).
- **Emission via `outboundSink`** (null in unit tests → dump is a no-op there; asserted via a capturing sink).
- **Build the `Custos_VST3` target.** Build/test from PowerShell: `.\scripts\ci.cmd`. Autonomous OSC tool: the pythonosc sender in the scratchpad + `tools/kmplugin.py`.

---

## File Structure

```
src/
  OscContract.h          # MODIFY: add buildParam(), buildParamsDone()
  CustosOscServer.h      # MODIFY: Command gains Params kind + start/count fields
  CustosOscServer.cpp    # MODIFY: parseCommand maps /custos/params; oscMessageReceived routes Params -> dumpParams
  CustosProcessor.h/.cpp # MODIFY: void dumpParams(int start, int count)
tests/
  OscContractTest.cpp    # MODIFY: buildParam/buildParamsDone + parseCommand(params) tests
  ParamDumpTest.cpp      # NEW: dumpParams streaming + clamping (capturing sink)
tests/CMakeLists.txt     # MODIFY: add ParamDumpTest.cpp
```

---

## Task 1: Pure dump helpers + parseCommand(/custos/params)

**Files:**
- Modify: `src/OscContract.h`, `src/CustosOscServer.h`, `src/CustosOscServer.cpp`
- Modify: `tests/OscContractTest.cpp`

**Interfaces:**
- Produces: `juce::OSCMessage custos::buildParam(int n, int idx, float val, const juce::String& name)` (address `/custos/param`);
  `juce::OSCMessage custos::buildParamsDone(int n, int start, int count)` (address `/custos/params/done`);
  `custos::Command` gains `Params` and `int start, count`; `parseCommand` maps `/custos/params <start:int> <count:int>` (both int args required, else `Unknown`).

- [ ] **Step 1: Write the failing tests — append to tests/OscContractTest.cpp**

```cpp
TEST_CASE ("buildParam and buildParamsDone carry N first")
{
    const auto p = buildParam (7, 5, 0.5f, "Cutoff");
    REQUIRE (p.getAddressPattern().toString() == "/custos/param");
    REQUIRE (p[0].getInt32() == 7);
    REQUIRE (p[1].getInt32() == 5);
    REQUIRE (p[3].getString() == "Cutoff");

    const auto d = buildParamsDone (7, 0, 3);
    REQUIRE (d.getAddressPattern().toString() == "/custos/params/done");
    REQUIRE (d[0].getInt32() == 7);
    REQUIRE (d[1].getInt32() == 0);
    REQUIRE (d[2].getInt32() == 3);
}

TEST_CASE ("parseCommand maps /custos/params with start and count")
{
    juce::OSCMessage msg ("/custos/params", 10, 50);
    const auto c = parseCommand (msg);
    REQUIRE (c.kind == Command::Params);
    REQUIRE (c.start == 10);
    REQUIRE (c.count == 50);
}

TEST_CASE ("parseCommand rejects /custos/params without two ints")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/params")).kind == Command::Unknown);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/params", 10)).kind == Command::Unknown);
}
```

- [ ] **Step 2: Run to verify failure**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: FAIL — `buildParam`/`buildParamsDone` undefined; `Command::Params`, `.start`, `.count` don't exist.

- [ ] **Step 3: Add the builders — src/OscContract.h**

After `buildLoaded` (before the closing `}` of `namespace custos`), add:
```cpp
inline juce::OSCMessage buildParam (int n, int idx, float val, const juce::String& name)
{
    return juce::OSCMessage ("/custos/param", n, idx, val, name);
}

inline juce::OSCMessage buildParamsDone (int n, int start, int count)
{
    return juce::OSCMessage ("/custos/params/done", n, start, count);
}
```

- [ ] **Step 4: Extend Command — src/CustosOscServer.h**

Replace:
```cpp
struct Command { enum Kind { Load, Clear, Hello, Unknown } kind = Unknown; juce::String path; };
```
with:
```cpp
struct Command
{
    enum Kind { Load, Clear, Hello, Params, Unknown } kind = Unknown;
    juce::String path;
    int start = 0, count = 0;   // Params
};
```

- [ ] **Step 5: Map /custos/params — src/CustosOscServer.cpp**

In `parseCommand`, before the final `return { Command::Unknown, {} };`, add:
```cpp
    if (addr == "/custos/params")
    {
        if (msg.size() >= 2 && msg[0].isInt32() && msg[1].isInt32())
            return { Command::Params, {}, msg[0].getInt32(), msg[1].getInt32() };
        return { Command::Unknown, {} };
    }
```

- [ ] **Step 6: Build + test**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: the three new tests PASS; build pristine.

- [ ] **Step 7: Commit**

```bash
git add src/OscContract.h src/CustosOscServer.h src/CustosOscServer.cpp tests/OscContractTest.cpp
git commit -m "Param dump: pure builders + parseCommand(/custos/params)"
```

---

## Task 2: CustosProcessor::dumpParams

**Files:**
- Modify: `src/CustosProcessor.h`, `src/CustosProcessor.cpp`
- Create: `tests/ParamDumpTest.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `outboundSink`, `identity()`/`identityN`, `boundCount`, the `facade` vector, `buildParam`/`buildParamsDone` (Task 1).
- Produces: `void CustosProcessor::dumpParams(int start, int count)` — emits `/custos/param` for each bound param in the clamped range, then `/custos/params/done` with the actually-sent count (start echoed). No-op if `outboundSink` is null.

- [ ] **Step 1: Write the failing tests — tests/ParamDumpTest.cpp**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"
#include <vector>

using namespace custos;

TEST_CASE ("dumpParams streams bound params then a done marker, clamped to boundCount")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setIdentity (7);                                                       // no OSC bind (enableOsc=false)
    proc.attachInner (std::make_unique<custos::test::FakeInnerProcessor> (3));  // boundCount = 3

    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.dumpParams (0, 10);   // over-request -> clamps to [0,3)

    REQUIRE (sent.size() == 4);                                     // 3 params + 1 done
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/param");
    REQUIRE (sent[0][0].getInt32() == 7);                          // N
    REQUIRE (sent[0][1].getInt32() == 0);                          // idx
    REQUIRE (sent[0][3].getString() == "Fake 0");                  // name (mirrored from the fake inner)
    REQUIRE (sent[3].getAddressPattern().toString() == "/custos/params/done");
    REQUIRE (sent[3][0].getInt32() == 7);                          // N
    REQUIRE (sent[3][1].getInt32() == 0);                          // start echoed
    REQUIRE (sent[3][2].getInt32() == 3);                          // actually sent
}

TEST_CASE ("dumpParams past boundCount sends only the done marker with count 0")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setIdentity (7);
    proc.attachInner (std::make_unique<custos::test::FakeInnerProcessor> (3));

    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.dumpParams (5, 4);    // start >= boundCount
    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/params/done");
    REQUIRE (sent[0][1].getInt32() == 5);   // start echoed
    REQUIRE (sent[0][2].getInt32() == 0);   // nothing sent
}

TEST_CASE ("dumpParams honours a sub-range")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setIdentity (2);
    proc.attachInner (std::make_unique<custos::test::FakeInnerProcessor> (5));  // boundCount = 5

    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.dumpParams (1, 2);    // [1,3)
    REQUIRE (sent.size() == 3);                          // 2 params + done
    REQUIRE (sent[0][1].getInt32() == 1);
    REQUIRE (sent[1][1].getInt32() == 2);
    REQUIRE (sent[2].getAddressPattern().toString() == "/custos/params/done");
    REQUIRE (sent[2][2].getInt32() == 2);                // sent
}
```

- [ ] **Step 2: Register the test + run to verify failure**

Add `ParamDumpTest.cpp` to the `add_executable(custos_tests ...)` list in `tests/CMakeLists.txt`.
Run (PowerShell): `.\scripts\ci.cmd`
Expected: FAIL — `dumpParams` is not a member of `CustosProcessor`.

- [ ] **Step 3: Declare dumpParams — src/CustosProcessor.h**

In the public section, right after the `setIdentity`/`identity`/`modeString` block (before `outboundSink`), add:
```cpp
    // F1: stream the bound params in [start, start+count) (clamped to boundCount) via outboundSink,
    // then a /custos/params/done. Message thread. No-op if outboundSink is null.
    void dumpParams (int start, int count);
```

- [ ] **Step 4: Define dumpParams — src/CustosProcessor.cpp**

Add after `emitLoaded()` (anywhere in the `custos` namespace):
```cpp
void CustosProcessor::dumpParams (int start, int count)
{
    if (! outboundSink) return;

    const int from = juce::jmax (0, start);
    const int to   = juce::jmin (boundCount, from + juce::jmax (0, count));
    int sent = 0;
    for (int i = from; i < to; ++i)
    {
        outboundSink (buildParam (identityN, i, facade[(size_t) i]->getValue(),
                                  facade[(size_t) i]->getName (128)));
        ++sent;
    }
    outboundSink (buildParamsDone (identityN, start, sent));
}
```

- [ ] **Step 5: Build + test**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: all tests PASS (addressing-core + the 3 new ParamDump tests). Build pristine.

- [ ] **Step 6: Commit**

```bash
git add src/CustosProcessor.h src/CustosProcessor.cpp tests/ParamDumpTest.cpp tests/CMakeLists.txt
git commit -m "Param dump: CustosProcessor::dumpParams (bound-only, clamped, N-tagged)"
```

---

## Task 3: Route /custos/params through the OSC server

**Files:**
- Modify: `src/CustosOscServer.cpp`

**Interfaces:**
- Consumes: `parseCommand` (`Command::Params`, `start`, `count`), `CustosProcessor::dumpParams` (Task 2).
- Produces: on a `/custos/params` message the server calls `proc.dumpParams(cmd.start, cmd.count)` (no ack — the stream is the response).

- [ ] **Step 1: Handle Params — src/CustosOscServer.cpp**

In `oscMessageReceived`, add a case before `case Command::Unknown:`:
```cpp
        case Command::Params:
            proc.dumpParams (cmd.start, cmd.count);
            break;
```

- [ ] **Step 2: Build + test**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: all tests PASS (count unchanged — this is transport, verified by the Task 4 E2E). Build pristine, `Custos.vst3` produced.

- [ ] **Step 3: Commit**

```bash
git add src/CustosOscServer.cpp
git commit -m "Param dump: route /custos/params -> dumpParams"
```

---

## Task 4: Autonomous E2E

**Goal:** prove the dump end-to-end against a live GP. Record in `experiments/param-dump.md`.

**Setup:** deploy the build, restart GP, set an identity `N` in the Custos UI (so it binds `BASE+N`), load a known synth. A pythonosc helper sends `/custos/params <start> <count>` to `127.0.0.1:BASE+N` and listens on the hub `:8000`.

- [ ] **Step 1: Deploy + bind an instance**

`.\scripts\deploy.cmd` → restart GP (relaunch the currently-open gig; path from the GP process command line) → set `N` (e.g. 9) in the Custos editor → confirm `/custos/hello :9109` → `/custos/here [9,…]`.

- [ ] **Step 2: Dump a range and verify the stream + done**

Send `/custos/params 0 8` → `:9109`. Expected on the hub: eight `/custos/param [9, idx, val, name]` (idx 0..7, names matching the loaded synth's params) followed by `/custos/params/done [9, 0, 8]`.

- [ ] **Step 3: Verify clamping**

With a synth whose `boundCount = C`, send `/custos/params 0 100000` → expect exactly `C` `/custos/param` messages + `/custos/params/done [9, 0, C]`. Send `/custos/params <C+10> 5` → expect only `/custos/params/done [9, C+10, 0]`.

- [ ] **Step 4: Record + commit**

Write `experiments/param-dump.md` with the observed stream, then:
```bash
git add experiments/param-dump.md
git commit -m "Param dump: autonomous E2E results"
```

---

## Self-Review

**Spec coverage (contract §5.1 F1 + §3 shapes):**
- `/custos/params <start> <count>` inbound → Task 1 (`parseCommand`) + Task 3 (route). ✓
- Bound-only, ranged, clamped to `boundCount` → Task 2 (`dumpParams` `jmin(boundCount, …)`). ✓
- `/custos/param <N> <idx> <val> <name>` per param → Task 1 (`buildParam`) + Task 2 (emit). ✓
- `/custos/params/done <N> <start> <count>` with **actually-sent** count + **echoed** start → Task 2 (`sent`, `start`). ✓
- N-tagged (N first arg) → Task 1 (builders). ✓
- No ack (stream is the response) → Task 3 (no `ack()` in the Params case). ✓
- Emission via `outboundSink`, hermetic tests → Task 2 (capturing sink, `enableOsc=false`). ✓

**Placeholder scan:** every code step is complete; no TBD/TODO. E2E ranges are explicit/substitutable.

**Type consistency:** `buildParam(int,int,float,String)` / `buildParamsDone(int,int,int)` (Task 1) are used identically in Task 2. `Command{kind,path,start,count}` (Task 1) is consumed unchanged in Task 3. `dumpParams(int,int)` (Task 2) is the only method Task 3 calls. `facade[i]->getValue()/getName(int)` are the existing `FacadeParameter` accessors. Each task compiles green on its own (Task 2's `dumpParams` is self-contained; Task 3 only consumes it).
