# Custos Preset Store Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give Custos a central, per-synth preset library (`.cuspreset` files) so a hosted inner synth's sound can be saved and recalled independently of GP User Presets and build variants.

**Architecture:** A pure codec (`PresetCodec`) and a pure file-store (`PresetStore`) provide the on-disk layer; `CustosProcessor` ties them to the currently loaded inner synth and emits OSC feedback; `CustosOscServer` maps new `/custos/preset/*` verbs to processor calls; `CustosEditor` gains a minimal save/load control. Presets live under a KM-pushed root, in one folder per synth keyed by a stable synth identifier, ordered alphabetically.

**Tech Stack:** C++17, JUCE 8.0.14, Catch2 v3.15.

## Global Constraints

- **Language/tooling:** C++17, JUCE 8.0.14, Catch2 v3. Code, comments and any GitHub content in English.
- **CMake wiring:** new non-GUI sources go into the `custos_core` library (`CMakeLists.txt` lines 18-28); new test files go into the `custos_tests` `add_executable` list (`tests/CMakeLists.txt`).
- **`.cuspreset` format (exact):** magic `"CUSP"` + 1 version byte (=1) + `int32 classIdLen`+UTF-8 + `int32 synthNameLen`+UTF-8 + `int32 presetNameLen`+UTF-8 + `int32 innerLen`+inner bytes. No timestamps, no N.
- **Store layout:** `<root>/<hex(classId)>/<sanitized name>.cuspreset`. Order = alphabetical, case-insensitive. `next/prev` wrap at the ends.
- **OSC:** commands arrive at `9100+N`; every feedback goes to the KM hub `:8000` with the identity **N as the first argument** (via `outboundSink`, no-op when null in tests). Recall preview/debounce ~400 ms, mirroring the existing favourite browse.
- **Safety:** never write an empty preset; exact-name save overwrites; missing/corrupt file → emit `/custos/preset/error`, never crash, keep the current sound.
- **Build/run (per the build-env note):** in a shell with `vcvars64.bat` loaded, using the VS-bundled cmake: `cmake --build C:/dev/custos/build --target custos_tests` then `C:/dev/custos/build/tests/custos_tests.exe "<test filter>"` (Ninja auto-reconfigures on CMake edits).
- **Commits:** end each commit message with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.

## File Structure

- `src/PresetCodec.h` / `.cpp` — `.cuspreset` serialize/parse. One responsibility: byte format. (→ `custos_core`)
- `src/PresetStore.h` / `.cpp` — file IO over a root dir: folder resolution, sanitize, save/list/load/delete/rename, root-path persistence. (→ `custos_core`)
- `src/CustosProcessor.h` / `.cpp` — new accessors (`captureInnerState`, `restoreInnerState`, `innerSynthKey`) and preset orchestration (save/list/load/next/prev/set/rename/delete + feedback). (compiled into `custos_tests` and each plugin variant)
- `src/CustosOscServer.h` / `.cpp` — `Command` additions, `parseCommand` mapping, dispatch wiring, GP mirror policy. (→ `custos_core`)
- `src/CustosEditor.h` / `.cpp` — save name field + save button + preset picker. (→ `custos_core`)
- `tests/PresetCodecTest.cpp`, `tests/PresetStoreTest.cpp`, `tests/PresetProcessorTest.cpp` — new. Verb parsing extends `tests/OscCommandTest.cpp`; mirror policy extends `tests/OscContractTest.cpp`.

---

### Task 1: PresetCodec — `.cuspreset` byte format

**Files:**
- Create: `src/PresetCodec.h`, `src/PresetCodec.cpp`, `tests/PresetCodecTest.cpp`
- Modify: `CMakeLists.txt` (add `src/PresetCodec.cpp` to `custos_core`), `tests/CMakeLists.txt` (add `PresetCodecTest.cpp`)

**Interfaces:**
- Produces: `struct PresetData { juce::String classId, synthName, presetName; juce::MemoryBlock innerState; };`, `juce::MemoryBlock serializePreset (const PresetData&);`, `bool parsePreset (const void* data, int size, PresetData& out);`

- [ ] **Step 1: Write the failing test** — create `tests/PresetCodecTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "PresetCodec.h"

using namespace custos;

TEST_CASE ("preset round-trips through serialize/parse")
{
    PresetData in;
    in.classId    = "VST3-Omnisphere-abc123";
    in.synthName  = "Omnisphere";
    in.presetName = "Warm Pad";
    in.innerState.append ("inner-bytes", 11);

    const auto blob = serializePreset (in);
    PresetData out;
    REQUIRE (parsePreset (blob.getData(), (int) blob.getSize(), out));
    REQUIRE (out.classId == "VST3-Omnisphere-abc123");
    REQUIRE (out.synthName == "Omnisphere");
    REQUIRE (out.presetName == "Warm Pad");
    REQUIRE (out.innerState.getSize() == 11);
    REQUIRE (out.innerState.matches ("inner-bytes", 11));
}

TEST_CASE ("parsePreset rejects wrong magic and truncation")
{
    PresetData out;
    REQUIRE_FALSE (parsePreset ("XXXX\1", 5, out));
    const char tiny[] = { 'C','U','S','P' };
    REQUIRE_FALSE (parsePreset (tiny, 4, out));   // no version byte
}
```

- [ ] **Step 2: Add both files to CMake, then run the test to verify it fails to build/link**

Add `src/PresetCodec.cpp` to the `custos_core` source list in `CMakeLists.txt` and `PresetCodecTest.cpp` to the `custos_tests` list in `tests/CMakeLists.txt`.
Run: `cmake --build C:/dev/custos/build --target custos_tests`
Expected: FAIL — `PresetCodec.h` not found / unresolved `serializePreset`.

- [ ] **Step 3: Write the header** — `src/PresetCodec.h`:

```cpp
#pragma once
#include <juce_core/juce_core.h>

namespace custos
{
struct PresetData
{
    juce::String classId;      // stable per-synth key (also the folder scope)
    juce::String synthName;    // human-readable, for display
    juce::String presetName;   // user-given
    juce::MemoryBlock innerState;
};

// Format: "CUSP" + version byte (1) + int32 classIdLen + UTF-8 classId
//         + int32 synthNameLen + UTF-8 synthName + int32 presetNameLen + UTF-8 presetName
//         + int32 innerLen + inner state bytes.
juce::MemoryBlock serializePreset (const PresetData&);

// Returns false (leaving `out` untouched) on wrong magic/version or truncation.
bool parsePreset (const void* data, int size, PresetData& out);
}
```

- [ ] **Step 4: Write the implementation** — `src/PresetCodec.cpp`:

```cpp
#include "PresetCodec.h"

namespace custos
{
static void writeStr (juce::MemoryOutputStream& os, const juce::String& s)
{
    const char* utf8 = s.toRawUTF8();
    const int len = (int) std::strlen (utf8);
    os.writeInt (len);
    os.write (utf8, (size_t) len);
}

static bool readStr (juce::MemoryInputStream& is, juce::String& out)
{
    if (is.getNumBytesRemaining() < 4) return false;
    const int len = is.readInt();
    if (len < 0 || (juce::int64) len > is.getNumBytesRemaining()) return false;
    juce::MemoryBlock mb;
    is.readIntoMemoryBlock (mb, len);
    out = juce::String::fromUTF8 ((const char*) mb.getData(), len);
    return true;
}

juce::MemoryBlock serializePreset (const PresetData& p)
{
    juce::MemoryBlock mb;
    juce::MemoryOutputStream os (mb, false);
    os.write ("CUSP", 4);
    os.writeByte (1);
    writeStr (os, p.classId);
    writeStr (os, p.synthName);
    writeStr (os, p.presetName);
    os.writeInt ((int) p.innerState.getSize());
    os.write (p.innerState.getData(), p.innerState.getSize());
    os.flush();
    return mb;
}

bool parsePreset (const void* data, int size, PresetData& out)
{
    if (data == nullptr || size < 5) return false;
    juce::MemoryInputStream is (data, (size_t) size, false);
    char magic[4] = {};
    if (is.read (magic, 4) != 4 || std::memcmp (magic, "CUSP", 4) != 0) return false;
    const int version = is.readByte();
    if (version != 1) return false;

    PresetData p;
    if (! readStr (is, p.classId))   return false;
    if (! readStr (is, p.synthName)) return false;
    if (! readStr (is, p.presetName))return false;
    if (is.getNumBytesRemaining() < 4) return false;
    const int innerLen = is.readInt();
    if (innerLen < 0 || (juce::int64) innerLen > is.getNumBytesRemaining()) return false;
    is.readIntoMemoryBlock (p.innerState, innerLen);

    out = std::move (p);
    return true;
}
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build C:/dev/custos/build --target custos_tests && C:/dev/custos/build/tests/custos_tests.exe "preset round-trips*,parsePreset rejects*"`
Expected: PASS (2 test cases).

- [ ] **Step 6: Commit**

```bash
git add src/PresetCodec.h src/PresetCodec.cpp tests/PresetCodecTest.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(preset): .cuspreset codec (serialize/parse)"
```

---

### Task 2: PresetStore — file operations

**Files:**
- Create: `src/PresetStore.h`, `src/PresetStore.cpp`, `tests/PresetStoreTest.cpp`
- Modify: `CMakeLists.txt` (add `src/PresetStore.cpp` to `custos_core`), `tests/CMakeLists.txt` (add `PresetStoreTest.cpp`)

**Interfaces:**
- Consumes: `PresetData`, `serializePreset`, `parsePreset` (Task 1).
- Produces: `presetFolderFor`, `sanitizePresetName`, `savePreset`, `listPresets`, `loadPreset`, `deletePreset`, `renamePreset` (signatures in Step 3).

- [ ] **Step 1: Write the failing tests** — create `tests/PresetStoreTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "PresetStore.h"

using namespace custos;

static PresetData makePreset (const juce::String& classId, const juce::String& name,
                              const juce::String& marker)
{
    PresetData p; p.classId = classId; p.synthName = "Synth"; p.presetName = name;
    p.innerState.append (marker.toRawUTF8(), marker.getNumBytesAsUTF8());
    return p;
}

TEST_CASE ("save then list returns names alphabetically case-insensitive")
{
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    const juce::String cls = "SynthA";
    REQUIRE (savePreset (root, makePreset (cls, "banana", "b")));
    REQUIRE (savePreset (root, makePreset (cls, "Apple",  "a")));
    const auto names = listPresets (root, cls);
    REQUIRE (names.size() == 2);
    REQUIRE (names[0] == "Apple");
    REQUIRE (names[1] == "banana");
    root.deleteRecursively();
}

TEST_CASE ("load returns the stored inner state and metadata")
{
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    REQUIRE (savePreset (root, makePreset ("SynthA", "Warm Pad", "hello")));
    PresetData out;
    REQUIRE (loadPreset (root, "SynthA", "Warm Pad", out));
    REQUIRE (out.classId == "SynthA");
    REQUIRE (out.innerState.matches ("hello", 5));
    REQUIRE_FALSE (loadPreset (root, "SynthA", "Missing", out));
    root.deleteRecursively();
}

TEST_CASE ("two synths keep separate folders keyed by classId")
{
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    REQUIRE (savePreset (root, makePreset ("SynthA", "P1", "a")));
    REQUIRE (savePreset (root, makePreset ("SynthB", "P1", "b")));
    REQUIRE (listPresets (root, "SynthA").size() == 1);
    REQUIRE (listPresets (root, "SynthB").size() == 1);
    root.deleteRecursively();
}

TEST_CASE ("rename and delete")
{
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    REQUIRE (savePreset (root, makePreset ("SynthA", "Old", "x")));
    REQUIRE (renamePreset (root, "SynthA", "Old", "New"));
    REQUIRE (listPresets (root, "SynthA") == std::vector<juce::String> { "New" });
    REQUIRE (deletePreset (root, "SynthA", "New"));
    REQUIRE (listPresets (root, "SynthA").empty());
    root.deleteRecursively();
}

TEST_CASE ("save with an all-illegal name is rejected, no file written")
{
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    REQUIRE_FALSE (savePreset (root, makePreset ("SynthA", "///", "x")));
    REQUIRE (listPresets (root, "SynthA").empty());
    root.deleteRecursively();
}
```

- [ ] **Step 2: Add both files to CMake, then build to verify it fails**

Add `src/PresetStore.cpp` to `custos_core` and `PresetStoreTest.cpp` to `custos_tests`.
Run: `cmake --build C:/dev/custos/build --target custos_tests`
Expected: FAIL — `PresetStore.h` not found.

- [ ] **Step 3: Write the header** — `src/PresetStore.h`:

```cpp
#pragma once
#include <juce_core/juce_core.h>
#include "PresetCodec.h"
#include <vector>

namespace custos
{
// <root>/<hex(classId)> — class-ids are not guaranteed filesystem-safe, so the folder name
// is the hex encoding of the classId's UTF-8 bytes.
juce::File presetFolderFor (const juce::File& root, const juce::String& classId);

// Strip filesystem-illegal characters for the on-disk file stem. May return "" (all illegal).
juce::String sanitizePresetName (const juce::String& name);

// Write p under presetFolderFor(root, p.classId)/<sanitize(p.presetName)>.cuspreset (creates dirs,
// overwrites same name). Returns false if the sanitized name is empty (nothing written).
bool savePreset (const juce::File& root, const PresetData& p);

// File stems (no extension) in the synth's folder, alphabetical, case-insensitive.
std::vector<juce::String> listPresets (const juce::File& root, const juce::String& classId);

// Load by display name (sanitized to locate the file). false if missing or corrupt.
bool loadPreset (const juce::File& root, const juce::String& classId,
                 const juce::String& name, PresetData& out);

bool deletePreset (const juce::File& root, const juce::String& classId, const juce::String& name);
bool renamePreset (const juce::File& root, const juce::String& classId,
                   const juce::String& oldName, const juce::String& newName);
}
```

- [ ] **Step 4: Write the implementation** — `src/PresetStore.cpp`:

```cpp
#include "PresetStore.h"

namespace custos
{
juce::File presetFolderFor (const juce::File& root, const juce::String& classId)
{
    return root.getChildFile (juce::String::toHexString (classId.toRawUTF8(),
                                                         classId.getNumBytesAsUTF8(), 0));
}

juce::String sanitizePresetName (const juce::String& name)
{
    juce::String s;
    for (auto c : name)
        if (juce::CharacterFunctions::isPrintable (c)
            && juce::String ("\\/:*?\"<>|").indexOfChar (c) < 0)
            s += c;
    return s.trim();
}

static juce::File fileFor (const juce::File& root, const juce::String& classId, const juce::String& name)
{
    return presetFolderFor (root, classId).getChildFile (sanitizePresetName (name) + ".cuspreset");
}

bool savePreset (const juce::File& root, const PresetData& p)
{
    const auto stem = sanitizePresetName (p.presetName);
    if (stem.isEmpty()) return false;
    auto folder = presetFolderFor (root, p.classId);
    folder.createDirectory();
    auto file = folder.getChildFile (stem + ".cuspreset");
    const auto blob = serializePreset (p);
    return file.replaceWithData (blob.getData(), blob.getSize());
}

std::vector<juce::String> listPresets (const juce::File& root, const juce::String& classId)
{
    std::vector<juce::String> names;
    auto folder = presetFolderFor (root, classId);
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.cuspreset"))
        names.push_back (f.getFileNameWithoutExtension());
    std::sort (names.begin(), names.end(),
               [] (const juce::String& a, const juce::String& b)
               { return a.compareIgnoreCase (b) < 0; });
    return names;
}

bool loadPreset (const juce::File& root, const juce::String& classId,
                 const juce::String& name, PresetData& out)
{
    auto file = fileFor (root, classId, name);
    if (! file.existsAsFile()) return false;
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb)) return false;
    return parsePreset (mb.getData(), (int) mb.getSize(), out);
}

bool deletePreset (const juce::File& root, const juce::String& classId, const juce::String& name)
{
    auto file = fileFor (root, classId, name);
    return file.existsAsFile() && file.deleteFile();
}

bool renamePreset (const juce::File& root, const juce::String& classId,
                   const juce::String& oldName, const juce::String& newName)
{
    const auto stem = sanitizePresetName (newName);
    if (stem.isEmpty()) return false;
    auto from = fileFor (root, classId, oldName);
    if (! from.existsAsFile()) return false;
    return from.moveFileTo (presetFolderFor (root, classId).getChildFile (stem + ".cuspreset"));
}
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build C:/dev/custos/build --target custos_tests && C:/dev/custos/build/tests/custos_tests.exe "save then list*,load returns*,two synths*,rename and delete,save with an all-illegal*"`
Expected: PASS (5 test cases).

- [ ] **Step 6: Commit**

```bash
git add src/PresetStore.h src/PresetStore.cpp tests/PresetStoreTest.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(preset): PresetStore file ops (save/list/load/rename/delete)"
```

---

### Task 3: PresetStore — root path persistence

**Files:**
- Modify: `src/PresetStore.h`, `src/PresetStore.cpp`, `tests/PresetStoreTest.cpp`

**Interfaces:**
- Produces: `juce::File presetRootConfigFile();`, `juce::File defaultPresetRoot();`, `void writePresetRoot (const juce::File&, const juce::String&);`, `juce::String readPresetRoot (const juce::File&);`

- [ ] **Step 1: Write the failing test** — append to `tests/PresetStoreTest.cpp`:

```cpp
TEST_CASE ("preset root persists and falls back to default when unset")
{
    auto cfg = juce::File::createTempFile (".txt"); cfg.deleteFile();
    REQUIRE (readPresetRoot (cfg) == defaultPresetRoot().getFullPathName());  // missing -> default
    writePresetRoot (cfg, "C:/Rig/Snapshots/CustosPresets");
    REQUIRE (readPresetRoot (cfg) == "C:/Rig/Snapshots/CustosPresets");
    cfg.deleteFile();
}
```

- [ ] **Step 2: Build to verify it fails**

Run: `cmake --build C:/dev/custos/build --target custos_tests`
Expected: FAIL — `readPresetRoot` undeclared.

- [ ] **Step 3: Add declarations** — append to `src/PresetStore.h` (inside the namespace):

```cpp
// Machine-global root persistence (favourites-style), %APPDATA%/Custos/presetRoot.txt.
juce::File   presetRootConfigFile();
juce::File   defaultPresetRoot();                    // ~/Documents/CustosPresets
void         writePresetRoot (const juce::File& cfg, const juce::String& path);
juce::String readPresetRoot (const juce::File& cfg); // missing/empty -> defaultPresetRoot() path
```

- [ ] **Step 4: Add definitions** — append to `src/PresetStore.cpp` (inside the namespace):

```cpp
juce::File presetRootConfigFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
              .getChildFile ("Custos").getChildFile ("presetRoot.txt");
}

juce::File defaultPresetRoot()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
              .getChildFile ("CustosPresets");
}

void writePresetRoot (const juce::File& cfg, const juce::String& path)
{
    cfg.getParentDirectory().createDirectory();
    cfg.replaceWithText (path);
}

juce::String readPresetRoot (const juce::File& cfg)
{
    if (cfg.existsAsFile())
    {
        const auto p = cfg.loadFileAsString().trim();
        if (p.isNotEmpty()) return p;
    }
    return defaultPresetRoot().getFullPathName();
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build C:/dev/custos/build --target custos_tests && C:/dev/custos/build/tests/custos_tests.exe "preset root persists*"`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/PresetStore.h src/PresetStore.cpp tests/PresetStoreTest.cpp
git commit -m "feat(preset): persist preset root path with default fallback"
```

---

### Task 4: Processor — inner-state and synth-key accessors

**Files:**
- Modify: `src/CustosProcessor.h`, `src/CustosProcessor.cpp`
- Create: `tests/PresetProcessorTest.cpp`
- Modify: `tests/CMakeLists.txt` (add `PresetProcessorTest.cpp`)

**Interfaces:**
- Consumes: `juce::AudioPluginInstance::getPluginDescription().createIdentifierString()`; `inner->getStateInformation/setStateInformation`.
- Produces: `juce::MemoryBlock captureInnerState() const;`, `bool restoreInnerState (const juce::MemoryBlock&);`, `juce::String innerSynthKey() const;`

- [ ] **Step 1: Write the failing test** — create `tests/PresetProcessorTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"

using namespace custos;

TEST_CASE ("innerSynthKey is empty with no synth and non-empty once loaded")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    REQUIRE (proc.innerSynthKey().isEmpty());
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());
    REQUIRE (proc.innerSynthKey().isNotEmpty());   // falls back to the synth name for a non-plugin inner
}

TEST_CASE ("capture and restore inner state round-trips through the fake")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    auto fake = std::make_unique<test::FakeInnerProcessor>();
    fake->stateMarker = "abc";
    auto* raw = fake.get();
    proc.attachInner (std::move (fake));

    const auto captured = proc.captureInnerState();
    REQUIRE (captured.matches ("abc", 3));
    REQUIRE (proc.restoreInnerState (captured));
    REQUIRE (raw->restoredMarker == "abc");
}
```

- [ ] **Step 2: Add the test to CMake, then build to verify it fails**

Add `PresetProcessorTest.cpp` to `custos_tests`.
Run: `cmake --build C:/dev/custos/build --target custos_tests`
Expected: FAIL — `innerSynthKey` not a member.

- [ ] **Step 3: Declare the accessors** — in `src/CustosProcessor.h`, after `currentPath()` (around line 107):

```cpp
    // Preset store integration (message thread).
    juce::String innerSynthKey() const;                        // stable key of the loaded synth ("" if none)
    juce::MemoryBlock captureInnerState() const;               // inner->getStateInformation ({} if none)
    bool restoreInnerState (const juce::MemoryBlock& state);   // inner->setStateInformation; false if no inner
```

- [ ] **Step 4: Define the accessors** — in `src/CustosProcessor.cpp`, near `innerSynthName()` (around line 358):

```cpp
juce::String CustosProcessor::innerSynthKey() const
{
    if (inner == nullptr) return {};
    if (auto* api = dynamic_cast<juce::AudioPluginInstance*> (inner.get()))
    {
        const auto id = api->getPluginDescription().createIdentifierString();
        if (id.isNotEmpty()) return id;
    }
    return inner->getName();   // fallback (tests / non-plugin inners)
}

juce::MemoryBlock CustosProcessor::captureInnerState() const
{
    juce::MemoryBlock mb;
    if (inner != nullptr) inner->getStateInformation (mb);
    return mb;
}

bool CustosProcessor::restoreInnerState (const juce::MemoryBlock& state)
{
    if (inner == nullptr) return false;
    inner->setStateInformation (state.getData(), (int) state.getSize());
    return true;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build C:/dev/custos/build --target custos_tests && C:/dev/custos/build/tests/custos_tests.exe "innerSynthKey*,capture and restore*"`
Expected: PASS (2 test cases).

- [ ] **Step 6: Commit**

```bash
git add src/CustosProcessor.h src/CustosProcessor.cpp tests/PresetProcessorTest.cpp tests/CMakeLists.txt
git commit -m "feat(preset): processor inner-state + synth-key accessors"
```

---

### Task 5: Processor — root, save, list, load

**Files:**
- Modify: `src/CustosProcessor.h`, `src/CustosProcessor.cpp`, `tests/PresetProcessorTest.cpp`

**Interfaces:**
- Consumes: `PresetStore` (Task 2/3), accessors (Task 4), `outboundSink`, `identity()`.
- Produces: `void setPresetRoot (const juce::String&);`, `juce::String presetRoot() const;`, `int savePreset (const juce::String& name);`, `std::vector<juce::String> listPresets() const;`, `bool loadPresetByName (const juce::String&);`, `bool loadPresetAt (int index);`
- Feedback: `/custos/preset/root N path` (on setroot), `/custos/preset/saved N name idx`, `/custos/preset/loaded N name idx`, `/custos/preset/error N reason`.

- [ ] **Step 1: Write the failing test** — append to `tests/PresetProcessorTest.cpp` (each test captures emissions into a local `msgs` vector via `outboundSink`):

```cpp
TEST_CASE ("save writes a file and emits saved with identity first")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc;
    proc.setIdentity (7);
    proc.setPresetRoot (root.getFullPathName());
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());

    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };

    const int idx = proc.savePreset ("Warm Pad");
    REQUIRE (idx == 0);
    REQUIRE (proc.listPresets() == std::vector<juce::String> { "Warm Pad" });
    REQUIRE (msgs.size() == 1);
    REQUIRE (msgs[0].getAddressPattern().toString() == "/custos/preset/saved");
    REQUIRE ((int) msgs[0][0].getInt32() == 7);
    root.deleteRecursively();
}

TEST_CASE ("save with no synth loaded emits error, writes nothing")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc;
    proc.setIdentity (3);
    proc.setPresetRoot (root.getFullPathName());
    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };

    REQUIRE (proc.savePreset ("X") == -1);
    REQUIRE (msgs.size() == 1);
    REQUIRE (msgs[0].getAddressPattern().toString() == "/custos/preset/error");
    root.deleteRecursively();
}

TEST_CASE ("load restores inner state and emits loaded")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc;
    proc.setIdentity (2);
    proc.setPresetRoot (root.getFullPathName());
    auto fake = std::make_unique<test::FakeInnerProcessor>();
    fake->stateMarker = "saved-sound";
    auto* raw = fake.get();
    proc.attachInner (std::move (fake));
    proc.savePreset ("Lead");

    raw->restoredMarker = "";           // clear
    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };
    REQUIRE (proc.loadPresetByName ("Lead"));
    REQUIRE (raw->restoredMarker == "saved-sound");
    REQUIRE (msgs.back().getAddressPattern().toString() == "/custos/preset/loaded");

    REQUIRE_FALSE (proc.loadPresetByName ("Nope"));   // missing -> error
    REQUIRE (msgs.back().getAddressPattern().toString() == "/custos/preset/error");
    root.deleteRecursively();
}
```

- [ ] **Step 2: Build to verify it fails**

Run: `cmake --build C:/dev/custos/build --target custos_tests`
Expected: FAIL — `setPresetRoot` not a member.

- [ ] **Step 3: Declare the methods and state** — in `src/CustosProcessor.h`, extend the preset block from Task 4:

```cpp
    void         setPresetRoot (const juce::String& path);     // persist (KM push-once) + remember
    juce::String presetRoot() const { return presetRootPath; }
    int  savePreset (const juce::String& name);                // sorted index, or -1 (no synth / empty name)
    std::vector<juce::String> listPresets() const;             // current synth's presets, alphabetical
    bool loadPresetByName (const juce::String& name);          // emits loaded/error
    bool loadPresetAt (int index);                             // by sorted index; emits loaded/error
```

And in the private section (near `browseIndex`, around line 176):

```cpp
    juce::String presetRootPath;   // resolved preset root (from PresetStore config; KM may override)
    void emitPreset (const juce::String& verb, const juce::String& name, int idx);  // /custos/preset/<verb> N name idx
    void emitPresetError (const juce::String& reason);                               // /custos/preset/error N reason
    int  indexOfPreset (const juce::String& name) const;                             // in listPresets() (-1 if none)
```

- [ ] **Step 4: Define the methods** — in `src/CustosProcessor.cpp`, add near the other preset accessors. Include the header at the top (`#include "PresetStore.h"`):

```cpp
void CustosProcessor::setPresetRoot (const juce::String& path)
{
    presetRootPath = path;
    writePresetRoot (presetRootConfigFile(), path);
    if (outboundSink)
    {
        juce::OSCMessage m ("/custos/preset/root");
        m.addInt32 (identityN);
        m.addString (path);
        outboundSink (m);
    }
}

int CustosProcessor::indexOfPreset (const juce::String& name) const
{
    const auto names = listPresets();
    for (int i = 0; i < (int) names.size(); ++i)
        if (names[(size_t) i].equalsIgnoreCase (name)) return i;
    return -1;
}

std::vector<juce::String> CustosProcessor::listPresets() const
{
    const auto key = innerSynthKey();
    if (key.isEmpty() || presetRootPath.isEmpty()) return {};
    return custos::listPresets (juce::File (presetRootPath), key);
}

int CustosProcessor::savePreset (const juce::String& name)
{
    const auto key = innerSynthKey();
    if (key.isEmpty()) { emitPresetError ("no synth loaded"); return -1; }
    if (sanitizePresetName (name).isEmpty()) { emitPresetError ("invalid name"); return -1; }

    PresetData p;
    p.classId    = key;
    p.synthName  = innerSynthName();
    p.presetName = name;
    p.innerState = captureInnerState();
    if (! custos::savePreset (juce::File (presetRootPath), p)) { emitPresetError ("write failed"); return -1; }

    const int idx = indexOfPreset (name);
    emitPreset ("saved", name, idx);
    return idx;
}

bool CustosProcessor::loadPresetByName (const juce::String& name)
{
    const auto key = innerSynthKey();
    if (key.isEmpty()) { emitPresetError ("no synth loaded"); return false; }
    PresetData p;
    if (! custos::loadPreset (juce::File (presetRootPath), key, name, p))
        { emitPresetError ("preset not found"); return false; }
    if (p.classId != key) { emitPresetError ("preset belongs to another synth"); return false; }
    restoreInnerState (p.innerState);
    emitPreset ("loaded", name, indexOfPreset (name));
    return true;
}

bool CustosProcessor::loadPresetAt (int index)
{
    const auto names = listPresets();
    if (index < 0 || index >= (int) names.size()) { emitPresetError ("index out of range"); return false; }
    return loadPresetByName (names[(size_t) index]);
}

void CustosProcessor::emitPreset (const juce::String& verb, const juce::String& name, int idx)
{
    if (! outboundSink) return;
    juce::OSCMessage m ("/custos/preset/" + verb);
    m.addInt32 (identityN);
    m.addString (name);
    m.addInt32 (idx);
    outboundSink (m);
}

void CustosProcessor::emitPresetError (const juce::String& reason)
{
    if (! outboundSink) return;
    juce::OSCMessage m ("/custos/preset/error");
    m.addInt32 (identityN);
    m.addString (reason);
    outboundSink (m);
}
```

Also, in the constructor (or first use), initialise `presetRootPath` from the config so a fresh instance already has the default: add `presetRootPath = readPresetRoot (presetRootConfigFile());` to the `CustosProcessor` constructor body.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build C:/dev/custos/build --target custos_tests && C:/dev/custos/build/tests/custos_tests.exe "save writes a file*,save with no synth*,load restores*"`
Expected: PASS (3 test cases).

- [ ] **Step 6: Commit**

```bash
git add src/CustosProcessor.h src/CustosProcessor.cpp tests/PresetProcessorTest.cpp
git commit -m "feat(preset): processor save/list/load + root + feedback"
```

---

### Task 6: Processor — live recall cursor (next / prev / set)

**Files:**
- Modify: `src/CustosProcessor.h`, `src/CustosProcessor.cpp`, `tests/PresetProcessorTest.cpp`

**Interfaces:**
- Consumes: `listPresets`, `loadPresetAt`, `emitPreset` (Task 5); the `DebounceTimer` pattern used by `browseInstrument`.
- Produces: `void presetNext();`, `void presetPrev();`, `void presetSet (int index);`
- Feedback: `/custos/preset/browsing N name idx` on each step; `/custos/preset/loaded` when it settles / on `presetSet`.

- [ ] **Step 1: Write the failing test** — append to `tests/PresetProcessorTest.cpp`:

```cpp
TEST_CASE ("presetSet loads immediately by index and emits loaded")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc;
    proc.setIdentity (1);
    proc.setPresetRoot (root.getFullPathName());
    auto fake = std::make_unique<test::FakeInnerProcessor>();
    auto* raw = fake.get();
    proc.attachInner (std::move (fake));
    proc.savePreset ("Apple");   // idx 0
    proc.savePreset ("Banana");  // idx 1

    raw->stateMarker = "";       // savePreset captured "fake-state" for both; markers identical, so assert via emission
    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };

    proc.presetSet (1);
    REQUIRE (msgs.back().getAddressPattern().toString() == "/custos/preset/loaded");
    REQUIRE (msgs.back()[1].getString() == "Banana");
}

TEST_CASE ("presetNext/prev step the cursor with wrap and preview browsing")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc;
    proc.setIdentity (1);
    proc.setPresetRoot (root.getFullPathName());
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());
    proc.savePreset ("Apple");   // idx 0
    proc.savePreset ("Banana");  // idx 1

    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };

    proc.presetNext();   // from unset -> idx 0 preview
    REQUIRE (msgs.back().getAddressPattern().toString() == "/custos/preset/browsing");
    REQUIRE (msgs.back()[1].getString() == "Apple");
    proc.presetNext();   // -> idx 1
    REQUIRE (msgs.back()[1].getString() == "Banana");
    proc.presetNext();   // wrap -> idx 0
    REQUIRE (msgs.back()[1].getString() == "Apple");
    proc.presetPrev();   // wrap back -> idx 1
    REQUIRE (msgs.back()[1].getString() == "Banana");
}
```

- [ ] **Step 2: Build to verify it fails**

Run: `cmake --build C:/dev/custos/build --target custos_tests`
Expected: FAIL — `presetNext` not a member.

- [ ] **Step 3: Declare** — in `src/CustosProcessor.h` preset public block:

```cpp
    void presetNext();               // cursor +1 (wrap): preview browsing now, debounced load
    void presetPrev();               // cursor -1 (wrap)
    void presetSet (int index);      // absolute index: immediate load
```

Private (near `browseDebounce`):

```cpp
    int presetCursor = -1;                  // cursor into listPresets(); -1 = unset
    DebounceTimer presetDebounce;           // reuse the browse debounce struct type
    void stepPreset (int delta);            // shared next/prev cursor + preview + arm debounce
    void commitPresetLoad();                // debounce fired -> load the cursor
```

- [ ] **Step 4: Define** — in `src/CustosProcessor.cpp`:

```cpp
void CustosProcessor::stepPreset (int delta)
{
    const auto names = listPresets();
    if (names.empty()) { emitPresetError ("no presets"); return; }
    const int n = (int) names.size();
    presetCursor = presetCursor < 0 ? (delta > 0 ? 0 : n - 1)
                                    : ((presetCursor + delta) % n + n) % n;   // wrap
    emitPreset ("browsing", names[(size_t) presetCursor], presetCursor);
    presetDebounce.cb = [this] { commitPresetLoad(); };
    presetDebounce.startTimer (400);
}

void CustosProcessor::commitPresetLoad()
{
    const auto names = listPresets();
    if (presetCursor >= 0 && presetCursor < (int) names.size())
        loadPresetByName (names[(size_t) presetCursor]);
}

void CustosProcessor::presetNext() { stepPreset (+1); }
void CustosProcessor::presetPrev() { stepPreset (-1); }

void CustosProcessor::presetSet (int index)
{
    presetDebounce.stopTimer();
    presetCursor = index;
    loadPresetAt (index);   // immediate; emits loaded or error
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build C:/dev/custos/build --target custos_tests && C:/dev/custos/build/tests/custos_tests.exe "presetSet loads*,presetNext/prev step*"`
Expected: PASS (2 test cases).

- [ ] **Step 6: Commit**

```bash
git add src/CustosProcessor.h src/CustosProcessor.cpp tests/PresetProcessorTest.cpp
git commit -m "feat(preset): live recall cursor (next/prev/set) with debounce preview"
```

---

### Task 7: Processor — rename and delete

**Files:**
- Modify: `src/CustosProcessor.h`, `src/CustosProcessor.cpp`, `tests/PresetProcessorTest.cpp`

**Interfaces:**
- Consumes: `PresetStore::renamePreset/deletePreset`, `emitPreset`/`emitPresetError`, `innerSynthKey`.
- Produces: `bool renamePreset (const juce::String& oldName, const juce::String& newName);`, `bool deletePreset (const juce::String& name);`
- Feedback: `/custos/preset/renamed N newName idx`, `/custos/preset/deleted N name -1`; errors via `/custos/preset/error`.

- [ ] **Step 1: Write the failing test** — append to `tests/PresetProcessorTest.cpp`:

```cpp
TEST_CASE ("rename and delete presets emit feedback")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc;
    proc.setIdentity (4);
    proc.setPresetRoot (root.getFullPathName());
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());
    proc.savePreset ("Old");

    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };

    REQUIRE (proc.renamePreset ("Old", "New"));
    REQUIRE (msgs.back().getAddressPattern().toString() == "/custos/preset/renamed");
    REQUIRE (proc.listPresets() == std::vector<juce::String> { "New" });

    REQUIRE (proc.deletePreset ("New"));
    REQUIRE (msgs.back().getAddressPattern().toString() == "/custos/preset/deleted");
    REQUIRE (proc.listPresets().empty());

    REQUIRE_FALSE (proc.deletePreset ("Ghost"));
    REQUIRE (msgs.back().getAddressPattern().toString() == "/custos/preset/error");
    root.deleteRecursively();
}
```

- [ ] **Step 2: Build to verify it fails**

Run: `cmake --build C:/dev/custos/build --target custos_tests`
Expected: FAIL — `renamePreset` not a member.

- [ ] **Step 3: Declare** — in `src/CustosProcessor.h` preset public block:

```cpp
    bool renamePreset (const juce::String& oldName, const juce::String& newName);  // emits renamed/error
    bool deletePreset (const juce::String& name);                                  // emits deleted/error
```

- [ ] **Step 4: Define** — in `src/CustosProcessor.cpp`:

```cpp
bool CustosProcessor::renamePreset (const juce::String& oldName, const juce::String& newName)
{
    const auto key = innerSynthKey();
    if (key.isEmpty()) { emitPresetError ("no synth loaded"); return false; }
    if (! custos::renamePreset (juce::File (presetRootPath), key, oldName, newName))
        { emitPresetError ("rename failed"); return false; }
    emitPreset ("renamed", newName, indexOfPreset (newName));
    return true;
}

bool CustosProcessor::deletePreset (const juce::String& name)
{
    const auto key = innerSynthKey();
    if (key.isEmpty()) { emitPresetError ("no synth loaded"); return false; }
    if (! custos::deletePreset (juce::File (presetRootPath), key, name))
        { emitPresetError ("delete failed"); return false; }
    emitPreset ("deleted", name, -1);
    return true;
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build C:/dev/custos/build --target custos_tests && C:/dev/custos/build/tests/custos_tests.exe "rename and delete presets*"`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/CustosProcessor.h src/CustosProcessor.cpp tests/PresetProcessorTest.cpp
git commit -m "feat(preset): processor rename/delete + feedback"
```

---

### Task 8: OSC — command parsing for `/custos/preset/*`

**Files:**
- Modify: `src/CustosOscServer.h` (Command struct + Kind enum), `src/CustosOscServer.cpp` (`parseCommand`), `tests/OscCommandTest.cpp`

**Interfaces:**
- Produces: new `Command::Kind` values `PresetSetRoot, PresetSave, PresetList, PresetLoad, PresetNext, PresetPrev, PresetSet, PresetRename, PresetDelete`; new fields `juce::String presetName, presetNewName, rootPath; int presetIndex = -1;`.

- [ ] **Step 1: Write the failing test** — append to `tests/OscCommandTest.cpp`:

```cpp
TEST_CASE ("parseCommand maps preset verbs")
{
    {
        juce::OSCMessage m ("/custos/preset/save"); m.addString ("Warm Pad");
        const auto c = parseCommand (m);
        REQUIRE (c.kind == Command::PresetSave);
        REQUIRE (c.presetName == "Warm Pad");
    }
    {
        juce::OSCMessage m ("/custos/preset/setroot"); m.addString ("C:/Rig/CustosPresets");
        REQUIRE (parseCommand (m).kind == Command::PresetSetRoot);
        REQUIRE (parseCommand (m).rootPath == "C:/Rig/CustosPresets");
    }
    {
        juce::OSCMessage m ("/custos/preset/set"); m.addInt32 (3);
        const auto c = parseCommand (m);
        REQUIRE (c.kind == Command::PresetSet);
        REQUIRE (c.presetIndex == 3);
    }
    {
        juce::OSCMessage m ("/custos/preset/load"); m.addString ("Lead");
        const auto c = parseCommand (m);
        REQUIRE (c.kind == Command::PresetLoad);
        REQUIRE (c.presetName == "Lead");
        REQUIRE (c.presetIndex == -1);
    }
    {
        juce::OSCMessage m ("/custos/preset/load"); m.addInt32 (2);
        const auto c = parseCommand (m);
        REQUIRE (c.kind == Command::PresetLoad);
        REQUIRE (c.presetIndex == 2);
    }
    {
        juce::OSCMessage m ("/custos/preset/rename"); m.addString ("Old"); m.addString ("New");
        const auto c = parseCommand (m);
        REQUIRE (c.kind == Command::PresetRename);
        REQUIRE (c.presetName == "Old");
        REQUIRE (c.presetNewName == "New");
    }
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/preset/next")).kind == Command::PresetNext);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/preset/prev")).kind == Command::PresetPrev);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/preset/list")).kind == Command::PresetList);
    { juce::OSCMessage m ("/custos/preset/delete"); m.addString ("Gone");
      REQUIRE (parseCommand (m).kind == Command::PresetDelete); }
}
```

- [ ] **Step 2: Build to verify it fails**

Run: `cmake --build C:/dev/custos/build --target custos_tests`
Expected: FAIL — `Command::PresetSave` undeclared.

- [ ] **Step 3: Extend the Command struct** — in `src/CustosOscServer.h`, add to the `Kind` enum (before `Unknown`) and add fields:

```cpp
    enum Kind { Load, Clear, Hello, Params, Volume, FavBegin, FavEntry, FavEnd,
                WindowShow, WindowTitled, WindowHide, WindowRect, MidiRoute, MidiQuery,
                BrowseNext, BrowsePrev, BrowseSet,
                PresetSetRoot, PresetSave, PresetList, PresetLoad, PresetNext, PresetPrev,
                PresetSet, PresetRename, PresetDelete, Unknown } kind = Unknown;
```

Add near the other fields:

```cpp
    juce::String presetName, presetNewName, rootPath;   // preset verbs
    int presetIndex = -1;                               // PresetSet, or PresetLoad-by-index
```

- [ ] **Step 4: Extend parseCommand** — in `src/CustosOscServer.cpp`, add these branches (follow the existing address-compare style; place before the final `return c;`):

```cpp
    if (addr == "/custos/preset/setroot")
    { c.kind = Command::PresetSetRoot;
      if (msg.size() > 0 && msg[0].isString()) c.rootPath = msg[0].getString(); return c; }
    if (addr == "/custos/preset/save")
    { c.kind = Command::PresetSave;
      if (msg.size() > 0 && msg[0].isString()) c.presetName = msg[0].getString(); return c; }
    if (addr == "/custos/preset/list")   { c.kind = Command::PresetList; return c; }
    if (addr == "/custos/preset/next")   { c.kind = Command::PresetNext; return c; }
    if (addr == "/custos/preset/prev")   { c.kind = Command::PresetPrev; return c; }
    if (addr == "/custos/preset/set")
    { c.kind = Command::PresetSet;
      if (msg.size() > 0 && msg[0].isInt32()) c.presetIndex = msg[0].getInt32(); return c; }
    if (addr == "/custos/preset/load")
    { c.kind = Command::PresetLoad;
      if (msg.size() > 0 && msg[0].isString()) c.presetName = msg[0].getString();
      else if (msg.size() > 0 && msg[0].isInt32()) c.presetIndex = msg[0].getInt32(); return c; }
    if (addr == "/custos/preset/rename")
    { c.kind = Command::PresetRename;
      if (msg.size() > 1 && msg[0].isString() && msg[1].isString())
        { c.presetName = msg[0].getString(); c.presetNewName = msg[1].getString(); } return c; }
    if (addr == "/custos/preset/delete")
    { c.kind = Command::PresetDelete;
      if (msg.size() > 0 && msg[0].isString()) c.presetName = msg[0].getString(); return c; }
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build C:/dev/custos/build --target custos_tests && C:/dev/custos/build/tests/custos_tests.exe "parseCommand maps preset verbs"`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/CustosOscServer.h src/CustosOscServer.cpp tests/OscCommandTest.cpp
git commit -m "feat(preset): parse /custos/preset/* OSC verbs"
```

---

### Task 9: OSC — dispatch wiring + GP mirror policy

**Files:**
- Modify: `src/CustosOscServer.cpp` (`oscMessageReceived` dispatch, `gpMirrorsFeedback`), `src/CustosOscServer.h` (`gpMirrorsFeedback`), `tests/OscContractTest.cpp`

**Interfaces:**
- Consumes: processor preset methods (Tasks 5-7); `parseCommand` (Task 8).
- Produces: dispatch side effects; `gpMirrorsFeedback` returns true for `/custos/preset/browsing`, `/custos/preset/loaded`, `/custos/preset/error` (so the GP-Script Voice-Selector sees recall preview + result + failures).

- [ ] **Step 1: Write the failing test** — append to `tests/OscContractTest.cpp`:

```cpp
TEST_CASE ("GP mirrors preset recall feedback")
{
    REQUIRE (gpMirrorsFeedback ("/custos/preset/browsing", {}));
    REQUIRE (gpMirrorsFeedback ("/custos/preset/loaded", {}));
    REQUIRE (gpMirrorsFeedback ("/custos/preset/error", {}));
    REQUIRE_FALSE (gpMirrorsFeedback ("/custos/preset/saved", {}));   // management noise stays hub-only
    REQUIRE_FALSE (gpMirrorsFeedback ("/custos/preset/list", {}));
}
```

- [ ] **Step 2: Build to verify it fails**

Run: `cmake --build C:/dev/custos/build --target custos_tests`
Expected: FAIL — assertion fails (mirror returns false for preset addresses).

- [ ] **Step 3: Extend the mirror policy** — in `src/CustosOscServer.h`, update `gpMirrorsFeedback`:

```cpp
inline bool gpMirrorsFeedback (const juce::String& addr, const juce::String& ackText)
{
    if (addr == "/custos/browsing" || addr == "/custos/loaded" || addr == "/custos/here")
        return true;
    if (addr == "/custos/preset/browsing" || addr == "/custos/preset/loaded"
        || addr == "/custos/preset/error")
        return true;
    if (addr == "/custos/ack")
        return ackText.startsWith ("error");
    return false;
}
```

- [ ] **Step 4: Wire dispatch** — in `src/CustosOscServer.cpp`, in `oscMessageReceived` where the parsed `Command` is switched/handled, add cases:

```cpp
    case Command::PresetSetRoot: proc.setPresetRoot (cmd.rootPath); break;
    case Command::PresetSave:    proc.savePreset (cmd.presetName); break;
    case Command::PresetList:    emitPresetList(); break;   // helper below
    case Command::PresetLoad:
        if (cmd.presetIndex >= 0) proc.loadPresetAt (cmd.presetIndex);
        else                      proc.loadPresetByName (cmd.presetName);
        break;
    case Command::PresetNext:    proc.presetNext(); break;
    case Command::PresetPrev:    proc.presetPrev(); break;
    case Command::PresetSet:     proc.presetSet (cmd.presetIndex); break;
    case Command::PresetRename:  proc.renamePreset (cmd.presetName, cmd.presetNewName); break;
    case Command::PresetDelete:  proc.deletePreset (cmd.presetName); break;
```

If the handler is an `if/else` chain rather than a `switch`, translate these to the same style. Add a small private helper to emit the list reply through the ack sender (the processor doesn't build multi-name replies):

```cpp
void CustosOscServer::emitPresetList()
{
    if (! ackReady) return;
    const auto names = proc.listPresets();
    juce::OSCMessage m ("/custos/preset/list");
    m.addInt32 (proc.identity());
    m.addInt32 ((int) names.size());
    for (const auto& n : names) m.addString (n);
    ackSender.send (m);
}
```

Declare `void emitPresetList();` in the private section of `CustosOscServer` in `src/CustosOscServer.h`.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build C:/dev/custos/build --target custos_tests && C:/dev/custos/build/tests/custos_tests.exe "GP mirrors preset*"`
Expected: PASS. Then run the whole suite to confirm nothing regressed: `C:/dev/custos/build/tests/custos_tests.exe`.
Expected: all tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/CustosOscServer.h src/CustosOscServer.cpp tests/OscContractTest.cpp
git commit -m "feat(preset): dispatch /custos/preset/* + mirror recall to GP"
```

---

### Task 10: Custos window — save field, save button, preset picker

**Files:**
- Modify: `src/CustosEditor.h`, `src/CustosEditor.cpp`, `tests/EditorTest.cpp`

**Interfaces:**
- Consumes: `proc.savePreset`, `proc.listPresets`, `proc.loadPresetAt` (Tasks 5-6).
- Produces: editor controls; `refresh()` repopulates the picker.

This task is GUI wiring; the unit test is a construction/refresh smoke test (a full interaction test needs a running message loop). Real verification is the E2E step in §6 of the spec.

- [ ] **Step 1: Write the failing test** — append to `tests/EditorTest.cpp` (mirror the existing editor-construction pattern in that file):

```cpp
TEST_CASE ("editor constructs and refreshes with presets present")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc;
    proc.setPresetRoot (root.getFullPathName());
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());
    proc.savePreset ("Warm Pad");

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    // refresh() must not crash and must reflect the one saved preset.
    dynamic_cast<CustosEditor*> (ed.get())->refresh();
    REQUIRE (proc.listPresets().size() == 1);
    root.deleteRecursively();
}
```

- [ ] **Step 2: Build to verify it fails/needs the include**

Run: `cmake --build C:/dev/custos/build --target custos_tests`
Expected: FAIL only if `EditorTest.cpp` lacks the `CustosEditor` include or the smoke test surfaces a crash; if it already compiles and passes trivially, proceed — the controls are added next.

- [ ] **Step 3: Add controls** — in `src/CustosEditor.h`, add members near `favPicker`/`openButton`:

```cpp
    juce::ComboBox   presetPicker;                          // saved presets for the loaded synth
    juce::TextEditor presetNameField;                       // name for the next Save
    juce::TextButton savePresetButton { "Save preset" };
    void rebuildPresetList();                               // fill presetPicker from proc.listPresets()
```

- [ ] **Step 4: Wire the controls** — in `src/CustosEditor.cpp`:

In the constructor, make them visible and set callbacks:

```cpp
    addAndMakeVisible (presetNameField);
    presetNameField.setTextToShowWhenEmpty ("preset name", juce::Colours::grey);
    addAndMakeVisible (savePresetButton);
    savePresetButton.onClick = [this]
    {
        const auto name = presetNameField.getText().trim();
        if (name.isNotEmpty()) { proc.savePreset (name); presetNameField.clear(); rebuildPresetList(); }
    };
    addAndMakeVisible (presetPicker);
    presetPicker.onChange = [this]
    {
        const int i = presetPicker.getSelectedItemIndex();
        if (i >= 0) proc.loadPresetAt (i);
    };
```

Add the rebuild helper and call it from `refresh()`:

```cpp
void CustosEditor::rebuildPresetList()
{
    presetPicker.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& n : proc.listPresets()) presetPicker.addItem (n, id++);
}
```

In `refresh()`, add a call to `rebuildPresetList();`. In `resized()`, lay the three controls out in a free row following the existing layout idiom (e.g. below the volume fader): give `presetNameField` and `presetPicker` a shared row with `savePresetButton`. Use the same bounds-slicing pattern already used for `favPicker`/`openButton` in that method.

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build C:/dev/custos/build --target custos_tests && C:/dev/custos/build/tests/custos_tests.exe "editor constructs and refreshes*"`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/CustosEditor.h src/CustosEditor.cpp tests/EditorTest.cpp
git commit -m "feat(preset): Custos window save field + save button + preset picker"
```

---

### Task 11: Build the plugin variants + full-suite gate

**Files:** none (verification only)

- [ ] **Step 1: Build one plugin variant to confirm the per-target compile of `CustosProcessor.cpp` still links**

Run: `cmake --build C:/dev/custos/build --target Custos5000`
Expected: builds with no errors (the new processor methods and `#include "PresetStore.h"` compile into the plugin target too).

- [ ] **Step 2: Run the entire test suite**

Run: `C:/dev/custos/build/tests/custos_tests.exe`
Expected: all tests PASS (existing + new preset tests).

- [ ] **Step 3: Manual E2E (per spec §6)** — load a synth in GP, save a preset via the Custos window, confirm the `.cuspreset` appears under `<root>/<hex(classId)>/`, then recall it over OSC (`/custos/preset/load` and `/custos/preset/next`) and confirm the `/custos/preset/loaded` ack on the hub. Record the result in the PR description.

---

## Deliverables after this plan

1. **Custos PR** from branch `custos-preset-store` — everything above.
2. **KM handoff** (separate, later) — preset-manager tab consuming the OSC wire in §4 of the spec.
3. **GP-Script handoff prompt** — Voice-Selector recall axis (`/custos/preset/next|prev|set`, consuming the mirrored `/custos/preset/browsing|loaded|error`).
