# Custos Main L/R Fold — Feedback + Query Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Main L/R audio-fold flag observable to KM by adding feedback-on-set plus an on-demand `/custos/mainlr/query`, mirroring the existing `/custos/midi/route` + `/custos/midi/query` pattern.

**Architecture:** A new `buildMainLR` OSC builder and a `CustosProcessor::emitMainLR()` twin the existing `buildMidiRoute`/`emitMidiRoute`. A new `MainLRQuery` command parses `/custos/mainlr/query`. The dispatch switch emits after an OSC set and on a query. The Custos editor toggle is deliberately untouched (no UI-driven feedback, per the spec).

**Tech Stack:** C++17, JUCE 8, CMake + Ninja + MSVC, Catch2 tests.

## Global Constraints

- `kProtoVersion` stays **2** — the change is additive/back-compat; do NOT bump it.
- Feedback fires on exactly two paths: the `Command::MainLR` OSC-set handler and the `Command::MainLRQuery` handler. The editor toggle (`CustosEditor.cpp:117`) stays as-is — no emit.
- Feedback message shape: `/custos/mainlr` with args `[N:int, on:int]`, `on` = `1|0`. `N` first (demux convention).
- Inbound set `/custos/mainlr <on:int>` is unchanged.
- No state-format change (the flag already persists in state v4).
- Build/test on this machine needs the VS env loaded — cmake/cl are NOT on PATH. Use the wrapper created in Task 1.

## File Structure

- `src/OscContract.h` — add `buildMainLR(int n, bool on)` inline builder (twin of `buildMidiRoute`).
- `src/CustosProcessor.h` — declare `void emitMainLR();`.
- `src/CustosProcessor.cpp` — define `emitMainLR()` (twin of `emitMidiRoute()`).
- `src/CustosOscServer.h` — add `MainLRQuery` to the `Command::kind` enum.
- `src/CustosOscServer.cpp` — parse `/custos/mainlr/query`; wire the dispatch switch (emit on set + on query).
- `tests/OscContractTest.cpp` — `buildMainLR` shape test.
- `tests/OscCommandTest.cpp` — `emitMainLR` emit test + `/custos/mainlr/query` parse test.
- `docs/osc-contract.md` — §2 query row, §3 feedback row, §4 amend the Main L/R bullet.

**Build wrapper (created in Task 1, reused by every task):**
`C:\Users\marti\AppData\Local\Temp\claude\C--dev\69381d00-225b-4849-805f-4990151335f2\scratchpad\build_tests.cmd`

From the Bash tool, build with:
`cmd //c "C:\Users\marti\AppData\Local\Temp\claude\C--dev\69381d00-225b-4849-805f-4990151335f2\scratchpad\build_tests.cmd"`
Run tests with:
`"C:/dev/custos/build/tests/custos_tests.exe" "<catch2-filter>"` (omit the filter to run the whole suite).

---

### Task 1: `buildMainLR` OSC builder

**Files:**
- Create: `C:\Users\marti\AppData\Local\Temp\claude\C--dev\69381d00-225b-4849-805f-4990151335f2\scratchpad\build_tests.cmd`
- Modify: `src/OscContract.h` (add builder after `buildMidiRoute`, ~line 68)
- Test: `tests/OscContractTest.cpp` (add after the `buildMidiRoute` test, ~line 194)

**Interfaces:**
- Produces: `custos::buildMainLR(int n, bool on) -> juce::OSCMessage` with address `/custos/mainlr`, args `[n, on?1:0]`.

- [ ] **Step 1: Create the build wrapper**

Create `C:\Users\marti\AppData\Local\Temp\claude\C--dev\69381d00-225b-4849-805f-4990151335f2\scratchpad\build_tests.cmd` with:

```bat
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build C:\dev\custos\build --target custos_tests
```

- [ ] **Step 2: Write the failing test**

Add to `tests/OscContractTest.cpp`:

```cpp
TEST_CASE ("buildMainLR carries N first, then the fold flag")
{
    const auto on = buildMainLR (9, true);
    REQUIRE (on.getAddressPattern().toString() == "/custos/mainlr");
    REQUIRE (on.size() == 2);
    REQUIRE (on[0].getInt32() == 9);
    REQUIRE (on[1].getInt32() == 1);

    const auto off = buildMainLR (9, false);
    REQUIRE (off[1].getInt32() == 0);
}
```

- [ ] **Step 3: Build to verify it fails**

Run: `cmd //c "C:\Users\marti\AppData\Local\Temp\claude\C--dev\69381d00-225b-4849-805f-4990151335f2\scratchpad\build_tests.cmd"`
Expected: FAIL — compile error, `buildMainLR` is not declared.

- [ ] **Step 4: Implement the builder**

In `src/OscContract.h`, immediately after the `buildMidiRoute` inline function, add:

```cpp
inline juce::OSCMessage buildMainLR (int n, bool on)
{
    return juce::OSCMessage ("/custos/mainlr", n, on ? 1 : 0);
}
```

- [ ] **Step 5: Build and run the test to verify it passes**

Run: `cmd //c "C:\Users\marti\AppData\Local\Temp\claude\C--dev\69381d00-225b-4849-805f-4990151335f2\scratchpad\build_tests.cmd"`
Then: `"C:/dev/custos/build/tests/custos_tests.exe" "buildMainLR*"`
Expected: PASS (1 test case).

- [ ] **Step 6: Commit**

```bash
cd /c/dev/custos && git add src/OscContract.h tests/OscContractTest.cpp && git commit -m "feat: add buildMainLR OSC builder

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: `CustosProcessor::emitMainLR()`

**Files:**
- Modify: `src/CustosProcessor.h` (declare next to `emitMidiRoute()`, ~line 144)
- Modify: `src/CustosProcessor.cpp` (define after `emitMidiRoute()`, ~line 332)
- Test: `tests/OscCommandTest.cpp` (add after the `emitMidiRoute` test, ~line 77)

**Interfaces:**
- Consumes: `custos::buildMainLR` (Task 1); `CustosProcessor::mainLROnly()`, `setMainLROnly(bool)`, `setIdentity(int)`, `outboundSink`.
- Produces: `void CustosProcessor::emitMainLR()` — sends `buildMainLR(identityN, mainLROnly())` through `outboundSink` (no-op if the sink is unset).

- [ ] **Step 1: Write the failing test**

Add to `tests/OscCommandTest.cpp`:

```cpp
TEST_CASE ("emitMainLR sends the current fold flag with N first")
{
    custos::CustosProcessor proc;
    proc.setIdentity (9);
    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.setMainLROnly (true);
    proc.emitMainLR();

    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/mainlr");
    REQUIRE (sent[0].size() == 2);
    REQUIRE (sent[0][0].getInt32() == 9);
    REQUIRE (sent[0][1].getInt32() == 1);
}
```

- [ ] **Step 2: Build to verify it fails**

Run: `cmd //c "C:\Users\marti\AppData\Local\Temp\claude\C--dev\69381d00-225b-4849-805f-4990151335f2\scratchpad\build_tests.cmd"`
Expected: FAIL — compile error, `emitMainLR` is not a member of `CustosProcessor`.

- [ ] **Step 3: Declare the method**

In `src/CustosProcessor.h`, directly below `void emitMidiRoute();`, add:

```cpp
    void emitMainLR();
```

- [ ] **Step 4: Define the method**

In `src/CustosProcessor.cpp`, directly after the `emitMidiRoute()` definition, add:

```cpp
void CustosProcessor::emitMainLR()
{
    if (outboundSink) outboundSink (buildMainLR (identityN, mainLROnly()));
}
```

- [ ] **Step 5: Build and run the test to verify it passes**

Run: `cmd //c "C:\Users\marti\AppData\Local\Temp\claude\C--dev\69381d00-225b-4849-805f-4990151335f2\scratchpad\build_tests.cmd"`
Then: `"C:/dev/custos/build/tests/custos_tests.exe" "emitMainLR*"`
Expected: PASS (1 test case).

- [ ] **Step 6: Commit**

```bash
cd /c/dev/custos && git add src/CustosProcessor.h src/CustosProcessor.cpp tests/OscCommandTest.cpp && git commit -m "feat: add CustosProcessor::emitMainLR feedback emitter

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: `/custos/mainlr/query` command + parser

**Files:**
- Modify: `src/CustosOscServer.h` (add `MainLRQuery` to the `Command::kind` enum, ~line 16)
- Modify: `src/CustosOscServer.cpp` (parse `/custos/mainlr/query`, after the `/custos/mainlr` block ~line 50)
- Test: `tests/OscCommandTest.cpp` (add after the `/custos/mainlr` parse test, ~line 152)

**Interfaces:**
- Consumes: `parseCommand(const juce::OSCMessage&) -> Command`; `Command::kind` enum.
- Produces: enum value `Command::MainLRQuery`; `parseCommand` maps `/custos/mainlr/query` (no args) to it.

- [ ] **Step 1: Write the failing test**

Add to `tests/OscCommandTest.cpp`:

```cpp
TEST_CASE ("parseCommand maps /custos/mainlr/query")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/mainlr/query")).kind == Command::MainLRQuery);
}
```

- [ ] **Step 2: Build to verify it fails**

Run: `cmd //c "C:\Users\marti\AppData\Local\Temp\claude\C--dev\69381d00-225b-4849-805f-4990151335f2\scratchpad\build_tests.cmd"`
Expected: FAIL — compile error, `MainLRQuery` is not a member of `Command::kind`.

- [ ] **Step 3: Add the enum value**

In `src/CustosOscServer.h`, in the `Command::kind` enum, insert `MainLRQuery` immediately before `Unknown`:

```cpp
                PresetSet, PresetRename, PresetDelete, MainLR, MainLRQuery, Unknown } kind = Unknown;
```

- [ ] **Step 4: Add the parse branch**

In `src/CustosOscServer.cpp`, immediately after the closing brace of the `/custos/mainlr` set block (the `MainLR` command, ~line 50), add:

```cpp
    if (addr == "/custos/mainlr/query")
        return { Command::MainLRQuery, {} };
```

- [ ] **Step 5: Build and run the test to verify it passes**

Run: `cmd //c "C:\Users\marti\AppData\Local\Temp\claude\C--dev\69381d00-225b-4849-805f-4990151335f2\scratchpad\build_tests.cmd"`
Then: `"C:/dev/custos/build/tests/custos_tests.exe" "*mainlr/query*"`
Expected: PASS (1 test case).

- [ ] **Step 6: Commit**

```bash
cd /c/dev/custos && git add src/CustosOscServer.h src/CustosOscServer.cpp tests/OscCommandTest.cpp && git commit -m "feat: parse /custos/mainlr/query into Command::MainLRQuery

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Wire the dispatch switch + update the contract doc

**Files:**
- Modify: `src/CustosOscServer.cpp` (dispatch switch: `case MainLR` emits, add `case MainLRQuery`, ~lines 266-268)
- Modify: `docs/osc-contract.md` (§2 query row, §3 feedback row, §4 Main L/R bullet)

**Interfaces:**
- Consumes: `CustosProcessor::emitMainLR()` (Task 2); `Command::MainLR`, `Command::MainLRQuery` (Task 3).
- Produces: an applied `/custos/mainlr` set now emits one `/custos/mainlr` feedback; `/custos/mainlr/query` emits one without changing state.

This is one-line dispatch glue mirroring the existing `Command::MidiRoute` (emits `emitMidiRoute()`) and `Command::MidiQuery` cases, so it carries no dedicated unit test — the builder and emitter are already covered (Tasks 1-2), and end-to-end behaviour is confirmed by the full-suite run below plus live verification. It completes the feature, so the contract doc lands in the same commit.

- [ ] **Step 1: Emit after the OSC set**

In `src/CustosOscServer.cpp`, change the `Command::MainLR` case from:

```cpp
        case Command::MainLR:
            proc.setMainLROnly (cmd.mainLROn);
            break;
```

to:

```cpp
        case Command::MainLR:
            proc.setMainLROnly (cmd.mainLROn);
            proc.emitMainLR();   // confirm the applied fold
            break;
```

- [ ] **Step 2: Add the query case**

In the same switch, immediately after the `Command::MainLR` case, add:

```cpp
        case Command::MainLRQuery:
            proc.emitMainLR();
            break;
```

- [ ] **Step 3: Update the contract doc — §2 (KM → Custos)**

In `docs/osc-contract.md`, in the §2 table, immediately after the `/custos/mainlr` row, add:

```markdown
| `/custos/mainlr/query` | — | → replies `/custos/mainlr` with the current fold flag (no state change) |
```

- [ ] **Step 4: Update the contract doc — §3 (Custos → KM)**

In the §3 table, immediately after the `/custos/midi/route` feedback row, add:

```markdown
| `/custos/mainlr` | `N, on:int` | Main L/R fold feedback — the current flag (`1` = all inner outputs summed onto stereo Out 1, `0` = inner pairs mapped across the 5 buses). Emitted after an applied `/custos/mainlr` set and in reply to `/custos/mainlr/query` |
```

- [ ] **Step 5: Update the contract doc — §4 Main L/R bullet**

In `docs/osc-contract.md` §4, replace the bullet:

```markdown
- **Main L/R only is an audio-fold**: `/custos/mainlr 1` sums all inner outputs onto stereo Out 1; `0`
  maps inner pairs across the 5 out buses. Set-only (no feedback), persisted in Custos state v4;
  the local editor toggle mirrors the same flag.
```

with:

```markdown
- **Main L/R only is an audio-fold**: `/custos/mainlr 1` sums all inner outputs onto stereo Out 1; `0`
  maps inner pairs across the 5 out buses. Persisted in Custos state v4; the local editor toggle mirrors
  the same flag. **Observable:** an applied set emits `/custos/mainlr <N, on>` feedback, and
  `/custos/mainlr/query` returns the current flag on demand. The editor toggle does **not** emit — KM
  re-queries to resync after an operator-driven UI flip.
```

- [ ] **Step 6: Build and run the FULL suite to verify no regressions**

Run: `cmd //c "C:\Users\marti\AppData\Local\Temp\claude\C--dev\69381d00-225b-4849-805f-4990151335f2\scratchpad\build_tests.cmd"`
Then: `"C:/dev/custos/build/tests/custos_tests.exe"`
Expected: PASS — all test cases, all assertions green (the earlier suite reported 265 assertions; expect that plus the new ones).

- [ ] **Step 7: Commit**

```bash
cd /c/dev/custos && git add src/CustosOscServer.cpp docs/osc-contract.md && git commit -m "feat: emit /custos/mainlr feedback on set + query; document contract

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Notes for after the plan

- **Live E2E** (outside this plan's unit scope): with a Custos instance bound, send `/custos/mainlr/query` from `custos/tools/` and confirm a `/custos/mainlr <N, on>` reply on the hub `:8000`; send `/custos/mainlr 1`/`0` and confirm the echo matches. Deploy via `scripts\deploy.cmd` if a fresh `.vst3` is needed (mind the Debug/Release stale-bundle gotcha).
- **KM handoff:** after merge, the KM side gains a read path for Main L/R — deliver a short prompt (new query verb `/custos/mainlr/query`, feedback `/custos/mainlr <N, on>`, protoVer still 2, detect support by reply-or-timeout).
- **Branch:** already on `custos-mainlr-feedback`. Finish → push + PR + deploy:test; never auto-merge (Martin merges after E2E).
