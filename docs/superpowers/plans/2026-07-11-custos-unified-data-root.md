# Custos Unified Data Root — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Relocate Custos's instrument/favourites database (`instruments.json`) from hidden `%APPDATA%\Custos\` into the configurable, OSC-settable `presetRoot`, so the operator's curated work lives in one backup-friendly folder, with a legacy self-heal fallback and a read-only query verb.

**Architecture:** Extend the existing machine-global `presetRoot` (already persisted + OSC-settable for `.cuspreset`) to also host `instruments.json`. Pure path/resolver/self-heal helpers land in `FavoritesStore`; the processor and OSC server are re-wired to use them; a new `/custos/preset/queryroot` verb reports the current root without mutation. Migration of existing files and KM's read-path switch are out-of-repo handoffs.

**Tech Stack:** C++17, JUCE (`juce_core`, `juce_osc`), Catch2 tests, CMake + Ninja + MSVC.

## Global Constraints

- **Repo boundary:** Custos repo only (`C:\dev\custos`). No edits to Kapellmeister / GP-Script / any sibling repo.
- **protoVer stays 3** — changes are additive (one new verb + a relocated file); no wire-format change to existing messages.
- **Backward compatible:** a Custos with no root configured (empty `presetRootPath`) must still read/write the legacy `%APPDATA%\Custos\instruments.json` and read `favorites.json`.
- **One logical dataset:** `std::vector<custos::Favorite>` serialized as JSON. `instruments.json` is canonical; `favorites.json` is legacy read-only fallback.
- **Build env:** MSVC not on PATH by default — load `vcvars64.bat` (see `reference-custos-build-env`), then `cmake --build C:/dev/custos/build --target custos_tests`. Ninja auto-reconfigures. Run tests: `C:/dev/custos/build/tests/custos_tests.exe "<Catch2 test name>"`.
- **Tests are registered already:** new cases go into existing `tests/FavoritesStoreTest.cpp`, `tests/PresetProcessorTest.cpp`, `tests/OscCommandTest.cpp` — no `CMakeLists.txt` change.
- **English** in all code, comments, commit messages, and docs.

---

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `src/FavoritesStore.h/.cpp` | Path selection, layered resolution, self-heal load, root-aware write | Add `instrumentsFileIn`, `instrumentsTargetFor`, `writeInstruments`, `InstrumentsSource` + `resolveInstrumentsSource`, `loadInstrumentsWithSelfHeal` |
| `src/CustosProcessor.h/.cpp` | Owns favourites + presetRoot; emits feedback | Add `emitPresetRoot`; extend `setPresetRoot` with runtime re-point; (helpers reused from store) |
| `src/CustosOscServer.h` | `Command` enum | Add `PresetQueryRoot` |
| `src/CustosOscServer.cpp` | Parse + dispatch; favourites load/write sites | Parse+dispatch `queryroot`; load via `loadInstrumentsWithSelfHeal`; write via `writeInstruments` |
| `docs/osc-contract.md` | OSC surface | Document `queryroot`; note `setroot` relocates the instrument DB |
| `tests/FavoritesStoreTest.cpp` | Store unit tests | Add resolver / target / self-heal cases |
| `tests/PresetProcessorTest.cpp` | Processor tests | Add `emitPresetRoot` + `setPresetRoot` re-point cases |
| `tests/OscCommandTest.cpp` | Parse tests | Add `queryroot` parse case |

---

## Task 1: Root-aware path selection + write

**Files:**
- Modify: `src/FavoritesStore.h` (add declarations after line 20)
- Modify: `src/FavoritesStore.cpp` (add definitions after `instrumentsConfigFile`, ~line 69)
- Test: `tests/FavoritesStoreTest.cpp` (append)

**Interfaces:**
- Consumes: existing `favoritesConfigFile()`, `instrumentsConfigFile()`, `writeFavorites(File, vector<Favorite>)`.
- Produces:
  - `juce::File instrumentsFileIn (const juce::File& root)` → `<root>/instruments.json`
  - `juce::File instrumentsTargetFor (const juce::File& root)` → `instrumentsFileIn(root)` when root path non-empty, else `instrumentsConfigFile()` (legacy)
  - `bool writeInstruments (const juce::File& root, const std::vector<Favorite>& favs)` → writes to `instrumentsTargetFor(root)`; returns `writeFavorites` success

- [ ] **Step 1: Write the failing tests**

Append to `tests/FavoritesStoreTest.cpp`:

```cpp
TEST_CASE ("instrumentsFileIn / instrumentsTargetFor pick the right file")
{
    juce::File root ("C:/backup/Custos");
    REQUIRE (instrumentsFileIn (root).getFullPathName()
             == juce::File ("C:/backup/Custos/instruments.json").getFullPathName());

    // Non-empty root -> under the root.
    REQUIRE (instrumentsTargetFor (root) == instrumentsFileIn (root));

    // Empty/invalid root -> legacy %APPDATA% canonical file.
    REQUIRE (instrumentsTargetFor (juce::File()) == instrumentsConfigFile());
}

TEST_CASE ("writeInstruments lands at <root>/instruments.json")
{
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    REQUIRE (writeInstruments (root, { { "Zebra2", "C:/x/Zebra2.vst3", 1, 0.0f, "u-he" } }));
    auto target = root.getChildFile ("instruments.json");
    REQUIRE (target.existsAsFile());
    const auto back = readFavorites (target);
    REQUIRE (back.size() == 1);
    REQUIRE (back[0].name == "Zebra2");
    root.deleteRecursively();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `C:/dev/custos/build/tests/custos_tests.exe "instrumentsFileIn / instrumentsTargetFor pick the right file"`
Expected: compile error / FAIL — `instrumentsFileIn` undefined.

- [ ] **Step 3: Add declarations to `FavoritesStore.h`**

Insert after line 20 (after the `readInstruments` declaration), before the closing `}`:

```cpp
// Root-aware instrument-DB location (unified data root).
juce::File instrumentsFileIn   (const juce::File& root);           // <root>/instruments.json
juce::File instrumentsTargetFor (const juce::File& root);          // <root>/... or legacy %APPDATA% if root empty
bool       writeInstruments    (const juce::File& root, const std::vector<Favorite>& favs);
```

- [ ] **Step 4: Add definitions to `FavoritesStore.cpp`**

Insert after `instrumentsConfigFile()` (after line 69), before `readInstruments`:

```cpp
juce::File instrumentsFileIn (const juce::File& root)
{
    return root.getChildFile ("instruments.json");
}

juce::File instrumentsTargetFor (const juce::File& root)
{
    return root.getFullPathName().isNotEmpty() ? instrumentsFileIn (root)
                                               : instrumentsConfigFile();
}

bool writeInstruments (const juce::File& root, const std::vector<Favorite>& favs)
{
    auto target = instrumentsTargetFor (root);
    writeFavorites (target, favs);            // creates parent dir, replaces file
    return target.existsAsFile();
}
```

- [ ] **Step 5: Build + run tests to verify they pass**

Run: `cmake --build C:/dev/custos/build --target custos_tests && C:/dev/custos/build/tests/custos_tests.exe "instrumentsFileIn / instrumentsTargetFor pick the right file" "writeInstruments lands at <root>/instruments.json"`
Expected: PASS (2 test cases).

- [ ] **Step 6: Commit**

```bash
git add src/FavoritesStore.h src/FavoritesStore.cpp tests/FavoritesStoreTest.cpp
git commit -m "feat(custos): root-aware instrument-DB path + writeInstruments"
```

---

## Task 2: Layered source resolver

**Files:**
- Modify: `src/FavoritesStore.h` (add after Task 1 declarations)
- Modify: `src/FavoritesStore.cpp` (add after Task 1 definitions)
- Test: `tests/FavoritesStoreTest.cpp` (append)

**Interfaces:**
- Consumes: `instrumentsFileIn` (Task 1).
- Produces:
  - `struct InstrumentsSource { juce::File file; bool fromLegacy = false; bool found = false; };`
  - `InstrumentsSource resolveInstrumentsSource (const juce::File& root, const juce::File& legacyCanonical, const juce::File& legacyOld)` — returns the first EXISTING of `[<root>/instruments.json, legacyCanonical, legacyOld]`. `fromLegacy` is true when it resolved to tier 2 or 3. `found` is false (and `file` = the tier-1 path) when none exist. A non-empty `root` is required for tier 1 to be considered.

- [ ] **Step 1: Write the failing test**

Append to `tests/FavoritesStoreTest.cpp`:

```cpp
TEST_CASE ("resolveInstrumentsSource honours tier order root > canonical > old")
{
    auto dir = juce::File::createTempFile (""); dir.deleteFile(); dir.createDirectory();
    auto root  = dir.getChildFile ("root");  root.createDirectory();
    auto rootF = root.getChildFile ("instruments.json");
    auto canon = dir.getChildFile ("instruments.json");   // legacy canonical
    auto old   = dir.getChildFile ("favorites.json");     // legacy old

    // None present -> not found, file points at tier 1 target.
    auto r0 = resolveInstrumentsSource (root, canon, old);
    REQUIRE_FALSE (r0.found);
    REQUIRE (r0.file == rootF);

    // Only old present -> tier 3, fromLegacy.
    writeFavorites (old, { { "O", "C:/o.vst3", 1, 0.0f } });
    auto r3 = resolveInstrumentsSource (root, canon, old);
    REQUIRE (r3.found); REQUIRE (r3.fromLegacy); REQUIRE (r3.file == old);

    // Canonical present -> tier 2 wins over old.
    writeFavorites (canon, { { "C", "C:/c.vst3", 1, 0.0f } });
    auto r2 = resolveInstrumentsSource (root, canon, old);
    REQUIRE (r2.found); REQUIRE (r2.fromLegacy); REQUIRE (r2.file == canon);

    // Root present -> tier 1 wins, not legacy.
    writeFavorites (rootF, { { "R", "C:/r.vst3", 1, 0.0f } });
    auto r1 = resolveInstrumentsSource (root, canon, old);
    REQUIRE (r1.found); REQUIRE_FALSE (r1.fromLegacy); REQUIRE (r1.file == rootF);

    // Empty root -> tier 1 skipped, resolves to canonical.
    auto re = resolveInstrumentsSource (juce::File(), canon, old);
    REQUIRE (re.found); REQUIRE (re.fromLegacy); REQUIRE (re.file == canon);

    dir.deleteRecursively();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `C:/dev/custos/build/tests/custos_tests.exe "resolveInstrumentsSource honours tier order root > canonical > old"`
Expected: compile error — `resolveInstrumentsSource` / `InstrumentsSource` undefined.

- [ ] **Step 3: Add declaration to `FavoritesStore.h`**

Insert after the Task 1 declarations:

```cpp
struct InstrumentsSource { juce::File file; bool fromLegacy = false; bool found = false; };

// First existing of [<root>/instruments.json, legacyCanonical, legacyOld]. Non-empty root required
// for tier 1. When none exist, found=false and file = the tier-1 target (<root>/instruments.json,
// or legacyCanonical if root empty).
InstrumentsSource resolveInstrumentsSource (const juce::File& root,
                                            const juce::File& legacyCanonical,
                                            const juce::File& legacyOld);
```

- [ ] **Step 4: Add definition to `FavoritesStore.cpp`**

Insert after the Task 1 definitions:

```cpp
InstrumentsSource resolveInstrumentsSource (const juce::File& root,
                                            const juce::File& legacyCanonical,
                                            const juce::File& legacyOld)
{
    const bool rootValid = root.getFullPathName().isNotEmpty();
    if (rootValid)
    {
        auto tier1 = instrumentsFileIn (root);
        if (tier1.existsAsFile()) return { tier1, false, true };
    }
    if (legacyCanonical.existsAsFile()) return { legacyCanonical, true, true };
    if (legacyOld.existsAsFile())       return { legacyOld, true, true };
    return { rootValid ? instrumentsFileIn (root) : legacyCanonical, false, false };
}
```

- [ ] **Step 5: Build + run test to verify it passes**

Run: `cmake --build C:/dev/custos/build --target custos_tests && C:/dev/custos/build/tests/custos_tests.exe "resolveInstrumentsSource honours tier order root > canonical > old"`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/FavoritesStore.h src/FavoritesStore.cpp tests/FavoritesStoreTest.cpp
git commit -m "feat(custos): layered instrument-DB source resolver"
```

---

## Task 3: Self-heal load

**Files:**
- Modify: `src/FavoritesStore.h` (add after Task 2 declarations)
- Modify: `src/FavoritesStore.cpp` (add after Task 2 definitions)
- Test: `tests/FavoritesStoreTest.cpp` (append)

**Interfaces:**
- Consumes: `resolveInstrumentsSource` (Task 2), `readFavorites`, `writeInstruments` (Task 1).
- Produces:
  - `std::vector<Favorite> loadInstrumentsWithSelfHeal (const juce::File& root, const juce::File& legacyCanonical, const juce::File& legacyOld)` — resolves the source, reads it, and if the data came from a legacy tier AND root is non-empty, writes it once to `<root>/instruments.json` (self-heal seed). Returns the favourites (empty if none / if the resolved file is absent).

- [ ] **Step 1: Write the failing test**

Append to `tests/FavoritesStoreTest.cpp`:

```cpp
TEST_CASE ("loadInstrumentsWithSelfHeal seeds the root from a legacy file, once")
{
    auto dir = juce::File::createTempFile (""); dir.deleteFile(); dir.createDirectory();
    auto root  = dir.getChildFile ("root");  root.createDirectory();
    auto rootF = root.getChildFile ("instruments.json");
    auto canon = dir.getChildFile ("instruments.json");
    auto old   = dir.getChildFile ("favorites.json");

    writeFavorites (canon, { { "Legacy", "C:/l.vst3", 1, 0.0f } });
    REQUIRE_FALSE (rootF.existsAsFile());

    // Legacy read -> data returned AND seeded into the root.
    auto favs = loadInstrumentsWithSelfHeal (root, canon, old);
    REQUIRE (favs.size() == 1);
    REQUIRE (favs[0].name == "Legacy");
    REQUIRE (rootF.existsAsFile());                 // self-heal wrote it

    // Second call reads tier 1 and does not touch legacy; still correct.
    auto again = loadInstrumentsWithSelfHeal (root, canon, old);
    REQUIRE (again.size() == 1);
    REQUIRE (again[0].name == "Legacy");

    dir.deleteRecursively();
}

TEST_CASE ("loadInstrumentsWithSelfHeal returns empty and seeds nothing when no source exists")
{
    auto dir = juce::File::createTempFile (""); dir.deleteFile(); dir.createDirectory();
    auto root  = dir.getChildFile ("root");  root.createDirectory();
    auto favs = loadInstrumentsWithSelfHeal (root, dir.getChildFile ("instruments.json"),
                                                   dir.getChildFile ("favorites.json"));
    REQUIRE (favs.empty());
    REQUIRE_FALSE (root.getChildFile ("instruments.json").existsAsFile());   // no seed from nothing
    dir.deleteRecursively();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `C:/dev/custos/build/tests/custos_tests.exe "loadInstrumentsWithSelfHeal seeds the root from a legacy file, once"`
Expected: compile error — `loadInstrumentsWithSelfHeal` undefined.

- [ ] **Step 3: Add declaration to `FavoritesStore.h`**

Insert after the Task 2 declaration:

```cpp
// Load favourites for a data root: resolve (root > legacy canonical > legacy old), read, and if the
// data came from a legacy tier while root is non-empty, seed <root>/instruments.json once. Idempotent.
std::vector<Favorite> loadInstrumentsWithSelfHeal (const juce::File& root,
                                                   const juce::File& legacyCanonical,
                                                   const juce::File& legacyOld);
```

- [ ] **Step 4: Add definition to `FavoritesStore.cpp`**

Insert after the Task 2 definition:

```cpp
std::vector<Favorite> loadInstrumentsWithSelfHeal (const juce::File& root,
                                                   const juce::File& legacyCanonical,
                                                   const juce::File& legacyOld)
{
    const auto src = resolveInstrumentsSource (root, legacyCanonical, legacyOld);
    if (! src.found) return {};
    auto favs = readFavorites (src.file);
    if (src.fromLegacy && root.getFullPathName().isNotEmpty() && ! favs.empty())
        writeInstruments (root, favs);      // self-heal seed into the backup-friendly root
    return favs;
}
```

- [ ] **Step 5: Build + run tests to verify they pass**

Run: `cmake --build C:/dev/custos/build --target custos_tests && C:/dev/custos/build/tests/custos_tests.exe "loadInstrumentsWithSelfHeal seeds the root from a legacy file, once" "loadInstrumentsWithSelfHeal returns empty and seeds nothing when no source exists"`
Expected: PASS (2 cases).

- [ ] **Step 6: Commit**

```bash
git add src/FavoritesStore.h src/FavoritesStore.cpp tests/FavoritesStoreTest.cpp
git commit -m "feat(custos): self-heal load seeds instrument DB into the data root"
```

---

## Task 4: Processor wiring + runtime re-point + emitPresetRoot

**Files:**
- Modify: `src/CustosProcessor.h` (declare `emitPresetRoot` near line 116)
- Modify: `src/CustosProcessor.cpp` (`setPresetRoot` at 507-518; add `emitPresetRoot`)
- Modify: `src/CustosOscServer.cpp` (load site line 197; write site line 309)
- Test: `tests/PresetProcessorTest.cpp` (append)

**Interfaces:**
- Consumes: `loadInstrumentsWithSelfHeal`, `writeInstruments`, `instrumentsFileIn`, `readFavorites`, `favoritesConfigFile`, `instrumentsConfigFile` (Tasks 1-3); existing `favorites` member, `getFavorites()`, `setFavorites()`, `outboundSink`, `identityN`, `presetRootPath`.
- Produces:
  - `void CustosProcessor::emitPresetRoot()` — emits `/custos/preset/root <identityN> <presetRootPath>` via `outboundSink` (no-op if sink null).
  - `setPresetRoot` gains a runtime re-point: adopt `<newRoot>/instruments.json` if present, else carry current favourites into the new root.

- [ ] **Step 1: Write the failing tests**

Append to `tests/PresetProcessorTest.cpp`:

```cpp
TEST_CASE ("emitPresetRoot reports identity + current root")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc;
    proc.setIdentity (6);
    proc.setPresetRoot (root.getFullPathName());

    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };

    proc.emitPresetRoot();
    REQUIRE (msgs.size() == 1);
    REQUIRE (msgs[0].getAddressPattern().toString() == "/custos/preset/root");
    REQUIRE ((int) msgs[0][0].getInt32() == 6);
    REQUIRE (msgs[0][1].getString() == root.getFullPathName());
    root.deleteRecursively();
}

TEST_CASE ("setPresetRoot adopts an existing instruments.json at the new root")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    custos::writeFavorites (root.getChildFile ("instruments.json"),
                            { { "Adopted", "C:/a.vst3", 1, 0.0f } });
    CustosProcessor proc;
    proc.setIdentity (1);
    proc.setPresetRoot (root.getFullPathName());     // should adopt the file's favourites
    REQUIRE (proc.getFavorites().size() == 1);
    REQUIRE (proc.getFavorites()[0].name == "Adopted");
    root.deleteRecursively();
}

TEST_CASE ("setPresetRoot carries current favourites into an empty new root")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc;
    proc.setIdentity (1);
    proc.setFavorites ({ { "Carried", "C:/c.vst3", 1, 0.0f } });
    proc.setPresetRoot (root.getFullPathName());     // no file at root -> carry memory there
    auto seeded = custos::readFavorites (root.getChildFile ("instruments.json"));
    REQUIRE (seeded.size() == 1);
    REQUIRE (seeded[0].name == "Carried");
    root.deleteRecursively();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `C:/dev/custos/build/tests/custos_tests.exe "emitPresetRoot reports identity + current root"`
Expected: compile error — `emitPresetRoot` undefined; adopt/carry assertions fail.

- [ ] **Step 3: Declare `emitPresetRoot` in `CustosProcessor.h`**

Insert after line 116 (`void setPresetRoot (...)`):

```cpp
    void         emitPresetRoot();                             // report /custos/preset/root (query + on set)
```

- [ ] **Step 4: Add `emitPresetRoot` and re-point in `CustosProcessor.cpp`**

Replace the current `setPresetRoot` (lines 507-518) with:

```cpp
void CustosProcessor::emitPresetRoot()
{
    if (outboundSink)
    {
        juce::OSCMessage m ("/custos/preset/root");
        m.addInt32 (identityN);
        m.addString (presetRootPath);
        outboundSink (m);
    }
}

void CustosProcessor::setPresetRoot (const juce::String& path)
{
    presetRootPath = path;
    writePresetRoot (presetRootConfigFile(), path);
    emitPresetRoot();

    // The instrument/favourites DB follows the root: adopt what is already at the new location,
    // otherwise carry the current in-memory favourites there so the operator never loses them.
    juce::File newRoot (path);
    if (newRoot.getFullPathName().isNotEmpty())
    {
        auto target = instrumentsFileIn (newRoot);
        if (target.existsAsFile())
            setFavorites (readFavorites (target));
        else if (! favorites.empty())
            writeInstruments (newRoot, favorites);
    }
}
```

Ensure `#include "FavoritesStore.h"` is present in `CustosProcessor.cpp` (add it near the top includes if absent).

- [ ] **Step 5: Re-wire the OSC server load + write sites**

In `src/CustosOscServer.cpp`, replace line 197:

```cpp
    proc.setFavorites (loadInstrumentsWithSelfHeal (juce::File (proc.presetRoot()),
                                                    instrumentsConfigFile(), favoritesConfigFile()));
```

And replace line 309 (`FavEnd` case):

```cpp
            writeInstruments (juce::File (proc.presetRoot()), proc.getFavorites());   // unified data root
```

- [ ] **Step 6: Build + run tests to verify they pass (and no regressions)**

Run: `cmake --build C:/dev/custos/build --target custos_tests && C:/dev/custos/build/tests/custos_tests.exe "emitPresetRoot reports identity + current root" "setPresetRoot adopts an existing instruments.json at the new root" "setPresetRoot carries current favourites into an empty new root"`
Expected: PASS (3 cases).

Then run the full preset processor suite to confirm no regression:
Run: `C:/dev/custos/build/tests/custos_tests.exe "[.]" ; C:/dev/custos/build/tests/custos_tests.exe`
Expected: full suite green (existing preset tests set the root on an empty temp dir; with empty favourites the re-point carries nothing and emits nothing extra, so their `msgs` assertions still hold).

- [ ] **Step 7: Commit**

```bash
git add src/CustosProcessor.h src/CustosProcessor.cpp src/CustosOscServer.cpp tests/PresetProcessorTest.cpp
git commit -m "feat(custos): favourites DB follows presetRoot; emitPresetRoot; self-heal load wiring"
```

---

## Task 5: `/custos/preset/queryroot` verb + docs

**Files:**
- Modify: `src/CustosOscServer.h` (`Command::Kind` enum, line 16)
- Modify: `src/CustosOscServer.cpp` (parse after line 155; dispatch after line 355)
- Modify: `docs/osc-contract.md` (add query row; note setroot relocation)
- Test: `tests/OscCommandTest.cpp` (append)

**Interfaces:**
- Consumes: `parseCommand`, `Command`, `proc.emitPresetRoot()` (Task 4).
- Produces: `Command::PresetQueryRoot`; address `/custos/preset/queryroot` → dispatches `proc.emitPresetRoot()`.

- [ ] **Step 1: Write the failing test**

Append to `tests/OscCommandTest.cpp`:

```cpp
TEST_CASE ("parseCommand maps /custos/preset/queryroot")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/preset/queryroot")).kind
             == Command::PresetQueryRoot);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `C:/dev/custos/build/tests/custos_tests.exe "parseCommand maps /custos/preset/queryroot"`
Expected: compile error — `Command::PresetQueryRoot` undefined.

- [ ] **Step 3: Add the enum value**

In `src/CustosOscServer.h` line 16, add `PresetQueryRoot` to the `Kind` enum (next to `MainLRQuery`):

```cpp
                PresetSet, PresetRename, PresetDelete, MainLR, MainLRQuery, PresetQueryRoot,
```

- [ ] **Step 4: Add the parser branch**

In `src/CustosOscServer.cpp`, immediately after the `/custos/preset/setroot` branch (after line 155):

```cpp
    if (addr == "/custos/preset/queryroot") { Command c; c.kind = Command::PresetQueryRoot; return c; }
```

- [ ] **Step 5: Add the dispatch branch**

In `src/CustosOscServer.cpp`, after the `PresetSetRoot` case (after line 356):

```cpp
        case Command::PresetQueryRoot:
            proc.emitPresetRoot();
            break;
```

- [ ] **Step 6: Build + run test to verify it passes**

Run: `cmake --build C:/dev/custos/build --target custos_tests && C:/dev/custos/build/tests/custos_tests.exe "parseCommand maps /custos/preset/queryroot"`
Expected: PASS.

- [ ] **Step 7: Update `docs/osc-contract.md`**

Add a row in the inbound-verbs table (near `/custos/preset/setroot`, line 66):

```
| `/custos/preset/queryroot` | (none) | report the current data root without changing it; echoes `/custos/preset/root` |
```

Update the `/custos/preset/setroot` row (line 66) to note it now also relocates the instrument DB:

```
| `/custos/preset/setroot` | `path:string` | set the machine-global data root (presets **and** instrument DB), persisted; echoes `/custos/preset/root`; the favourites DB is adopted from / carried to the new root |
```

And in the storage note (line 161) mention that `instruments.json` now lives under the data root with a legacy `%APPDATA%` fallback.

- [ ] **Step 8: Commit**

```bash
git add src/CustosOscServer.h src/CustosOscServer.cpp docs/osc-contract.md tests/OscCommandTest.cpp
git commit -m "feat(custos): add /custos/preset/queryroot; document unified data root"
```

---

## Task 6: Full-suite verification + live E2E

**Files:** none (verification only)

- [ ] **Step 1: Run the complete test suite**

Run: `cmake --build C:/dev/custos/build --target custos_tests && C:/dev/custos/build/tests/custos_tests.exe`
Expected: all tests pass (no failures, no regressions).

- [ ] **Step 2: Live E2E (standing OSC-probe permission)**

With a live Custos instance N running (port `9100+N`, hub `:8000`), using `custos/tools`:

1. `queryroot` → confirm `/custos/preset/root <N> <path>` arrives on `:8000` (no mutation).
2. Pick a temp folder `T`; send `/custos/preset/setroot T`; confirm `/custos/preset/root <N> T` echo.
3. Confirm `T/instruments.json` now exists and matches the live favourites (carried).
4. Restore: send `/custos/preset/setroot <original path>`; confirm echo + `instruments.json` present at the original.

Report side-effects (this mutates the live root; restore in step 4). If a GP restart would be needed, greenlight per standing permission but report it.

- [ ] **Step 3: Commit any doc/notes fixes surfaced by E2E (if needed)**

```bash
git add -A && git commit -m "docs(custos): E2E notes for unified data root"
```

---

## Handoff artifacts (deliver, do NOT implement here)

After the branch is pushed + PR opened + `deploy:test` exe delivered, produce a **Kapellmeister handoff prompt** (separate KM session) covering:

1. **KM read-path switch:** read `instruments.json` from `<presetRoot>/instruments.json`, discovering the root via `/custos/preset/queryroot` → `/custos/preset/root`, not from hardcoded `%APPDATA%`.
2. **Migration:** query the root; if `<root>/instruments.json` is absent and a `%APPDATA%\Custos\instruments.json` copy exists, either copy it up or simply send `setroot <root>` and rely on Custos self-heal seed (KM decides). Both paths are idempotent (last-writer-wins on identical content).
3. **Ordering:** avoid destructive double-writes — do not have KM overwrite the root file with stale `%APPDATA%` content after Custos has already seeded fresher data; prefer query-then-decide.

---

## Self-Review

**Spec coverage:**
- Relocate `instruments.json` under `presetRoot` → Tasks 1, 4. ✅
- Layered read fallback → Task 2. ✅
- Self-heal seed → Task 3. ✅
- Runtime re-point on `setroot` → Task 4. ✅
- `queryroot` verb → Task 5. ✅
- Backward compat (empty root → legacy) → Tasks 1 (`instrumentsTargetFor`), 2 (`resolveInstrumentsSource` empty-root branch), tested. ✅
- Corrupt-file handling (present-but-corrupt authoritative file returns empty, does NOT fall through) → `resolveInstrumentsSource` matches on `existsAsFile`, `readFavorites` yields empty for corrupt; the resolver stops at the first *existing* tier. ✅
- Docs updated → Task 5 Step 7. ✅
- Migration + KM read-path = handoff → Handoff section. ✅

**Placeholder scan:** No TBD/TODO; every code step shows full code. ✅

**Type consistency:** `instrumentsFileIn`, `instrumentsTargetFor`, `writeInstruments`, `InstrumentsSource`, `resolveInstrumentsSource`, `loadInstrumentsWithSelfHeal`, `emitPresetRoot`, `Command::PresetQueryRoot` — used identically across tasks. `writeFavorites(File, vector<Favorite>)` and `readFavorites(File)` match existing signatures. ✅
