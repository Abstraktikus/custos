# Custos Addressing Core Implementation Plan (Phase C, Feature 1)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give each Custos instance an operator-set identity `N`, derive its OSC port `BASE+N`, and expose the versioned liveness/identity surface (`/custos/hello` → `/custos/here`, `N`-tagged replies, `/custos/loaded` with `boundCount`) so Kapellmeister can address multiple instances.

**Architecture:** Identity `N` (1..15) is set in the Custos UI and persisted per-instance in the VST state (state format gains `N`). The processor stores `N`, (re)binds its OSC receiver to `BASE+N`, and announces `/custos/here`. All Custos→KM messages carry `N` as the first argument; message bodies are built by pure functions so they are unit-testable without a socket. The OSC transport (`CustosOscServer`) is a thin adapter that binds `BASE+N` and pumps the pure-built messages to the hub.

**Tech Stack:** C++17, CMake, JUCE 8.0.14 (`juce_osc` already linked), Catch2 v3.15.1, VST3 (Windows). Builds on M1–M3 (all on `master`; this feature is on branch `custos-osc-contract`).

## Global Constraints

- **Contract:** `docs/osc-contract.md` (v1, `protoVer = 1`). This plan implements only the **addressing core** — NOT param dump (F1), mode (F2), volume (F5), favorites (F4), or window (F3/F6).
- **Addressing:** `BASE = 9100` (the existing `CUSTOS_OSC_PORT` compile-def). `N ∈ 1..15`, 1-based; `port = BASE + N`; `base+0` (9100) is unused; `N = 0` means unassigned → no OSC-in. Set manually in the Custos UI, persisted per-instance. **No MIDI/param injection; GP-Script has no identity role.**
- **Replies:** Custos → hub `CUSTOS_ACK_HOST:CUSTOS_ACK_PORT` (default `127.0.0.1:8000`). **Every Custos→KM message carries `N` as its first argument.**
- **Verbs this feature adds/changes:** in `/custos/hello`; out `/custos/here` (`N, protoVer, mode, inner, boundCount, port`), `/custos/loaded` (`N, path, boundCount`); and `N`-prefix on the existing `/custos/ack`.
- **`/custos/here` mode field:** always `"replace"` in this feature (real mode toggle is F2; the field exists now with its default value).
- **N collision:** if `BASE+N` is already bound, bind fails → the UI shows a visible warning ("port in use — pick another N"), **no auto-fallback**, no crash; the instance has no OSC-in until `N` is changed.
- **No heartbeat.** Announce `/custos/here` on bind + answer `/custos/hello`; send nothing periodic.
- **Tests are hermetic:** construct `CustosProcessor` with `enableOsc = false` (the default) so unit tests bind no UDP port. Message bodies are tested via pure builders; outbound emission is tested via a capturing `outboundSink`.
- **Build the `Custos_VST3` target** (not `Custos`). Build/test from PowerShell: `.\scripts\ci.cmd` (configure clears `CUSTOS_HARDCODED_SYNTH_PATH` when no synth arg). Autonomous OSC tool: `tools/kmplugin.py` + a small pythonosc sender.

---

## File Structure

```
src/
  StateCodec.h/.cpp      # MODIFY: PersistedState gains identityN; serializeState/parseState v2 (+N), v1 back-compat
  OscContract.h          # NEW: pure helpers — kProtoVersion, oscPortForIdentity(N), buildHere/buildAck/buildLoaded
  CustosOscServer.h/.cpp # MODIFY: Command gains Hello; parseCommand(hello); bindToIdentity(N)->BASE+N; N-tagged replies; wire outboundSink
  CustosProcessor.h/.cpp # MODIFY: identityN + setIdentity + bindOsc + lastBindOk; loadInner(path) + emitLoaded via outboundSink; get/setStateInformation v2
  CustosEditor.h/.cpp    # MODIFY: an N field (1..15) + port/collision status; calls setIdentity
tests/
  StateCodecTest.cpp     # MODIFY: v2 round-trip incl. N + v1 back-compat
  OscContractTest.cpp    # NEW: oscPortForIdentity + parseCommand(hello) + builder address/args
  AddressingTest.cpp     # NEW: processor identity + CUS2 state + emitLoaded via capturing sink
CMakeLists.txt           # MODIFY: (OscContract.h is header-only; no source add needed) — no change unless noted
tests/CMakeLists.txt      # MODIFY: add OscContractTest.cpp, AddressingTest.cpp
```

---

## Task 1: StateCodec v2 — persist identity `N`

**Files:**
- Modify: `src/StateCodec.h`, `src/StateCodec.cpp`
- Modify: `tests/StateCodecTest.cpp`

**Interfaces:**
- Produces: `struct custos::PersistedState { juce::String path; juce::MemoryBlock innerState; int identityN = 0; };`
  `juce::MemoryBlock custos::serializeState(const juce::String& path, const juce::MemoryBlock& innerState, int identityN);`
  `bool custos::parseState(const void* data, int size, PersistedState& out);` (v2 magic `"CUS1"` + version byte `2` appends `int32 N`; version `1` still parses with `identityN = 0`).

- [ ] **Step 1: Write the failing tests — append to tests/StateCodecTest.cpp**

Add these two `TEST_CASE`s at the end of the file:
```cpp
TEST_CASE ("serializeState/parseState round-trips identity N (v2)")
{
    const auto blob = serializeState ("C:/x/Diva.vst3", {}, 7);
    PersistedState out;
    REQUIRE (parseState (blob.getData(), (int) blob.getSize(), out));
    REQUIRE (out.path == "C:/x/Diva.vst3");
    REQUIRE (out.identityN == 7);
}

TEST_CASE ("parseState reads a v1 blob with identityN defaulting to 0")
{
    // A hand-built v1 blob: "CUS1" + version 1 + empty path + empty inner (no N field).
    juce::MemoryBlock mb;
    juce::MemoryOutputStream os (mb, false);
    os.write ("CUS1", 4);
    os.writeByte (1);
    os.writeInt (0);   // pathLen
    os.writeInt (0);   // innerLen
    os.flush();

    PersistedState out;
    REQUIRE (parseState (mb.getData(), (int) mb.getSize(), out));
    REQUIRE (out.path.isEmpty());
    REQUIRE (out.identityN == 0);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: FAIL — `serializeState` does not take a third argument; `PersistedState` has no `identityN`.

- [ ] **Step 3: Update src/StateCodec.h**

Replace the struct and the `serializeState` declaration:
```cpp
struct PersistedState { juce::String path; juce::MemoryBlock innerState; };

// Format: "CUS1" + version(1) + int32 pathLen + UTF-8 path + int32 innerLen + inner state bytes.
juce::MemoryBlock serializeState (const juce::String& path, const juce::MemoryBlock& innerState);
```
with:
```cpp
struct PersistedState { juce::String path; juce::MemoryBlock innerState; int identityN = 0; };

// Format: "CUS1" + version byte + int32 pathLen + UTF-8 path + int32 innerLen + inner state bytes,
// and (version >= 2) + int32 identityN. Version 1 blobs parse with identityN = 0.
juce::MemoryBlock serializeState (const juce::String& path, const juce::MemoryBlock& innerState, int identityN);
```

- [ ] **Step 4: Update src/StateCodec.cpp**

Replace the whole `serializeState` body:
```cpp
juce::MemoryBlock serializeState (const juce::String& path, const juce::MemoryBlock& innerState)
{
    juce::MemoryBlock mb;
    juce::MemoryOutputStream os (mb, false);
    os.write ("CUS1", 4);
    os.writeByte (1);
    const char* utf8 = path.toRawUTF8();
    const int pathLen = (int) std::strlen (utf8);
    os.writeInt (pathLen);
    os.write (utf8, (size_t) pathLen);
    os.writeInt ((int) innerState.getSize());
    os.write (innerState.getData(), innerState.getSize());
    os.flush();
    return mb;
}
```
with (magic unchanged, version bumped to 2, `N` appended):
```cpp
juce::MemoryBlock serializeState (const juce::String& path, const juce::MemoryBlock& innerState, int identityN)
{
    juce::MemoryBlock mb;
    juce::MemoryOutputStream os (mb, false);
    os.write ("CUS1", 4);
    os.writeByte (2);
    const char* utf8 = path.toRawUTF8();
    const int pathLen = (int) std::strlen (utf8);
    os.writeInt (pathLen);
    os.write (utf8, (size_t) pathLen);
    os.writeInt ((int) innerState.getSize());
    os.write (innerState.getData(), innerState.getSize());
    os.writeInt (identityN);
    os.flush();
    return mb;
}
```
Then in `parseState`, replace the version guard:
```cpp
    if (is.readByte() != 1) return false;
```
with:
```cpp
    const int version = is.readByte();
    if (version != 1 && version != 2) return false;
```
and at the very end of `parseState`, just before `out.path = path;`, add the `N` read:
```cpp
    int identityN = 0;
    if (version >= 2)
    {
        if (is.getNumBytesRemaining() < 4) return false;
        identityN = is.readInt();
    }
```
and add to the assignment block:
```cpp
    out.path = path;
    out.innerState = std::move (inner);
    out.identityN = identityN;
    return true;
```

Then update the existing call sites so the tree still compiles:
- `src/CustosProcessor.cpp`: change `dest = serializeState (currentSynthPath, innerChunk);` to
  `dest = serializeState (currentSynthPath, innerChunk, 0);` (Task 3 replaces the `0` with the real `identityN`).
- `tests/StateCodecTest.cpp`: the two **existing** M3 tests call `serializeState` with 2 args — add a `0`
  third arg to both: `serializeState (path, inner, 0)` and `serializeState ({}, {}, 0)`.

- [ ] **Step 5: Run the tests to verify they pass**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: all tests PASS (the two new + all prior); build pristine. (`getStateInformation` now persists `N = 0` for every instance; Task 3 makes it the real identity.)

- [ ] **Step 6: Commit**

```bash
git add src/StateCodec.h src/StateCodec.cpp tests/StateCodecTest.cpp
git commit -m "Addressing core: StateCodec v2 persists identity N (v1 back-compat)"
```

---

## Task 2: Pure OSC helpers — port math, hello, message builders

**Files:**
- Create: `src/OscContract.h` (header-only, pure functions)
- Create: `tests/OscContractTest.cpp`
- Modify: `src/CustosOscServer.h` (add `Hello` to `Command::Kind`), `src/CustosOscServer.cpp` (map `/custos/hello`)
- Modify: `tests/CMakeLists.txt` (add `OscContractTest.cpp`)

**Interfaces:**
- Consumes: `CUSTOS_OSC_PORT` (compile-def, = BASE).
- Produces: `custos::kProtoVersion` (=1); `int custos::oscPortForIdentity(int N)` (BASE+N for 1..15, else 0);
  `juce::OSCMessage custos::buildHere(int N, const juce::String& mode, const juce::String& inner, int boundCount, int port)`;
  `juce::OSCMessage custos::buildAck(int N, const juce::String& text)`;
  `juce::OSCMessage custos::buildLoaded(int N, const juce::String& path, int boundCount)`.
  `custos::Command` gains `Hello`; `parseCommand` maps `/custos/hello`.

- [ ] **Step 1: Write src/OscContract.h**

```cpp
#pragma once
#include <juce_osc/juce_osc.h>

#ifndef CUSTOS_OSC_PORT
 #define CUSTOS_OSC_PORT 9100
#endif

namespace custos
{
constexpr int kProtoVersion = 1;

// Deterministic 1-based mapping. N in 1..15 -> BASE+N; anything else -> 0 (invalid / unassigned).
inline int oscPortForIdentity (int n)
{
    return (n >= 1 && n <= 15) ? (CUSTOS_OSC_PORT + n) : 0;
}

inline juce::OSCMessage buildHere (int n, const juce::String& mode, const juce::String& inner,
                                   int boundCount, int port)
{
    return juce::OSCMessage ("/custos/here", n, kProtoVersion, mode, inner, boundCount, port);
}

inline juce::OSCMessage buildAck (int n, const juce::String& text)
{
    return juce::OSCMessage ("/custos/ack", n, text);
}

inline juce::OSCMessage buildLoaded (int n, const juce::String& path, int boundCount)
{
    return juce::OSCMessage ("/custos/loaded", n, path, boundCount);
}
}
```

- [ ] **Step 2: Add `Hello` to Command and map it — src/CustosOscServer.h**

Change:
```cpp
struct Command { enum Kind { Load, Clear, Unknown } kind = Unknown; juce::String path; };
```
to:
```cpp
struct Command { enum Kind { Load, Clear, Hello, Unknown } kind = Unknown; juce::String path; };
```

- [ ] **Step 3: Map `/custos/hello` — src/CustosOscServer.cpp**

In `parseCommand`, before the final `return { Command::Unknown, {} };`, add:
```cpp
    if (addr == "/custos/hello")
        return { Command::Hello, {} };
```

- [ ] **Step 4: Write tests/OscContractTest.cpp**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "OscContract.h"
#include "CustosOscServer.h"

using namespace custos;

TEST_CASE ("oscPortForIdentity maps 1..15 to BASE+N and rejects the rest")
{
    REQUIRE (oscPortForIdentity (1)  == CUSTOS_OSC_PORT + 1);
    REQUIRE (oscPortForIdentity (15) == CUSTOS_OSC_PORT + 15);
    REQUIRE (oscPortForIdentity (0)  == 0);      // unassigned
    REQUIRE (oscPortForIdentity (16) == 0);      // out of range
    REQUIRE (oscPortForIdentity (-3) == 0);
}

TEST_CASE ("parseCommand maps /custos/hello")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/hello")).kind == Command::Hello);
}

TEST_CASE ("buildHere carries N first, then protoVer/mode/inner/count/port")
{
    const auto m = buildHere (3, "replace", "CS-80 V4", 2797, 9103);
    REQUIRE (m.getAddressPattern().toString() == "/custos/here");
    REQUIRE (m.size() == 6);
    REQUIRE (m[0].getInt32() == 3);
    REQUIRE (m[1].getInt32() == kProtoVersion);
    REQUIRE (m[2].getString() == "replace");
    REQUIRE (m[3].getString() == "CS-80 V4");
    REQUIRE (m[4].getInt32() == 2797);
    REQUIRE (m[5].getInt32() == 9103);
}

TEST_CASE ("buildAck and buildLoaded carry N first")
{
    const auto a = buildAck (5, "cleared");
    REQUIRE (a.getAddressPattern().toString() == "/custos/ack");
    REQUIRE (a[0].getInt32() == 5);
    REQUIRE (a[1].getString() == "cleared");

    const auto l = buildLoaded (5, "C:/x/Diva.vst3", 3124);
    REQUIRE (l.getAddressPattern().toString() == "/custos/loaded");
    REQUIRE (l[0].getInt32() == 5);
    REQUIRE (l[1].getString() == "C:/x/Diva.vst3");
    REQUIRE (l[2].getInt32() == 3124);
}
```

- [ ] **Step 5: Register the test — tests/CMakeLists.txt**

Add `OscContractTest.cpp` to the `add_executable(custos_tests ...)` list (after `OscCommandTest.cpp`).

- [ ] **Step 6: Build + test**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: the four new OscContract tests PASS. (Link may still fail via `CustosProcessor.cpp` until Task 3 — see Task 1 Step 5.)

- [ ] **Step 7: Commit**

```bash
git add src/OscContract.h src/CustosOscServer.h src/CustosOscServer.cpp tests/OscContractTest.cpp tests/CMakeLists.txt
git commit -m "Addressing core: pure OSC helpers (port math, hello, N-tagged builders)"
```

---

## Task 3: Processor identity, CUS2 state, and /custos/loaded emission

**Files:**
- Modify: `src/CustosProcessor.h`, `src/CustosProcessor.cpp`
- Create: `tests/AddressingTest.cpp`
- Modify: `tests/CMakeLists.txt` (add `AddressingTest.cpp`)

**Interfaces:**
- Consumes: `serializeState/parseState` + `PersistedState.identityN` (Task 1); `buildLoaded`, `oscPortForIdentity` (Task 2).
- Produces (on `CustosProcessor`):
  `void setIdentity (int n);` `int identity() const noexcept;` `bool identityBound() const noexcept;`
  `juce::String modeString() const noexcept;` (returns `"replace"` for now)
  `std::function<void(const juce::OSCMessage&)> outboundSink;` (public; set by the OSC server or by a test)
  `loadInner` gains a `path` param; `load`/`clear` route through it; both emit `/custos/loaded`.

- [ ] **Step 1: Write the failing tests — tests/AddressingTest.cpp**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"
#include "StateCodec.h"
#include "FakeInnerProcessor.h"
#include <vector>

using namespace custos;

TEST_CASE ("setIdentity stores N and (without OSC) reports it")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;                 // enableOsc = false -> no bind
    REQUIRE (proc.identity() == 0);        // unassigned by default
    proc.setIdentity (4);
    REQUIRE (proc.identity() == 4);
}

TEST_CASE ("getStateInformation/setStateInformation round-trip identity N")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor a;
    a.setIdentity (9);
    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    CustosProcessor b;
    b.setStateInformation (blob.getData(), (int) blob.getSize());
    REQUIRE (b.identity() == 9);
}

TEST_CASE ("loadInner emits /custos/loaded with N, path and boundCount")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.prepareToPlay (48000.0, 64);
    proc.setIdentity (3);

    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    REQUIRE (proc.loadInner (std::make_unique<custos::test::FakeInnerProcessor> (5), "C:/x/Diva.vst3"));

    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/loaded");
    REQUIRE (sent[0][0].getInt32() == 3);
    REQUIRE (sent[0][1].getString() == "C:/x/Diva.vst3");
    REQUIRE (sent[0][2].getInt32() == 5);
}

TEST_CASE ("clear emits /custos/loaded with empty path and boundCount 0")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.prepareToPlay (48000.0, 64);
    proc.setIdentity (3);
    proc.attachInner (std::make_unique<custos::test::FakeInnerProcessor> (2));

    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.clear();
    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0][0].getInt32() == 3);
    REQUIRE (sent[0][1].getString().isEmpty());
    REQUIRE (sent[0][2].getInt32() == 0);
}
```

- [ ] **Step 2: Run to verify failure**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: FAIL — `setIdentity`, `identity`, `outboundSink`, and the 2-arg `loadInner` do not exist yet.

- [ ] **Step 3: Update src/CustosProcessor.h**

Add the includes at the top (after the existing includes): `#include <juce_osc/juce_osc.h>` and `#include <functional>`.

In the public section, replace the swap API block:
```cpp
    // M3 safe runtime swap (message thread). loadInner(nullptr) == clear.
    bool loadInner (std::unique_ptr<juce::AudioProcessor> newInner);
    CommandResult load (const juce::String& path);
    void clear();
    void attachInner (std::unique_ptr<juce::AudioProcessor> newInner) { loadInner (std::move (newInner)); }
```
with:
```cpp
    // M3 safe runtime swap (message thread). loadInner(nullptr) == clear.
    bool loadInner (std::unique_ptr<juce::AudioProcessor> newInner, const juce::String& path = {});
    CommandResult load (const juce::String& path);
    void clear();
    void attachInner (std::unique_ptr<juce::AudioProcessor> newInner) { loadInner (std::move (newInner)); }

    // Addressing core (message thread). N in 1..15; 0 = unassigned.
    void setIdentity (int n);
    int  identity() const noexcept { return identityN; }
    bool identityBound() const noexcept { return lastBindOk; }
    juce::String modeString() const noexcept { return "replace"; }   // real toggle = F2

    // Set by CustosOscServer to send to the KM hub; null in unit tests (emission is then a no-op).
    std::function<void(const juce::OSCMessage&)> outboundSink;
```

In the `private:` section, add near `currentSynthPath`:
```cpp
    int  identityN = 0;        // operator-set; 0 = unassigned. Persisted (CUS v2).
    bool lastBindOk = false;   // did the OSC receiver bind BASE+N?
    void emitLoaded();         // send /custos/loaded via outboundSink (no-op if null)
    void bindOsc();            // (re)bind the OSC server to BASE+identityN, if any
```

- [ ] **Step 4: Update src/CustosProcessor.cpp — includes + loadInner path + emit + identity**

Add the include (after `#include "CustosOscServer.h"`):
```cpp
#include "OscContract.h"
```

Change the `loadInner` signature line:
```cpp
bool CustosProcessor::loadInner (std::unique_ptr<juce::AudioProcessor> newInner)
```
to:
```cpp
bool CustosProcessor::loadInner (std::unique_ptr<juce::AudioProcessor> newInner, const juce::String& path)
```
Inside `loadInner`, replace the tail (the `refreshEditor(); return inner != nullptr;` at the end) with a `currentSynthPath` update + emit:
```cpp
    currentSynthPath = (inner != nullptr) ? path : juce::String();
    refreshEditor();
    emitLoaded();
    return inner != nullptr;
```

In `load`, route the path through `loadInner` and drop the separate assignment. Replace:
```cpp
    if (auto instance = SynthLoader::loadVST3 (path, sr, block, err))
    {
        loadInner (std::move (instance));
        currentSynthPath = path;
        return { true, boundCount, "loaded " + path };
    }
```
with:
```cpp
    if (auto instance = SynthLoader::loadVST3 (path, sr, block, err))
    {
        loadInner (std::move (instance), path);
        return { true, boundCount, "loaded " + path };
    }
```
Replace `clear`:
```cpp
void CustosProcessor::clear()
{
    currentSynthPath = {};
    loadInner (nullptr);
}
```
with:
```cpp
void CustosProcessor::clear()
{
    loadInner (nullptr, {});
}
```
Add the new methods (anywhere in the `custos` namespace, e.g. after `clear`):
```cpp
void CustosProcessor::emitLoaded()
{
    if (outboundSink)
        outboundSink (buildLoaded (identityN, currentSynthPath, boundCount));
}

void CustosProcessor::bindOsc()
{
    if (oscServer != nullptr)
        lastBindOk = oscServer->bindToIdentity (identityN);
    else
        lastBindOk = false;
}

void CustosProcessor::setIdentity (int n)
{
    identityN = n;
    bindOsc();          // no-op inner details when oscServer is null (unit tests)
    refreshEditor();
}
```

- [ ] **Step 5: Update get/setStateInformation — src/CustosProcessor.cpp**

Replace:
```cpp
void CustosProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    trace ("getStateInformation");
    juce::MemoryBlock innerChunk;
    if (inner != nullptr) inner->getStateInformation (innerChunk);
    dest = serializeState (currentSynthPath, innerChunk, 0);
}

void CustosProcessor::setStateInformation (const void* data, int size)
{
    trace ("setStateInformation");
    PersistedState ps;
    if (! parseState (data, size, ps)) return;   // unknown/legacy blob -> ignore, don't crash

    if (ps.path.isEmpty())
    {
        clear();
        return;
    }

    const auto r = load (ps.path);                // safe swap + currentSynthPath
    if (r.ok && inner != nullptr && ps.innerState.getSize() > 0)
        inner->setStateInformation (ps.innerState.getData(), (int) ps.innerState.getSize());
}
```
with (persist `N`; restore identity **before** load so `/custos/loaded` is tagged right, then bind after):
```cpp
void CustosProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    trace ("getStateInformation");
    juce::MemoryBlock innerChunk;
    if (inner != nullptr) inner->getStateInformation (innerChunk);
    dest = serializeState (currentSynthPath, innerChunk, identityN);
}

void CustosProcessor::setStateInformation (const void* data, int size)
{
    trace ("setStateInformation");
    PersistedState ps;
    if (! parseState (data, size, ps)) return;   // unknown/legacy blob -> ignore, don't crash

    identityN = ps.identityN;                    // tag emissions correctly before any load/clear

    if (ps.path.isEmpty())
    {
        clear();
    }
    else
    {
        const auto r = load (ps.path);           // safe swap + currentSynthPath + /custos/loaded
        if (r.ok && inner != nullptr && ps.innerState.getSize() > 0)
            inner->setStateInformation (ps.innerState.getData(), (int) ps.innerState.getSize());
    }

    bindOsc();                                   // bind BASE+N + announce /custos/here (reflects current inner)
    refreshEditor();
}
```

- [ ] **Step 6: Register the test + build + test**

Add `AddressingTest.cpp` to `tests/CMakeLists.txt` (`add_executable` list).
Run (PowerShell): `.\scripts\ci.cmd`
Expected: all tests PASS (M1/M2/M3 + StateCodec v2 + OscContract + the 4 new Addressing tests). Build pristine.
**Note:** `bindOsc()` here calls `CustosOscServer::bindToIdentity` (declared in Task 4). Tasks 3 and 4 are
**compile-coupled** — the tree builds green only once Task 4's header declaration is in place. Implement
Task 4 immediately after Task 3 and build them together (commit may be combined).

- [ ] **Step 7: Commit**

```bash
git add src/CustosProcessor.h src/CustosProcessor.cpp tests/AddressingTest.cpp tests/CMakeLists.txt
git commit -m "Addressing core: processor identity N, CUS2 state, /custos/loaded emission"
```

---

## Task 4: CustosOscServer — bind BASE+N, wire replies + hello + loaded

**Files:**
- Modify: `src/CustosOscServer.h`, `src/CustosOscServer.cpp`

**Interfaces:**
- Consumes: `CustosProcessor::{load,clear,innerSynthName,boundParamCount,modeString,identity,outboundSink}`; `oscPortForIdentity`, `buildHere`, `buildAck` (Task 2).
- Produces: `bool CustosOscServer::bindToIdentity(int n)` (returns bind success; announces `/custos/here` on success). Server sets `proc.outboundSink` in its ctor and clears it in its dtor.

**Note:** this task is a thin transport adapter; its behavior is verified by the Task 6 autonomous E2E (binding a real UDP port is not hermetic). Keep the logic trivial.

- [ ] **Step 1: Update src/CustosOscServer.h**

Add the public method and a member. In the public section, after the destructor:
```cpp
    // (Re)bind the OSC receiver to BASE+n. Returns false on invalid n or a port clash (N collision).
    // On success, announces /custos/here. Message thread only.
    bool bindToIdentity (int n);
```
In `private:`, add:
```cpp
    void announceHere();
    int  currentN = 0;
```

- [ ] **Step 2: Rework the ctor/dtor + add bind/announce — src/CustosOscServer.cpp**

Add the include (after `#include "CustosProcessor.h"`):
```cpp
#include "OscContract.h"
```
Replace the constructor:
```cpp
CustosOscServer::CustosOscServer (CustosProcessor& p) : proc (p)
{
    if (receiver.connect (CUSTOS_OSC_PORT))
        receiver.addListener (this);
    else
        juce::Logger::writeToLog ("Custos: OSC receiver could not bind port "
                                  + juce::String (CUSTOS_OSC_PORT));

    ackReady = ackSender.connect (CUSTOS_ACK_HOST, CUSTOS_ACK_PORT);
}
```
with (do NOT bind the receiver here — that happens in `bindToIdentity` once `N` is known; wire the outbound sink):
```cpp
CustosOscServer::CustosOscServer (CustosProcessor& p) : proc (p)
{
    ackReady = ackSender.connect (CUSTOS_ACK_HOST, CUSTOS_ACK_PORT);
    proc.outboundSink = [this] (const juce::OSCMessage& m) { if (ackReady) ackSender.send (m); };
}
```
Replace the destructor:
```cpp
CustosOscServer::~CustosOscServer()
{
    receiver.removeListener (this);
    receiver.disconnect();
    ackSender.disconnect();
}
```
with (also drop the sink so a late emission after teardown is safe):
```cpp
CustosOscServer::~CustosOscServer()
{
    proc.outboundSink = nullptr;
    receiver.removeListener (this);
    receiver.disconnect();
    ackSender.disconnect();
}
```
Add `bindToIdentity` and `announceHere` (after the destructor):
```cpp
bool CustosOscServer::bindToIdentity (int n)
{
    receiver.removeListener (this);
    receiver.disconnect();
    currentN = n;

    const int port = oscPortForIdentity (n);
    if (port == 0)
        return false;                      // unassigned / out of range -> no OSC-in

    if (! receiver.connect (port))
    {
        juce::Logger::writeToLog ("Custos: OSC port " + juce::String (port)
                                  + " in use (N collision, N=" + juce::String (n) + ")");
        return false;                      // N collision -> UI shows a warning; no auto-fallback
    }

    receiver.addListener (this);
    announceHere();
    return true;
}

void CustosOscServer::announceHere()
{
    if (ackReady)
        ackSender.send (buildHere (currentN, proc.modeString(), proc.innerSynthName(),
                                   proc.boundParamCount(), oscPortForIdentity (currentN)));
}
```

- [ ] **Step 3: N-tag the ack + handle hello — src/CustosOscServer.cpp**

Replace `oscMessageReceived`:
```cpp
void CustosOscServer::oscMessageReceived (const juce::OSCMessage& msg)
{
    const auto cmd = parseCommand (msg);
    switch (cmd.kind)
    {
        case Command::Load:
        {
            const auto r = proc.load (cmd.path);
            ack (r.ok ? ("loaded " + cmd.path + " count=" + juce::String (r.innerCount))
                      : r.message);
            break;
        }
        case Command::Clear:
            proc.clear();
            ack ("cleared");
            break;
        case Command::Unknown:
        default:
            ack ("error unknown " + msg.getAddressPattern().toString());
            break;
    }
}
```
with (route ack through the N-tagged builder, add `Hello`):
```cpp
void CustosOscServer::oscMessageReceived (const juce::OSCMessage& msg)
{
    const auto cmd = parseCommand (msg);
    switch (cmd.kind)
    {
        case Command::Load:
        {
            const auto r = proc.load (cmd.path);
            ack (r.ok ? ("loaded " + cmd.path + " count=" + juce::String (r.innerCount))
                      : r.message);
            break;
        }
        case Command::Clear:
            proc.clear();
            ack ("cleared");
            break;
        case Command::Hello:
            announceHere();
            break;
        case Command::Unknown:
        default:
            ack ("error unknown " + msg.getAddressPattern().toString());
            break;
    }
}
```
Replace the `ack` helper:
```cpp
void CustosOscServer::ack (const juce::String& text)
{
    if (ackReady)
        ackSender.send ("/custos/ack", text);
}
```
with (N-tagged via the builder):
```cpp
void CustosOscServer::ack (const juce::String& text)
{
    if (ackReady)
        ackSender.send (buildAck (currentN, text));
}
```

- [ ] **Step 4: Build + test**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: all unit tests still PASS (unchanged in count; this task adds no unit test — it is transport). Build pristine, `Custos.vst3` produced.

- [ ] **Step 5: Commit**

```bash
git add src/CustosOscServer.h src/CustosOscServer.cpp
git commit -m "Addressing core: OSC server binds BASE+N, N-tagged replies, hello->here"
```

---

## Task 5: CustosEditor — the identity `N` field + status

**Files:**
- Modify: `src/CustosEditor.h`, `src/CustosEditor.cpp`

**Interfaces:**
- Consumes: `CustosProcessor::{setIdentity,identity,identityBound}`, `oscPortForIdentity` (Task 2).

- [ ] **Step 1: Add the field to src/CustosEditor.h**

Add members + a private helper declaration (after `synthButton`, above the `JUCE_DECLARE_...` macro):
```cpp
    juce::Label      idLabel;      // "Id" caption
    juce::TextEditor idField;      // operator types N (1..15)
    juce::Label      idStatus;     // ":<port>" or a collision / unassigned warning

    void commitIdentity();
```

- [ ] **Step 2: Wire the field in src/CustosEditor.cpp — constructor**

In the constructor, after the `synthButton` block and before `refresh();`, add:
```cpp
    idLabel.setText ("Id", juce::dontSendNotification);
    idLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (idLabel);

    idField.setInputRestrictions (2, "0123456789");
    idField.setJustification (juce::Justification::centred);
    idField.onReturnKey = [this] { commitIdentity(); };
    idField.onFocusLost = [this] { commitIdentity(); };
    addAndMakeVisible (idField);

    idStatus.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (idStatus);
```
Add the commit helper + include (top of file, after existing includes): `#include "OscContract.h"`. Then add the method (e.g. after `refresh()`):
```cpp
void CustosEditor::commitIdentity()
{
    const int n = idField.getText().getIntValue();
    if (n >= 1 && n <= 15)
        proc.setIdentity (n);
    refresh();
}
```

- [ ] **Step 3: Show status in refresh() — src/CustosEditor.cpp**

Replace `refresh()`:
```cpp
void CustosEditor::refresh()
{
    statusLabel.setText ("Synth: " + proc.innerSynthName(), juce::dontSendNotification);
    synthButton.setButtonText (proc.isSynthWindowVisible() ? "Hide Synth" : "Show Synth");
    synthButton.setEnabled (proc.hasInnerSynth());
}
```
with:
```cpp
void CustosEditor::refresh()
{
    statusLabel.setText ("Synth: " + proc.innerSynthName(), juce::dontSendNotification);
    synthButton.setButtonText (proc.isSynthWindowVisible() ? "Hide Synth" : "Show Synth");
    synthButton.setEnabled (proc.hasInnerSynth());

    const int n = proc.identity();
    if (n < 1 || n > 15)
        idStatus.setText ("unassigned", juce::dontSendNotification);
    else if (! proc.identityBound())
        idStatus.setText (":" + juce::String (oscPortForIdentity (n)) + "  PORT IN USE — pick another N",
                          juce::dontSendNotification);
    else
        idStatus.setText (":" + juce::String (oscPortForIdentity (n)), juce::dontSendNotification);

    const juce::String nText = (n >= 1 && n <= 15) ? juce::String (n) : juce::String();
    if (idField.getText() != nText) idField.setText (nText, juce::dontSendNotification);
}
```

- [ ] **Step 4: Lay the field out — src/CustosEditor.cpp `resized()` + window size**

Replace `resized()`:
```cpp
void CustosEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    titleLabel.setBounds (r.removeFromTop (28));
    r.removeFromTop (8);
    statusLabel.setBounds (r.removeFromTop (24));
    r.removeFromTop (12);
    synthButton.setBounds (r.removeFromTop (32).removeFromLeft (140));
}
```
with (add an id row):
```cpp
void CustosEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    titleLabel.setBounds (r.removeFromTop (28));
    r.removeFromTop (8);
    statusLabel.setBounds (r.removeFromTop (24));
    r.removeFromTop (12);
    synthButton.setBounds (r.removeFromTop (32).removeFromLeft (140));
    r.removeFromTop (12);
    auto idRow = r.removeFromTop (24);
    idLabel.setBounds  (idRow.removeFromLeft (28));
    idField.setBounds  (idRow.removeFromLeft (48));
    idRow.removeFromLeft (8);
    idStatus.setBounds (idRow);
}
```
And enlarge the window — change `setSize (320, 120);` (end of constructor) to:
```cpp
    setSize (360, 172);
```

- [ ] **Step 5: Build + test**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: all tests PASS (the existing `EditorTest.cpp` still constructs a non-null editor). Build pristine.

- [ ] **Step 6: Commit**

```bash
git add src/CustosEditor.h src/CustosEditor.cpp
git commit -m "Addressing core: CustosEditor identity N field + port/collision status"
```

---

## Task 6: Autonomous E2E (driven from outside a live GP)

**Goal:** prove the addressing core end-to-end. Not TDD — this is verification driven by the operator/agent. Record results in `experiments/addressing-core.md`.

**Setup:** deploy the built plugin and restart GP with the currently-open gig (get the gig path from the GP process command line — do NOT assume a folder). Use two Custos instances in two Global slots for the multi-instance check. A tiny pythonosc helper sends to `127.0.0.1:9100+N` and listens on the hub `:8000`.

- [ ] **Step 1: Deploy + restart GP**

```
.\scripts\deploy.cmd "C:/Program Files/Common Files/VST3/CS-80 V4.vst3"
```
Save the gig via SystemActions param 89, terminate GP (`Stop-Process -Name GigPerformer5 -Force`), then relaunch the open gig (`Start-Process <gig-from-cmdline>`). Wait for the harness to answer.

- [ ] **Step 2: Set identity + verify /custos/here**

In each Custos editor set `N` (e.g. 1 and 2). Then from the hub, send `/custos/hello` to `127.0.0.1:9101` and `:9102` and confirm each replies `/custos/here <N> 1 replace <inner> <count> <port>` with the matching `N`/port. Expected: instance-1 answers on 9101, instance-2 on 9102; no cross-talk.

- [ ] **Step 3: Verify /custos/loaded on a swap**

Send `/custos/load "C:/Program Files/Common Files/VST3/DX7 V.vst3"` to `:9101`. Expected on the hub: `/custos/ack 1 "loaded … count=3124"` AND `/custos/loaded 1 "…/DX7 V.vst3" 3124`. Send `/custos/clear` to `:9101` → `/custos/ack 1 "cleared"` + `/custos/loaded 1 "" 0`.

- [ ] **Step 4: Verify persistence of N**

Set instance-1 to N=5, save the gig, restart GP, then `/custos/hello` to `:9105`. Expected: it answers `/custos/here 5 …` (N restored from the gig state, bound to 9105 automatically).

- [ ] **Step 5: Verify N collision**

Set instance-2's N to 1 (same as instance-1). Expected: instance-2's editor shows "PORT IN USE — pick another N"; `/custos/hello` to `:9101` still answers only instance-1; instance-2 is silent until its N is changed back.

- [ ] **Step 6: Record + commit**

Write `experiments/addressing-core.md` with the observed replies, then:
```bash
git add experiments/addressing-core.md
git commit -m "Addressing core: autonomous E2E results"
```

---

## Self-Review

**Spec coverage (contract §2 addressing, §3 verbs, §4 persistence, §6 UI, §7 errors):**
- Manual `N` in the UI + persisted per-instance → Task 1 (CUS2) + Task 3 (get/setStateInformation) + Task 5 (field). ✓
- `port = BASE+N`, 1-based, unset→no OSC-in → Task 2 (`oscPortForIdentity`) + Task 4 (`bindToIdentity`). ✓
- Every reply carries `N` first → Task 2 (builders) + Task 4 (ack/here). ✓
- `/custos/hello` → `/custos/here` (versioned, mode/inner/count/port) → Task 2 + Task 4. ✓
- `/custos/loaded <N> <path> <boundCount>` on OSC **and** state-load → Task 3 (emit in `loadInner`). ✓
- `N` collision → visible UI warning, no auto-fallback → Task 4 (`bindToIdentity` returns false) + Task 5 (status). ✓
- No heartbeat → nothing periodic is sent (announce-on-bind + hello only). ✓
- Out of scope (params dump/mode/volume/favorites/window) → not touched. ✓

**Placeholder scan:** every code step contains complete code; no TBD/TODO. E2E synth paths are explicit + substitutable.

**Type consistency:** `serializeState(path, inner, N)` and `PersistedState.identityN` (Task 1) are used identically in Task 3. `oscPortForIdentity`, `buildHere/buildAck/buildLoaded`, `kProtoVersion`, `Command::Hello` (Task 2) are consumed unchanged in Tasks 3–5. `setIdentity/identity/identityBound/modeString/outboundSink/loadInner(path)` (Task 3) match their use in Tasks 4–5. `bindToIdentity` (Task 4) is the only method Task 3's `bindOsc()` calls. `attachInner` stays a 1-arg wrapper so M1/M2 tests are untouched.

**Independent compilation:** every task ends green. Task 1 leaves the call site passing `N = 0`; Task 3 swaps it for the real `identityN`. No task depends on a later task to build.
