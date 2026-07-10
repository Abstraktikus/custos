# Custos Instrument-DB Ownership — Implementation Plan (Part A: Custos)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give Custos everything it needs to own the full instrument database and execute the Instrument / Patch / Preset axes, so GP-Script can later become a dumb OSC-trigger surface (Part B, separate plan).

**Architecture:** Extend the existing favourites store into a full instrument list carrying preset-stepping metadata (`controlType`/`paramDown`/`paramUp`), add stateless scope to instrument browsing, add load-by-name, and add a Patch axis that dispatches by `controlType` (PARAM param-inject / PC program-change / PRESET-store fallback). All new wire verbs extend the v2 contract back-compatibly.

**Tech Stack:** C++17, JUCE 8, Catch2, CMake+Ninja (MSVC). Build: `scripts\build.cmd custos_tests Custos` (self-sets-up MSVC via `_vsenv.cmd`; run from `c:\dev\custos`). Test suite: `scripts\test.cmd` (ctest). Filter one Catch2 case: `build\tests\custos_tests.exe "<test name>"` (Ninja single-config — no `Debug/` subfolder).

## Global Constraints

- **Contract version:** bump `kProtoVersion` 2 → 3 (`src/OscContract.h`); document every new verb in `docs/osc-contract.md`.
- **Backward-compatible wire changes only:** new args are appended and optional; existing v2 messages must still parse identically. Never reorder or retype existing args.
- **controlType domain:** `PARAM | PC | PRESET | NONE`. Only `PARAM` and `PC` are native; **any other value (incl. empty) falls back to the Preset store.** `PC` is never inferred — it is used only when explicitly declared.
- **Naming:** the three axes are **Instrument / Patch / Preset**. The label "HOST" does not appear anywhere.
- **Single-step only:** no bank/library (coarse) verbs in this plan.
- **Trim precedence needs no new Custos code:** on load Custos applies the list `gainDb` (`applyVolumeDefault`); a later `/custos/volume` naturally outranks it (last write wins). Only the doc records this (Task 11). GP authoring/writing of trim is out of scope (Part B removes it).
- **Message-thread only** for all new `CustosProcessor` methods, matching the existing browse/preset methods. The audio thread touches inner state only through the existing atomics/spin-lock.

---

### Task 1: Instrument-list metadata fields (controlType / paramDown / paramUp)

**Files:**
- Modify: `src/FavoritesStore.h`
- Modify: `src/FavoritesStore.cpp`
- Test: `tests/FavoritesStoreTest.cpp`

**Interfaces:**
- Produces: `Favorite` gains `juce::String controlType = "PRESET"; int paramDown = 0; int paramUp = 0;`. `favoritesToJson`/`favoritesFromJson` round-trip them; a legacy JSON entry without the keys reads back `controlType == "PRESET"`, `paramDown == 0`, `paramUp == 0`.

- [ ] **Step 1: Write the failing test**

Add to `tests/FavoritesStoreTest.cpp`:

```cpp
TEST_CASE ("Favorite round-trips controlType / paramDown / paramUp")
{
    std::vector<custos::Favorite> in;
    custos::Favorite f;
    f.name = "Analog Lab V"; f.path = "C:/AL.vst3"; f.favOrder = 3; f.gainDb = -3.0f;
    f.brand = "Arturia"; f.slots = 500;
    f.controlType = "PARAM"; f.paramDown = 498; f.paramUp = 499;
    in.push_back (f);

    const auto out = custos::favoritesFromJson (custos::favoritesToJson (in));
    REQUIRE (out.size() == 1);
    REQUIRE (out[0].controlType == "PARAM");
    REQUIRE (out[0].paramDown == 498);
    REQUIRE (out[0].paramUp  == 499);
}

TEST_CASE ("legacy Favorite JSON without controlType defaults to PRESET")
{
    const auto out = custos::favoritesFromJson (R"([{"name":"X","path":"C:/x.vst3","favOrder":1}])");
    REQUIRE (out.size() == 1);
    REQUIRE (out[0].controlType == "PRESET");
    REQUIRE (out[0].paramDown == 0);
    REQUIRE (out[0].paramUp  == 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "Favorite round-trips controlType / paramDown / paramUp"`
Expected: FAIL — `controlType`/`paramDown`/`paramUp` are not members of `Favorite`.

- [ ] **Step 3: Add the struct fields**

In `src/FavoritesStore.h`, extend the struct (keep the existing fields):

```cpp
struct Favorite { juce::String name, path; int favOrder = 0; float gainDb = 0.0f; juce::String brand;
                  int slots = 0;                                   // synth's param count (0 = unknown)
                  juce::String controlType = "PRESET";             // PARAM | PC | PRESET | NONE
                  int paramDown = 0, paramUp = 0; };               // inject indices (PARAM only)
```

- [ ] **Step 4: Serialise/deserialise the new fields**

In `src/FavoritesStore.cpp`, inside `favoritesToJson`'s loop, after `o->setProperty ("slots", f.slots);` add:

```cpp
        o->setProperty ("controlType", f.controlType);
        o->setProperty ("paramDown", f.paramDown);
        o->setProperty ("paramUp", f.paramUp);
```

In `favoritesFromJson`, after `f.slots = (int) v.getProperty ("slots", 0);` add:

```cpp
            f.controlType = v.getProperty ("controlType", "PRESET").toString();
            f.paramDown   = (int) v.getProperty ("paramDown", 0);
            f.paramUp     = (int) v.getProperty ("paramUp", 0);
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "[.]Favorite*"`
Expected: PASS (both new cases + existing FavoritesStore cases).

- [ ] **Step 6: Commit**

```bash
git add src/FavoritesStore.h src/FavoritesStore.cpp tests/FavoritesStoreTest.cpp
git commit -m "feat(custos): Favorite carries controlType/paramDown/paramUp"
```

---

### Task 2: Rename the on-disk config to instruments.json (with legacy migration)

**Files:**
- Modify: `src/FavoritesStore.h`
- Modify: `src/FavoritesStore.cpp`
- Modify: `src/CustosOscServer.cpp` (boot-load + write path)
- Test: `tests/FavoritesStoreTest.cpp`

**Interfaces:**
- Produces: `juce::File instrumentsConfigFile();` → `%APPDATA%/Custos/instruments.json`. `std::vector<Favorite> readInstruments (const juce::File& newFile, const juce::File& legacyFile);` returns `newFile`'s entries if it exists, else `legacyFile`'s (migration read), else empty. `favoritesConfigFile()` stays (points at the legacy `favorites.json`) so the migration can read it.

- [ ] **Step 1: Write the failing test**

Add to `tests/FavoritesStoreTest.cpp`:

```cpp
TEST_CASE ("readInstruments prefers the new file, else migrates the legacy file")
{
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("custos_migtest");
    tmp.createDirectory();
    auto legacy = tmp.getChildFile ("favorites.json");
    auto neu    = tmp.getChildFile ("instruments.json");
    legacy.deleteFile(); neu.deleteFile();

    custos::writeFavorites (legacy, { { "L", "C:/l.vst3", 1, 0.0f } });
    auto migrated = custos::readInstruments (neu, legacy);   // new absent -> read legacy
    REQUIRE (migrated.size() == 1);
    REQUIRE (migrated[0].name == "L");

    custos::writeFavorites (neu, { { "N", "C:/n.vst3", 1, 0.0f } });
    auto fresh = custos::readInstruments (neu, legacy);      // new present -> ignore legacy
    REQUIRE (fresh.size() == 1);
    REQUIRE (fresh[0].name == "N");

    legacy.deleteFile(); neu.deleteFile(); tmp.deleteRecursively();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "readInstruments prefers the new file, else migrates the legacy file"`
Expected: FAIL — `instrumentsConfigFile`/`readInstruments` undeclared.

- [ ] **Step 3: Declare the new helpers**

In `src/FavoritesStore.h`, after the existing `favoritesConfigFile()` line add:

```cpp
juce::File instrumentsConfigFile();                                          // %APPDATA%/Custos/instruments.json
std::vector<Favorite> readInstruments (const juce::File& newFile, const juce::File& legacyFile); // new, else migrate legacy
```

- [ ] **Step 4: Implement them**

In `src/FavoritesStore.cpp`, after `favoritesConfigFile()`'s definition add:

```cpp
juce::File instrumentsConfigFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
              .getChildFile ("Custos").getChildFile ("instruments.json");
}

std::vector<Favorite> readInstruments (const juce::File& newFile, const juce::File& legacyFile)
{
    if (newFile.existsAsFile())    return readFavorites (newFile);
    if (legacyFile.existsAsFile()) return readFavorites (legacyFile);   // one-time migration read
    return {};
}
```

- [ ] **Step 5: Point the processor's boot-load and write at the new file**

In `src/CustosOscServer.cpp`, constructor, replace:

```cpp
    proc.setFavorites (readFavorites (favoritesConfigFile()));   // boot-load the shared machine config
```
with:
```cpp
    proc.setFavorites (readInstruments (instrumentsConfigFile(), favoritesConfigFile()));  // new, else migrate legacy
```

In `oscMessageReceived`, `case Command::FavEnd`, replace:

```cpp
            writeFavorites (favoritesConfigFile(), proc.getFavorites());   // shared machine config
```
with:
```cpp
            writeFavorites (instrumentsConfigFile(), proc.getFavorites());   // shared machine config
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `scripts\build.cmd custos_tests Custos && scripts\test.cmd`
Expected: PASS (new migration case + full suite; Custos target still links).

- [ ] **Step 7: Commit**

```bash
git add src/FavoritesStore.h src/FavoritesStore.cpp src/CustosOscServer.cpp tests/FavoritesStoreTest.cpp
git commit -m "feat(custos): instruments.json store with one-time favorites.json migration"
```

---

### Task 3: Widen /custos/favorite to carry controlType/paramDown/paramUp

**Files:**
- Modify: `src/CustosOscServer.cpp` (`parseCommand`)
- Test: `tests/OscCommandTest.cpp`

**Interfaces:**
- Consumes: `Favorite.controlType/paramDown/paramUp` (Task 1).
- Produces: `/custos/favorite` accepts optional args 8/9/10 = `controlType:string, paramDown:int, paramUp:int`, populating `Command.fav`. Messages with ≤7 args parse exactly as before.

- [ ] **Step 1: Write the failing test**

Add to `tests/OscCommandTest.cpp`:

```cpp
TEST_CASE ("parseCommand reads controlType/paramDown/paramUp on /custos/favorite")
{
    juce::OSCMessage m ("/custos/favorite");
    m.addInt32 (0); m.addString ("Analog Lab V"); m.addString ("C:/AL.vst3");
    m.addInt32 (3); m.addFloat32 (-3.0f); m.addString ("Arturia"); m.addInt32 (500);
    m.addString ("PARAM"); m.addInt32 (498); m.addInt32 (499);
    const auto c = parseCommand (m);
    REQUIRE (c.kind == Command::FavEntry);
    REQUIRE (c.fav.controlType == "PARAM");
    REQUIRE (c.fav.paramDown == 498);
    REQUIRE (c.fav.paramUp  == 499);
}

TEST_CASE ("parseCommand keeps v2 /custos/favorite (5 args) working")
{
    juce::OSCMessage m ("/custos/favorite");
    m.addInt32 (0); m.addString ("X"); m.addString ("C:/x.vst3"); m.addInt32 (1); m.addFloat32 (0.0f);
    const auto c = parseCommand (m);
    REQUIRE (c.kind == Command::FavEntry);
    REQUIRE (c.fav.controlType == "PRESET");   // struct default
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "parseCommand reads controlType/paramDown/paramUp on /custos/favorite"`
Expected: FAIL — `c.fav.controlType` is the default, not "PARAM".

- [ ] **Step 3: Extend the parser**

In `src/CustosOscServer.cpp`, in `parseCommand`, inside the `/custos/favorite` block, after the existing `slots` line (`if (msg.size() >= 7 ...) c.fav.slots = msg[6].getInt32();`) add:

```cpp
            if (msg.size() >= 8 && msg[7].isString())   // controlType optional 8th arg
                c.fav.controlType = msg[7].getString();
            if (msg.size() >= 9 && msg[8].isInt32())    // paramDown optional 9th arg
                c.fav.paramDown = msg[8].getInt32();
            if (msg.size() >= 10 && msg[9].isInt32())   // paramUp optional 10th arg
                c.fav.paramUp = msg[9].getInt32();
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "[.]parseCommand*favorite*"`
Expected: PASS (both new cases).

- [ ] **Step 5: Commit**

```bash
git add src/CustosOscServer.cpp tests/OscCommandTest.cpp
git commit -m "feat(custos): /custos/favorite carries controlType/paramDown/paramUp (back-compat)"
```

---

### Task 4: Stateless browse scope (fav vs all)

**Files:**
- Modify: `src/InstrumentBrowser.h` (pure scope predicate)
- Modify: `src/CustosOscServer.h` (`Command.scope`)
- Modify: `src/CustosOscServer.cpp` (`parseCommand` + dispatch)
- Modify: `src/CustosProcessor.h` / `src/CustosProcessor.cpp` (`browseInstrument(delta, scope)`)
- Test: `tests/InstrumentBrowserTest.cpp`, `tests/OscCommandTest.cpp`

**Interfaces:**
- Produces: `inline bool favouriteInScope (int favOrder, int scope)` in `InstrumentBrowser.h` (`scope 0` = favourites only → `favOrder >= 1`; `scope 1` = all → always true). `CustosProcessor::browseInstrument (int delta, int scope)` — default `scope = 0` preserves the current signature's callers. `/custos/instrument/next|prev` read an optional `scope:int` (default 0) into `Command.scope`.

- [ ] **Step 1: Write the failing pure-predicate test**

Add to `tests/InstrumentBrowserTest.cpp`:

```cpp
TEST_CASE ("favouriteInScope: scope 0 = favourites only, scope 1 = all")
{
    REQUIRE (custos::favouriteInScope (1, 0) == true);    // fav, fav-scope
    REQUIRE (custos::favouriteInScope (0, 0) == false);   // non-fav, fav-scope
    REQUIRE (custos::favouriteInScope (0, 1) == true);    // non-fav, all-scope
    REQUIRE (custos::favouriteInScope (5, 1) == true);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "favouriteInScope: scope 0 = favourites only, scope 1 = all"`
Expected: FAIL — `favouriteInScope` undeclared.

- [ ] **Step 3: Add the pure predicate**

In `src/InstrumentBrowser.h`, inside `namespace custos`, after `favouriteFits`:

```cpp
// Browse-scope predicate. scope 0 = favourites only (favOrder >= 1); scope 1 = all instruments.
inline bool favouriteInScope (int favOrder, int scope) { return scope != 0 || favOrder >= 1; }
```

- [ ] **Step 4: Run the predicate test — PASS**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "favouriteInScope: scope 0 = favourites only, scope 1 = all"`
Expected: PASS.

- [ ] **Step 5: Write the failing parse test**

Add to `tests/OscCommandTest.cpp`:

```cpp
TEST_CASE ("parseCommand reads optional scope on /custos/instrument/next|prev")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/instrument/next")).scope == 0);      // default
    juce::OSCMessage all ("/custos/instrument/next"); all.addInt32 (1);
    const auto c = parseCommand (all);
    REQUIRE (c.kind == Command::BrowseNext);
    REQUIRE (c.scope == 1);
}
```

- [ ] **Step 6: Run it to verify it fails**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "parseCommand reads optional scope on /custos/instrument/next|prev"`
Expected: FAIL — `Command` has no `scope` member.

- [ ] **Step 7: Add `scope` to Command and parse it**

In `src/CustosOscServer.h`, in `struct Command`, add to the int-fields line group:

```cpp
    int scope = 0;              // BrowseNext/Prev: 0 = favourites, 1 = all
```

In `src/CustosOscServer.cpp`, `parseCommand`, replace the two lines:

```cpp
    if (addr == "/custos/instrument/next")
        return { Command::BrowseNext, {} };
    if (addr == "/custos/instrument/prev")
        return { Command::BrowsePrev, {} };
```
with:
```cpp
    if (addr == "/custos/instrument/next" || addr == "/custos/instrument/prev")
    {
        Command c; c.kind = (addr.endsWith ("next")) ? Command::BrowseNext : Command::BrowsePrev;
        if (msg.size() >= 1 && msg[0].isInt32()) c.scope = msg[0].getInt32();
        return c;
    }
```

- [ ] **Step 8: Thread scope through the processor call**

In `src/CustosProcessor.h`, change the declaration:

```cpp
    void browseInstrument (int delta, int scope = 0);
```

In `src/CustosProcessor.cpp`, change `browseInstrument`'s signature to `void CustosProcessor::browseInstrument (int delta, int scope)` and change the fit-skip loop's break condition to also honour scope:

```cpp
        if (favouriteFits (favorites[(size_t) idx].slots, cap)
            && favouriteInScope (favorites[(size_t) idx].favOrder, scope)) break;
```

In `src/CustosOscServer.cpp`, `oscMessageReceived`, update the browse cases:

```cpp
        case Command::BrowseNext:
            proc.browseInstrument (+1, cmd.scope);
            break;
        case Command::BrowsePrev:
            proc.browseInstrument (-1, cmd.scope);
            break;
```

- [ ] **Step 9: Run the full suite — PASS**

Run: `scripts\build.cmd custos_tests Custos && scripts\test.cmd`
Expected: PASS (predicate + parse + existing browse tests; Custos links).

- [ ] **Step 10: Commit**

```bash
git add src/InstrumentBrowser.h src/CustosOscServer.h src/CustosOscServer.cpp src/CustosProcessor.h src/CustosProcessor.cpp tests/InstrumentBrowserTest.cpp tests/OscCommandTest.cpp
git commit -m "feat(custos): stateless browse scope (favourites vs all) on instrument/next|prev"
```

---

### Task 5: /custos/instrument/load <name> — resolve name→path in Custos

**Files:**
- Modify: `src/CustosOscServer.h` (`Command` kind + field)
- Modify: `src/CustosOscServer.cpp` (`parseCommand` + dispatch)
- Modify: `src/CustosProcessor.h` / `src/CustosProcessor.cpp` (`loadByName`)
- Test: `tests/OscCommandTest.cpp`, `tests/InstrumentLoadTest.cpp` (new)

**Interfaces:**
- Consumes: `getFavorites()` (name/path).
- Produces: `bool CustosProcessor::loadByName (const juce::String& name);` — finds the first list entry whose `name == name`, calls `load(path)`, returns true; returns false (no load) if no match. `/custos/instrument/load` (`name:string`) → `Command::InstrumentLoad`, `Command.path` holds the name.

- [ ] **Step 1: Write the failing parse test**

Add to `tests/OscCommandTest.cpp`:

```cpp
TEST_CASE ("parseCommand maps /custos/instrument/load with a name")
{
    juce::OSCMessage m ("/custos/instrument/load", juce::String ("Analog Lab V"));
    const auto c = parseCommand (m);
    REQUIRE (c.kind == Command::InstrumentLoad);
    REQUIRE (c.path == "Analog Lab V");
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "parseCommand maps /custos/instrument/load with a name"`
Expected: FAIL — `Command::InstrumentLoad` undeclared.

- [ ] **Step 3: Add the command kind and parse rule**

In `src/CustosOscServer.h`, add `InstrumentLoad` to the `Kind` enum (next to `BrowseSet`).

In `src/CustosOscServer.cpp`, `parseCommand`, after the `/custos/instrument/set` block add:

```cpp
    if (addr == "/custos/instrument/load")
    {
        if (msg.size() >= 1 && msg[0].isString())
            return { Command::InstrumentLoad, msg[0].getString() };
        return { Command::Unknown, {} };
    }
```

- [ ] **Step 4: Run the parse test — PASS**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "parseCommand maps /custos/instrument/load with a name"`
Expected: PASS.

- [ ] **Step 5: Write the failing processor test**

Create `tests/InstrumentLoadTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"

using namespace custos;

TEST_CASE ("loadByName resolves a list entry's path; unknown name is a no-op")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setFavorites ({ { "Diva", "C:/does/not/exist/Diva.vst3", 1, 0.0f } });

    // Unknown name -> false, no synth.
    REQUIRE (proc.loadByName ("Nope") == false);
    REQUIRE (proc.hasInnerSynth() == false);

    // Known name -> load is attempted with the mapped path. The .vst3 is fake, so the actual
    // plugin load fails and no inner is attached, but the resolver returned true (name matched).
    REQUIRE (proc.loadByName ("Diva") == true);
}
```

Register it in `tests/CMakeLists.txt` (add `InstrumentLoadTest.cpp` to the test-sources list next to `FavoritesTest.cpp`).

- [ ] **Step 6: Run it to verify it fails**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "loadByName resolves a list entry's path; unknown name is a no-op"`
Expected: FAIL — `loadByName` undeclared.

- [ ] **Step 7: Implement `loadByName`**

In `src/CustosProcessor.h`, near `loadFavorite`:

```cpp
    bool loadByName (const juce::String& name);   // resolve name->path from the list; load; false if no match
```

In `src/CustosProcessor.cpp`, near `loadFavorite`:

```cpp
bool CustosProcessor::loadByName (const juce::String& name)
{
    for (const auto& f : favorites)
        if (f.name == name) { load (f.path); return true; }
    return false;
}
```

- [ ] **Step 8: Dispatch the OSC command**

In `src/CustosOscServer.cpp`, `oscMessageReceived`, after the `BrowseSet` case:

```cpp
        case Command::InstrumentLoad:
        {
            const bool ok = proc.loadByName (cmd.path);
            if (! ok) ack ("error unknown instrument " + cmd.path);
            break;   // success is conveyed by /custos/loaded (emitted inside load())
        }
```

- [ ] **Step 9: Run the full suite — PASS**

Run: `scripts\build.cmd custos_tests Custos && scripts\test.cmd`
Expected: PASS.

- [ ] **Step 10: Commit**

```bash
git add src/CustosOscServer.h src/CustosOscServer.cpp src/CustosProcessor.h src/CustosProcessor.cpp tests/OscCommandTest.cpp tests/InstrumentLoadTest.cpp tests/CMakeLists.txt
git commit -m "feat(custos): /custos/instrument/load <name> resolves name->path internally"
```

---

### Task 6: Patch-method resolution (pure)

**Files:**
- Modify: `src/InstrumentBrowser.h` (pure helper — it already houses the pure browse helpers)
- Test: `tests/PatchStepTest.cpp` (new)

**Interfaces:**
- Produces: `enum class PatchMethod { Param, Pc, PresetFallback };` and `inline PatchMethod patchMethodFor (const juce::String& controlType)` in `namespace custos` — `"PARAM"` → `Param`, `"PC"` → `Pc`, anything else (incl. `"PRESET"`, `"NONE"`, empty) → `PresetFallback`.

- [ ] **Step 1: Write the failing test**

Create `tests/PatchStepTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "InstrumentBrowser.h"

using namespace custos;

TEST_CASE ("patchMethodFor: PARAM/PC native, everything else falls back to Preset")
{
    REQUIRE (patchMethodFor ("PARAM")  == PatchMethod::Param);
    REQUIRE (patchMethodFor ("PC")     == PatchMethod::Pc);
    REQUIRE (patchMethodFor ("PRESET") == PatchMethod::PresetFallback);
    REQUIRE (patchMethodFor ("NONE")   == PatchMethod::PresetFallback);
    REQUIRE (patchMethodFor ("")       == PatchMethod::PresetFallback);
    REQUIRE (patchMethodFor ("host")   == PatchMethod::PresetFallback);   // no such type any more
}
```

Register `PatchStepTest.cpp` in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run it to verify it fails**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "patchMethodFor: PARAM/PC native, everything else falls back to Preset"`
Expected: FAIL — `PatchMethod`/`patchMethodFor` undeclared.

- [ ] **Step 3: Add the helper**

In `src/InstrumentBrowser.h`, inside `namespace custos`:

```cpp
enum class PatchMethod { Param, Pc, PresetFallback };

// Explicit-only: PC is never inferred. Anything not PARAM/PC (incl. PRESET, NONE, empty) falls
// back to the Preset store — the only implicit method, always available.
inline PatchMethod patchMethodFor (const juce::String& controlType)
{
    if (controlType == "PARAM") return PatchMethod::Param;
    if (controlType == "PC")    return PatchMethod::Pc;
    return PatchMethod::PresetFallback;
}
```

- [ ] **Step 4: Run the test — PASS**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "patchMethodFor: PARAM/PC native, everything else falls back to Preset"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/InstrumentBrowser.h tests/PatchStepTest.cpp tests/CMakeLists.txt
git commit -m "feat(custos): pure patchMethodFor resolver (PARAM/PC native, else Preset)"
```

---

### Task 7: Patch axis — verbs + PRESET fallback path

**Files:**
- Modify: `src/CustosOscServer.h` (`Command` kinds)
- Modify: `src/CustosOscServer.cpp` (`parseCommand` + dispatch)
- Modify: `src/CustosProcessor.h` / `src/CustosProcessor.cpp` (`patchNext`/`patchPrev`/`patchStep`)
- Test: `tests/OscCommandTest.cpp`, `tests/PatchStepTest.cpp`

**Interfaces:**
- Consumes: `patchMethodFor` (Task 6), `presetNext()`/`presetPrev()` (existing), `currentPath()`/`getFavorites()`.
- Produces: `void CustosProcessor::patchNext();` / `void patchPrev();` and `void patchStep (int delta);`. `patchStep` looks up the loaded synth's list entry by `currentPath()`; `PresetFallback` (or no match) → `presetNext()/presetPrev()`. `/custos/patch/next` → `Command::PatchNext`; `/custos/patch/prev` → `Command::PatchPrev`.

- [ ] **Step 1: Write the failing parse test**

Add to `tests/OscCommandTest.cpp`:

```cpp
TEST_CASE ("parseCommand maps /custos/patch/next and /custos/patch/prev")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/patch/next")).kind == Command::PatchNext);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/patch/prev")).kind == Command::PatchPrev);
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "parseCommand maps /custos/patch/next and /custos/patch/prev"`
Expected: FAIL — `Command::PatchNext` undeclared.

- [ ] **Step 3: Add command kinds + parse rules**

In `src/CustosOscServer.h`, add `PatchNext, PatchPrev` to the `Kind` enum.

In `src/CustosOscServer.cpp`, `parseCommand`, after the `/custos/instrument/load` block:

```cpp
    if (addr == "/custos/patch/next") { Command c; c.kind = Command::PatchNext; return c; }
    if (addr == "/custos/patch/prev") { Command c; c.kind = Command::PatchPrev; return c; }
```

- [ ] **Step 4: Run the parse test — PASS**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "parseCommand maps /custos/patch/next and /custos/patch/prev"`
Expected: PASS.

- [ ] **Step 5: Write the failing fallback behaviour test**

Add to `tests/PatchStepTest.cpp`:

```cpp
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"
using custos::test::FakeInnerProcessor;

TEST_CASE ("patchStep on a PRESET/unknown synth falls back to the preset cursor")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    // No matching favourite for the loaded path -> PresetFallback. With no presets on disk the
    // preset cursor cannot advance, so this must not crash and must not touch inner params.
    proc.loadInner (std::make_unique<FakeInnerProcessor> (4), "C:/unlisted.vst3");
    REQUIRE_NOTHROW (proc.patchNext());
    REQUIRE_NOTHROW (proc.patchPrev());
}
```

- [ ] **Step 6: Run it to verify it fails**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "patchStep on a PRESET/unknown synth falls back to the preset cursor"`
Expected: FAIL — `patchNext`/`patchPrev` undeclared.

- [ ] **Step 7: Implement patch dispatch (Param/Pc branches are stubs until Tasks 8–9)**

In `src/CustosProcessor.h`, near `presetNext`:

```cpp
    void patchNext();               // step the factory sound forward (controlType dispatch)
    void patchPrev();               // step the factory sound backward
    void patchStep (int delta);     // shared: resolve controlType of the loaded synth and dispatch
```

In `src/CustosProcessor.cpp`, add (after `presetPrev`/`presetSet` region), and `#include "InstrumentBrowser.h"` at the top if not already present:

```cpp
void CustosProcessor::patchNext() { patchStep (+1); }
void CustosProcessor::patchPrev() { patchStep (-1); }

void CustosProcessor::patchStep (int delta)
{
    juce::String controlType = "PRESET";
    int pDown = 0, pUp = 0;
    for (const auto& f : favorites)
        if (f.path == currentSynthPath) { controlType = f.controlType; pDown = f.paramDown; pUp = f.paramUp; break; }

    switch (patchMethodFor (controlType))
    {
        case PatchMethod::Param:          patchInjectParam (delta > 0 ? pUp : pDown); break;   // Task 8
        case PatchMethod::Pc:             patchSendProgramChange (delta);            break;   // Task 9
        case PatchMethod::PresetFallback: if (delta > 0) presetNext(); else presetPrev(); break;
    }
}
```

Add temporary no-op private stubs so this compiles now (Tasks 8/9 replace the bodies):

In `src/CustosProcessor.h` (private section):

```cpp
    void patchInjectParam (int paramIndex);       // Task 8
    void patchSendProgramChange (int delta);      // Task 9
```

In `src/CustosProcessor.cpp`:

```cpp
void CustosProcessor::patchInjectParam (int) {}         // Task 8 fills this in
void CustosProcessor::patchSendProgramChange (int) {}   // Task 9 fills this in
```

- [ ] **Step 8: Dispatch the OSC commands**

In `src/CustosOscServer.cpp`, `oscMessageReceived`, after the `InstrumentLoad` case:

```cpp
        case Command::PatchNext:
            proc.patchNext();
            break;
        case Command::PatchPrev:
            proc.patchPrev();
            break;
```

- [ ] **Step 9: Run the fallback test + suite — PASS**

Run: `scripts\build.cmd custos_tests Custos && scripts\test.cmd`
Expected: PASS (fallback is exercised; Param/Pc are no-ops for now).

- [ ] **Step 10: Commit**

```bash
git add src/CustosOscServer.h src/CustosOscServer.cpp src/CustosProcessor.h src/CustosProcessor.cpp tests/OscCommandTest.cpp tests/PatchStepTest.cpp
git commit -m "feat(custos): patch/next|prev verbs + PRESET fallback (Param/Pc stubbed)"
```

---

### Task 8: PARAM patch inject

**Files:**
- Modify: `src/CustosProcessor.h` / `src/CustosProcessor.cpp`
- Test: `tests/PatchStepTest.cpp`

**Interfaces:**
- Produces: `patchInjectParam(int paramIndex)` sets the inner synth's parameter `paramIndex` to `1.0f` and arms a one-shot timer (~150 ms) that resets it to `0.0f`. Out-of-range index or no inner = no-op. Reuses the `DebounceTimer` struct already declared for browse/preset.

- [ ] **Step 1: Write the failing test**

Add to `tests/PatchStepTest.cpp`:

```cpp
#include <catch2/catch_approx.hpp>
using Catch::Approx;

TEST_CASE ("PARAM patchStep injects 1.0 into the paramUp/paramDown index")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    auto fake = std::make_unique<FakeInnerProcessor> (4);
    auto* fakePtr = fake.get();
    proc.loadInner (std::move (fake), "C:/AL.vst3");
    proc.setFavorites ({ [] { custos::Favorite f; f.name="AL"; f.path="C:/AL.vst3"; f.favOrder=1;
                              f.controlType="PARAM"; f.paramDown=2; f.paramUp=3; return f; }() });

    proc.patchNext();   // -> paramUp = index 3 set to 1.0
    REQUIRE (fakePtr->getParameters()[3]->getValue() == Approx (1.0f));

    proc.patchPrev();   // -> paramDown = index 2 set to 1.0
    REQUIRE (fakePtr->getParameters()[2]->getValue() == Approx (1.0f));
}

TEST_CASE ("PARAM patchStep with an out-of-range index is a no-op")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.loadInner (std::make_unique<FakeInnerProcessor> (2), "C:/AL.vst3");
    proc.setFavorites ({ [] { custos::Favorite f; f.name="AL"; f.path="C:/AL.vst3"; f.favOrder=1;
                              f.controlType="PARAM"; f.paramDown=0; f.paramUp=9; return f; }() });
    REQUIRE_NOTHROW (proc.patchNext());   // index 9 >= 2 params -> no-op, no crash
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "PARAM patchStep injects 1.0 into the paramUp/paramDown index"`
Expected: FAIL — parameter stays at its default (stub is a no-op).

- [ ] **Step 3: Implement the inject + reset timer**

In `src/CustosProcessor.h`, private section, add the reset timer + pending index (place near `presetDebounce`):

```cpp
    DebounceTimer patchInjectTimer;   // reuse DebounceTimer; resets the injected param after the hold
    int patchInjectIndex = -1;        // param index currently held at 1.0 (-1 = none)
```

In `src/CustosProcessor.cpp`, replace the Task-7 stub `patchInjectParam` body:

```cpp
void CustosProcessor::patchInjectParam (int paramIndex)
{
    if (inner == nullptr) return;
    auto& params = inner->getParameters();
    if (paramIndex < 0 || paramIndex >= params.size()) return;   // out of range -> no-op

    params[paramIndex]->setValueNotifyingHost (1.0f);            // momentary press
    patchInjectIndex = paramIndex;
    patchInjectTimer.cb = [this]
    {
        if (inner != nullptr && patchInjectIndex >= 0 && patchInjectIndex < inner->getParameters().size())
            inner->getParameters()[patchInjectIndex]->setValueNotifyingHost (0.0f);   // release
        patchInjectIndex = -1;
    };
    patchInjectTimer.startTimer (150);   // release ~150 ms later (heavy synths need the hold)
}
```

- [ ] **Step 4: Run the tests — PASS**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "[.]PARAM patchStep*"`
Expected: PASS (the 1.0 set is synchronous; the 150 ms release is verified in the live E2E harness, not here).

- [ ] **Step 5: Commit**

```bash
git add src/CustosProcessor.h src/CustosProcessor.cpp tests/PatchStepTest.cpp
git commit -m "feat(custos): PARAM patch inject (momentary paramUp/paramDown, timed release)"
```

---

### Task 9: PC patch inject

**Files:**
- Modify: `src/CustosProcessor.h` / `src/CustosProcessor.cpp`
- Test: `tests/PatchStepTest.cpp`

**Interfaces:**
- Produces: `patchSendProgramChange(int delta)` advances an internal program counter (0..127, wrapping) and enqueues a Program Change (MIDI channel 1) that `processBlock` injects into the inner synth on the next block. `int pendingProgram() const` exposes the queued program (−1 = none) for tests.

- [ ] **Step 1: Write the failing test**

Add to `tests/PatchStepTest.cpp`:

```cpp
TEST_CASE ("PC patchStep queues a wrapping program change that processBlock injects")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    auto fake = std::make_unique<FakeInnerProcessor> (2);
    auto* fakePtr = fake.get();
    proc.prepareToPlay (48000.0, 64);
    proc.loadInner (std::move (fake), "C:/Korg.vst3");
    proc.setFavorites ({ [] { custos::Favorite f; f.name="Korg"; f.path="C:/Korg.vst3"; f.favOrder=1;
                              f.controlType="PC"; return f; }() });

    proc.patchNext();                       // program 0 -> 1
    REQUIRE (proc.pendingProgram() == 1);

    juce::AudioBuffer<float> buf (2, 64);
    juce::MidiBuffer midi;
    proc.processBlock (buf, midi);          // injects the queued PC into the inner
    REQUIRE (proc.pendingProgram() == -1);  // consumed
    REQUIRE (fakePtr->lastNumMidi >= 1);    // the inner saw a MIDI event
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "PC patchStep queues a wrapping program change that processBlock injects"`
Expected: FAIL — `pendingProgram` undeclared / program not injected.

- [ ] **Step 3: Add the atomic queue + counter**

In `src/CustosProcessor.h`, private section:

```cpp
    int pcProgram = 0;                              // last program sent (0..127); message thread only
    std::atomic<int> pendingPc { -1 };              // program queued for the next processBlock (-1 = none)
```

In `src/CustosProcessor.h`, public section (test observation):

```cpp
    int pendingProgram() const noexcept { return pendingPc.load(); }
```

- [ ] **Step 4: Implement enqueue + processBlock injection**

In `src/CustosProcessor.cpp`, replace the Task-7 stub `patchSendProgramChange` body:

```cpp
void CustosProcessor::patchSendProgramChange (int delta)
{
    pcProgram = ((pcProgram + delta) % 128 + 128) % 128;   // wrap 0..127
    pendingPc.store (pcProgram);
}
```

In `src/CustosProcessor.cpp`, `processBlock`, immediately after the `applyMidiRoute (midi, snap, routeScratch);` line, inject the queued PC so it reaches the inner unrouted:

```cpp
    const int pc = pendingPc.exchange (-1);
    if (pc >= 0)
        midi.addEvent (juce::MidiMessage::programChange (1, pc), 0);
```

- [ ] **Step 5: Run the tests — PASS**

Run: `scripts\build.cmd custos_tests Custos && scripts\test.cmd`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/CustosProcessor.h src/CustosProcessor.cpp tests/PatchStepTest.cpp
git commit -m "feat(custos): PC patch inject (wrapping program change via processBlock)"
```

---

### Task 10: /custos/patch/stepped feedback

**Files:**
- Modify: `src/OscContract.h` (`buildPatchStepped`)
- Modify: `src/CustosOscServer.h` (`gpMirrorsFeedback`)
- Modify: `src/CustosProcessor.h` / `src/CustosProcessor.cpp` (`emitPatchStepped` + calls)
- Test: `tests/OscContractTest.cpp`

**Interfaces:**
- Produces: `juce::OSCMessage buildPatchStepped (int n, const juce::String& controlType, const juce::String& detail);` → address `/custos/patch/stepped`, args `n, controlType, detail`. `gpMirrorsFeedback` returns true for `/custos/patch/stepped`. `patchStep` emits it: `PARAM` → detail `"+"`/`"-"`; `PC` → detail = program number; `PresetFallback` emits nothing (the preset cursor already emits `/custos/preset/browsing`).

- [ ] **Step 1: Write the failing builder + mirror tests**

Add to `tests/OscContractTest.cpp`:

```cpp
TEST_CASE ("buildPatchStepped carries n, controlType, detail")
{
    const auto m = custos::buildPatchStepped (2, "PC", "17");
    REQUIRE (m.getAddressPattern().toString() == "/custos/patch/stepped");
    REQUIRE (m[0].getInt32()  == 2);
    REQUIRE (m[1].getString() == "PC");
    REQUIRE (m[2].getString() == "17");
}

TEST_CASE ("gpMirrorsFeedback mirrors /custos/patch/stepped")
{
    REQUIRE (custos::gpMirrorsFeedback ("/custos/patch/stepped", {}) == true);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "buildPatchStepped carries n, controlType, detail"`
Expected: FAIL — `buildPatchStepped` undeclared.

- [ ] **Step 3: Add the builder + mirror rule**

In `src/OscContract.h`, inside `namespace custos`, near `buildBrowsing`:

```cpp
// Patch-step feedback (Custos -> KM/GP). controlType = the method that ran (PARAM|PC|PRESET);
// detail is best-effort ("+"/"-" for PARAM, program number for PC, "" otherwise).
inline juce::OSCMessage buildPatchStepped (int n, const juce::String& controlType, const juce::String& detail)
{
    return juce::OSCMessage ("/custos/patch/stepped", n, controlType, detail);
}
```

In `src/CustosOscServer.h`, in `gpMirrorsFeedback`, extend the browse/preset block:

```cpp
    if (addr == "/custos/browsing" || addr == "/custos/loaded" || addr == "/custos/here"
        || addr == "/custos/patch/stepped")
        return true;
```

- [ ] **Step 4: Emit it from patchStep**

In `src/CustosProcessor.h`, private section:

```cpp
    void emitPatchStepped (const juce::String& controlType, const juce::String& detail);
```

In `src/CustosProcessor.cpp`, add:

```cpp
void CustosProcessor::emitPatchStepped (const juce::String& controlType, const juce::String& detail)
{
    if (outboundSink) outboundSink (buildPatchStepped (identityN, controlType, detail));
}
```

Update `patchStep` (Task 7) to emit after dispatching PARAM/PC (leave PresetFallback silent):

```cpp
    switch (patchMethodFor (controlType))
    {
        case PatchMethod::Param:
            patchInjectParam (delta > 0 ? pUp : pDown);
            emitPatchStepped ("PARAM", delta > 0 ? "+" : "-");
            break;
        case PatchMethod::Pc:
            patchSendProgramChange (delta);
            emitPatchStepped ("PC", juce::String (pcProgram));
            break;
        case PatchMethod::PresetFallback:
            if (delta > 0) presetNext(); else presetPrev();
            break;
    }
```

Add `#include "OscContract.h"` to `src/CustosProcessor.cpp` if not already present.

- [ ] **Step 5: Run the suite — PASS**

Run: `scripts\build.cmd custos_tests Custos && scripts\test.cmd`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/OscContract.h src/CustosOscServer.h src/CustosProcessor.h src/CustosProcessor.cpp tests/OscContractTest.cpp
git commit -m "feat(custos): /custos/patch/stepped feedback (mirrored to GP)"
```

---

### Task 11: Contract version bump + doc

**Files:**
- Modify: `src/OscContract.h` (`kProtoVersion`)
- Modify: `docs/osc-contract.md`
- Test: `tests/OscContractTest.cpp`

**Interfaces:**
- Produces: `kProtoVersion == 3`; `docs/osc-contract.md` documents `/custos/instrument/load`, the `scope` arg on `/custos/instrument/next|prev`, `/custos/patch/next|prev`, `/custos/patch/stepped`, the widened `/custos/favorite`, the `instruments.json` store, and the trim-precedence note.

- [ ] **Step 1: Write/adjust the failing version test**

In `tests/OscContractTest.cpp`, find the existing protoVer assertion (`kProtoVersion == 2`) and change it to `== 3` (if none exists, add):

```cpp
TEST_CASE ("proto version is 3")
{
    REQUIRE (custos::kProtoVersion == 3);
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `scripts\build.cmd custos_tests && build\tests\custos_tests.exe "proto version is 3"`
Expected: FAIL — `kProtoVersion` is still 2.

- [ ] **Step 3: Bump the version**

In `src/OscContract.h`: `constexpr int kProtoVersion = 3;`

- [ ] **Step 4: Document the new/changed verbs**

In `docs/osc-contract.md`: change the header to `protoVer = 3`; in the §2 KM→Custos table add rows for `/custos/instrument/load <name>`, `/custos/patch/next`, `/custos/patch/prev`, the optional `scope:int` on `/custos/instrument/next|prev`, and the three new `/custos/favorite` args (`controlType, paramDown, paramUp`); in §3 Custos→KM add `/custos/patch/stepped N, controlType, detail` (mirrored to GP); add a note that the machine config is now `instruments.json` (migrated once from `favorites.json`); add a bullet under §4 that a delivered `/custos/volume` outranks the list `gainDb` (trim precedence).

- [ ] **Step 5: Run the suite — PASS**

Run: `scripts\build.cmd custos_tests Custos && scripts\test.cmd`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/OscContract.h docs/osc-contract.md tests/OscContractTest.cpp
git commit -m "docs(custos): OSC contract v3 — instrument/load, patch axis, scope, widened favorite"
```

---

## Self-Review

**Spec coverage (spec §-by-§):**
- §4 data model → Tasks 1 (fields), 2 (instruments.json + migration). ✓
- §4 KM push (widen `/custos/favorite`) → Task 3. ✓
- §5 `instrument/next|prev` scope → Task 4; `instrument/load <name>` → Task 5; `patch/next|prev` → Tasks 6–7; `preset/*` already exists (no task needed). ✓
- §5 `/custos/volume` for override → no code (precedence by ordering); documented in Task 11. ✓
- §6 controlType dispatch (PARAM/PC/PRESET fallback) → Tasks 6 (resolve), 7 (fallback), 8 (PARAM), 9 (PC). ✓
- §5 `/custos/patch/stepped` feedback → Task 10. ✓
- §6a trim precedence → Task 11 doc note (no Custos code). ✓
- §11 open items resolved: push shape = widen `/custos/favorite` (Task 3); migration = Task 2; patch/stepped name = best-effort detail (Task 10). ✓
- **Out of scope (correctly absent):** coarse/bank verbs, second PARAM pair, joystick binding, GP-Script changes (Part B).

**Placeholder scan:** No TBD/TODO. Task 7 stubs `patchInjectParam`/`patchSendProgramChange` as *named, compiling* no-ops that Tasks 8/9 fill — each is shown in full, not deferred prose.

**Type consistency:** `browseInstrument(int, int)`, `favouriteInScope(int,int)`, `patchMethodFor(juce::String)→PatchMethod`, `loadByName(juce::String)→bool`, `patchNext/patchPrev/patchStep`, `patchInjectParam(int)`, `patchSendProgramChange(int)`, `pendingProgram()`, `emitPatchStepped(String,String)`, `buildPatchStepped(int,String,String)` — names/signatures identical across the tasks that declare vs. call them.

---

## Part B (separate plan, not here)

GP-Script side — write after Part A lands and is E2E-verified via the Python OSC harness. Scope: add the single-step macro catalogue (`Instrument/Patch/Preset Prev/Next` + `Preset Load`); delete the `VstDatabase.txt` pipeline (`LoadVstDatabase`, `DB_*`, `GetPathForVst`, `RefreshVstTrims`), the native engine (`TriggerVstPresetChange`, `CyclePluginPreset`, `FullDeflectVoice`), the trim-authoring path (`BTN_AutoGainCalibration`, `PushVstTrimsToKM`), and `_LIBR`/`_INST` (`LibCyclePrefix`, `LoadRigConfig`, `BuildPresetSublist`, `MovePreset`, `RigConfig.txt`); forward `Song.ini` names via `/custos/instrument/load` and per-song trim overrides via `/custos/volume`. Different repo + live-E2E workflow (no Catch2), so it gets its own plan.
