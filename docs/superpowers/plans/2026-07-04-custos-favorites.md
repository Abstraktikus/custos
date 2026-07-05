# Custos Favorites Implementation Plan (Phase C, Feature F4)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Kapellmeister pushes a ranked favourites list (name/path/order/volume per synth) to Custos over OSC; Custos persists it to a machine-level JSON config, shows it as a dropdown picker in its editor, and loads a synth on a manual pick — applying that synth's stored volume default. This also lets Custos switch synths standalone (no KM).

**Architecture:** A `FavoritesStore` unit owns the `Favorite` type + JSON (de)serialisation + the config file at `%APPDATA%\Custos\favorites.json`. The processor holds the in-memory list (push accumulator → committed, sorted by `favOrder`) — file I/O lives in the OSC server (production-only), so the processor stays hermetically testable. The editor shows a `juce::ComboBox`; a pick calls `load()`, which applies the synth's config volume default (wires F5). Parsing reuses the `Command`/`parseCommand`/OSC-server seam.

**Tech Stack:** C++17, CMake, JUCE 8.0.14 (`juce::JSON`, `juce::File`), Catch2 v3.15.1, VST3 (Windows). Builds on `master` (addressing core + param dump + volume).

## Global Constraints

- **Contract:** `docs/osc-contract.md` v1. This plan implements **F4 favourites** and wires the **F5 per-synth volume default** (deferred from the volume plan).
- **Inbound push:** `/custos/favorites/begin`, then `/custos/favorite <idx:int> <name:string> <path:string> <favOrder:int> <gainDb:float>` per entry, then `/custos/favorites/end <count:int>`.
- **Persistence:** machine-level, shared — one JSON config `%APPDATA%\Custos\favorites.json` (JUCE `userApplicationDataDirectory/Custos/favorites.json`). Push-once to any one instance writes it; every instance reads it on boot. **Not** in the per-instance VST state.
- **UI:** a `juce::ComboBox` ranked by `favOrder`; selecting an item loads that synth (`load()` → emits `/custos/loaded`).
- **Volume default:** `load(path)` applies the matching favourite's `gainDb` via `setVolumeDb` (unity if the path is not a favourite).
- **Custos loads a synth only on a manual UI pick or an OSC `/custos/load`** — the favourites push itself never loads anything.
- **Hermetic tests:** the processor's favourites logic is in-memory only (no file I/O); `FavoritesStore` file I/O is tested against temp files; unit tests never touch `%APPDATA%`.
- **Build the `Custos_VST3` target.** Build/test: `.\scripts\ci.cmd`.

---

## File Structure

```
src/
  FavoritesStore.h/.cpp  # NEW: struct Favorite; toJson/fromJson; favoritesConfigFile(); read/write
  CustosOscServer.h      # MODIFY: #include FavoritesStore.h; Command gains FavBegin/FavEntry/FavEnd + Favorite fav
  CustosOscServer.cpp    # MODIFY: parseCommand maps the 3 fav addresses; route to proc; config read(ctor)/write(end)
  CustosProcessor.h/.cpp # MODIFY: in-memory favourites (begin/add/end/setFavorites/getFavorites/loadFavorite);
                         #         load() applies the per-synth volume default
  CustosEditor.h/.cpp    # MODIFY: a ComboBox favourites picker
CMakeLists.txt           # MODIFY: add src/FavoritesStore.cpp to custos_core
tests/
  FavoritesStoreTest.cpp # NEW: JSON + file round-trip
  OscContractTest.cpp    # MODIFY: parseCommand(fav) tests
  FavoritesTest.cpp      # NEW: processor accumulator + loadFavorite + volume default (hermetic)
tests/CMakeLists.txt     # MODIFY: add FavoritesStoreTest.cpp, FavoritesTest.cpp
```

---

## Task 1: FavoritesStore — type, JSON, config file

**Files:**
- Create: `src/FavoritesStore.h`, `src/FavoritesStore.cpp`
- Create: `tests/FavoritesStoreTest.cpp`
- Modify: `CMakeLists.txt` (add `src/FavoritesStore.cpp`), `tests/CMakeLists.txt` (add `FavoritesStoreTest.cpp`)

**Interfaces:**
- Produces: `struct custos::Favorite { juce::String name, path; int favOrder = 0; float gainDb = 0.0f; };`
  `juce::String custos::favoritesToJson(const std::vector<Favorite>&)`; `std::vector<Favorite> custos::favoritesFromJson(const juce::String&)`;
  `juce::File custos::favoritesConfigFile()`; `void custos::writeFavorites(const juce::File&, const std::vector<Favorite>&)`; `std::vector<Favorite> custos::readFavorites(const juce::File&)`.

- [ ] **Step 1: Write src/FavoritesStore.h**

```cpp
#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace custos
{
struct Favorite { juce::String name, path; int favOrder = 0; float gainDb = 0.0f; };

juce::String favoritesToJson (const std::vector<Favorite>& favs);
std::vector<Favorite> favoritesFromJson (const juce::String& json);

juce::File favoritesConfigFile();                                             // %APPDATA%/Custos/favorites.json
void writeFavorites (const juce::File& file, const std::vector<Favorite>& favs);
std::vector<Favorite> readFavorites (const juce::File& file);                 // missing file -> empty
}
```

- [ ] **Step 2: Write src/FavoritesStore.cpp**

```cpp
#include "FavoritesStore.h"

namespace custos
{
juce::String favoritesToJson (const std::vector<Favorite>& favs)
{
    juce::Array<juce::var> arr;
    for (const auto& f : favs)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", f.name);
        o->setProperty ("path", f.path);
        o->setProperty ("favOrder", f.favOrder);
        o->setProperty ("gainDb", f.gainDb);
        arr.add (juce::var (o));
    }
    return juce::JSON::toString (juce::var (arr));
}

std::vector<Favorite> favoritesFromJson (const juce::String& json)
{
    std::vector<Favorite> out;
    const auto parsed = juce::JSON::parse (json);
    if (auto* arr = parsed.getArray())
        for (const auto& v : *arr)
            out.push_back ({ v.getProperty ("name", "").toString(),
                             v.getProperty ("path", "").toString(),
                             (int) v.getProperty ("favOrder", 0),
                             (float) (double) v.getProperty ("gainDb", 0.0) });
    return out;
}

juce::File favoritesConfigFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
              .getChildFile ("Custos").getChildFile ("favorites.json");
}

void writeFavorites (const juce::File& file, const std::vector<Favorite>& favs)
{
    file.getParentDirectory().createDirectory();
    file.replaceWithText (favoritesToJson (favs));
}

std::vector<Favorite> readFavorites (const juce::File& file)
{
    if (! file.existsAsFile()) return {};
    return favoritesFromJson (file.loadFileAsString());
}
}
```

- [ ] **Step 3: Write tests/FavoritesStoreTest.cpp**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "FavoritesStore.h"

using namespace custos;
using Catch::Approx;

TEST_CASE ("favorites JSON round-trips")
{
    std::vector<Favorite> favs { { "Diva", "C:/x/Diva.vst3", 1, -3.0f },
                                 { "DX7",  "C:/x/DX7.vst3",  2,  0.0f } };
    const auto back = favoritesFromJson (favoritesToJson (favs));
    REQUIRE (back.size() == 2);
    REQUIRE (back[0].name == "Diva");
    REQUIRE (back[0].path == "C:/x/Diva.vst3");
    REQUIRE (back[0].favOrder == 1);
    REQUIRE (back[0].gainDb == Approx (-3.0f));
    REQUIRE (back[1].name == "DX7");
}

TEST_CASE ("writeFavorites/readFavorites round-trip via a temp file")
{
    auto tmp = juce::File::createTempFile (".json");
    writeFavorites (tmp, { { "Pigments", "C:/x/Pigments.vst3", 5, 1.5f } });
    const auto back = readFavorites (tmp);
    REQUIRE (back.size() == 1);
    REQUIRE (back[0].name == "Pigments");
    REQUIRE (back[0].gainDb == Approx (1.5f));
    tmp.deleteFile();
}

TEST_CASE ("readFavorites on a missing file returns empty")
{
    REQUIRE (readFavorites (juce::File ("C:/nonexistent/nope.json")).empty());
}
```

- [ ] **Step 4: Register sources**

In `CMakeLists.txt`, add `src/FavoritesStore.cpp` to the `custos_core` list (after `src/StateCodec.cpp`).
In `tests/CMakeLists.txt`, add `FavoritesStoreTest.cpp` to the `add_executable(custos_tests ...)` list.

- [ ] **Step 5: Build + test**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: the three new FavoritesStore tests PASS; build pristine.

- [ ] **Step 6: Commit**

```bash
git add src/FavoritesStore.h src/FavoritesStore.cpp tests/FavoritesStoreTest.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "Favorites: FavoritesStore (Favorite + JSON + config file I/O)"
```

---

## Task 2: parseCommand for the favourites push

**Files:**
- Modify: `src/CustosOscServer.h`, `src/CustosOscServer.cpp`, `tests/OscContractTest.cpp`

**Interfaces:**
- Consumes: `custos::Favorite` (Task 1).
- Produces: `Command` gains `FavBegin`, `FavEntry`, `FavEnd` and a `Favorite fav`; `parseCommand` maps the three addresses (`/custos/favorites/begin`, `/custos/favorite <idx> <name> <path> <favOrder> <gainDb>`, `/custos/favorites/end <count>`).

- [ ] **Step 1: Write the failing tests — append to tests/OscContractTest.cpp**

```cpp
TEST_CASE ("parseCommand maps the favourites push")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/favorites/begin")).kind == Command::FavBegin);

    juce::OSCMessage entry ("/custos/favorite", 0, juce::String ("Diva"),
                            juce::String ("C:/x/Diva.vst3"), 3, -2.0f);
    const auto e = parseCommand (entry);
    REQUIRE (e.kind == Command::FavEntry);
    REQUIRE (e.fav.name == "Diva");
    REQUIRE (e.fav.path == "C:/x/Diva.vst3");
    REQUIRE (e.fav.favOrder == 3);
    REQUIRE (e.fav.gainDb == -2.0f);

    const auto end = parseCommand (juce::OSCMessage ("/custos/favorites/end", 12));
    REQUIRE (end.kind == Command::FavEnd);
    REQUIRE (end.count == 12);
}

TEST_CASE ("parseCommand rejects a malformed favourite entry")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/favorite", 0, juce::String ("x"))).kind == Command::Unknown);
}
```

- [ ] **Step 2: Run to verify failure**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: FAIL — `Command::FavBegin/FavEntry/FavEnd`, `.fav` don't exist.

- [ ] **Step 3: Extend Command — src/CustosOscServer.h**

Add the include at the top (after `#include <juce_osc/juce_osc.h>`):
```cpp
#include "FavoritesStore.h"
```
Replace:
```cpp
struct Command
{
    enum Kind { Load, Clear, Hello, Params, Volume, Unknown } kind = Unknown;
    juce::String path;
    int start = 0, count = 0;   // Params
    float gainDb = 0.0f;        // Volume
};
```
with:
```cpp
struct Command
{
    enum Kind { Load, Clear, Hello, Params, Volume, FavBegin, FavEntry, FavEnd, Unknown } kind = Unknown;
    juce::String path;
    int start = 0, count = 0;   // Params; count also = FavEnd count
    float gainDb = 0.0f;        // Volume
    Favorite fav;               // FavEntry
};
```

- [ ] **Step 4: Map the three addresses — src/CustosOscServer.cpp**

In `parseCommand`, before the final `return { Command::Unknown, {} };`, add:
```cpp
    if (addr == "/custos/favorites/begin")
        return { Command::FavBegin, {} };
    if (addr == "/custos/favorite")
    {
        if (msg.size() >= 5 && msg[0].isInt32() && msg[1].isString() && msg[2].isString()
            && msg[3].isInt32() && msg[4].isFloat32())
        {
            Command c;
            c.kind = Command::FavEntry;
            c.fav  = { msg[1].getString(), msg[2].getString(), msg[3].getInt32(), msg[4].getFloat32() };
            return c;
        }
        return { Command::Unknown, {} };
    }
    if (addr == "/custos/favorites/end")
    {
        if (msg.size() >= 1 && msg[0].isInt32())
        {
            Command c; c.kind = Command::FavEnd; c.count = msg[0].getInt32(); return c;
        }
        return { Command::Unknown, {} };
    }
```

- [ ] **Step 5: Build + test**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: the two new tests PASS; build pristine.

- [ ] **Step 6: Commit**

```bash
git add src/CustosOscServer.h src/CustosOscServer.cpp tests/OscContractTest.cpp
git commit -m "Favorites: parseCommand for the begin/entry/end push"
```

---

## Task 3: Processor favourites (in-memory) + volume default on load

**Files:**
- Modify: `src/CustosProcessor.h`, `src/CustosProcessor.cpp`
- Create: `tests/FavoritesTest.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Favorite` (Task 1); `setVolumeDb` (F5); `load`/`loadInner`.
- Produces (on `CustosProcessor`): `void favoritesBegin();` `void favoritesAdd(const Favorite&);` `void favoritesEnd();`
  `void setFavorites(std::vector<Favorite>);` `const std::vector<Favorite>& getFavorites() const;` `void loadFavorite(int index);`
  and `load(path)` now applies the per-synth volume default.

- [ ] **Step 1: Write the failing tests — tests/FavoritesTest.cpp**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"

using namespace custos;
using Catch::Approx;

TEST_CASE ("favourites push accumulates then commits, sorted by favOrder")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.favoritesBegin();
    proc.favoritesAdd ({ "B", "C:/b.vst3", 2, 0.0f });
    proc.favoritesAdd ({ "A", "C:/a.vst3", 1, 0.0f });
    proc.favoritesEnd();

    const auto& favs = proc.getFavorites();
    REQUIRE (favs.size() == 2);
    REQUIRE (favs[0].name == "A");   // favOrder 1 first
    REQUIRE (favs[1].name == "B");
}

TEST_CASE ("setFavorites replaces the list, sorted")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setFavorites ({ { "Z", "C:/z.vst3", 9, 0.0f }, { "Y", "C:/y.vst3", 4, 0.0f } });
    REQUIRE (proc.getFavorites().size() == 2);
    REQUIRE (proc.getFavorites()[0].name == "Y");
}
```
(A `load()` test needs a real VST3 and is covered by the E2E; the volume-default lookup is proven by
`applyVolumeDefault` being exercised in the E2E. The in-memory list is what unit tests cover.)

- [ ] **Step 2: Register the test + run to verify failure**

Add `FavoritesTest.cpp` to `tests/CMakeLists.txt`.
Run (PowerShell): `.\scripts\ci.cmd`
Expected: FAIL — `favoritesBegin`/`favoritesAdd`/`favoritesEnd`/`setFavorites`/`getFavorites` don't exist.

- [ ] **Step 3: Declare the API — src/CustosProcessor.h**

Add the include near the others: `#include "FavoritesStore.h"`.

In the public section, after `setVolumeDb`, add:
```cpp
    // F4 favourites (message thread). Push: begin -> add* -> end (commits + sorts). setFavorites for boot-load.
    void favoritesBegin();
    void favoritesAdd (const Favorite& f);
    void favoritesEnd();
    void setFavorites (std::vector<Favorite> favs);
    const std::vector<Favorite>& getFavorites() const noexcept { return favorites; }
    void loadFavorite (int index);   // loads getFavorites()[index]
```
In `private:` (near `favorites`-related state), add:
```cpp
    std::vector<Favorite> favorites;       // committed, sorted by favOrder
    std::vector<Favorite> favAccumulator;  // during a begin..end push
    void applyVolumeDefault (const juce::String& path);   // set the trim from the matching favourite (unity if none)
```

- [ ] **Step 4: Define the methods — src/CustosProcessor.cpp**

Add (e.g. after `setVolumeDb`):
```cpp
static void sortByFavOrder (std::vector<Favorite>& v)
{
    std::sort (v.begin(), v.end(), [] (const Favorite& a, const Favorite& b) { return a.favOrder < b.favOrder; });
}

void CustosProcessor::favoritesBegin() { favAccumulator.clear(); }
void CustosProcessor::favoritesAdd (const Favorite& f) { favAccumulator.push_back (f); }

void CustosProcessor::favoritesEnd()
{
    favorites = std::move (favAccumulator);
    favAccumulator.clear();
    sortByFavOrder (favorites);
    refreshEditor();
}

void CustosProcessor::setFavorites (std::vector<Favorite> favs)
{
    favorites = std::move (favs);
    sortByFavOrder (favorites);
    refreshEditor();
}

void CustosProcessor::loadFavorite (int index)
{
    if (index >= 0 && index < (int) favorites.size())
        load (favorites[(size_t) index].path);
}

void CustosProcessor::applyVolumeDefault (const juce::String& path)
{
    for (const auto& f : favorites)
        if (f.path == path) { setVolumeDb (f.gainDb); return; }
    setVolumeDb (0.0f);   // not a favourite -> unity
}
```
Add `#include <algorithm>` at the top of `CustosProcessor.cpp` (for `std::sort`).
In `load`, apply the default after a successful load. Replace:
```cpp
    if (auto instance = SynthLoader::loadVST3 (path, sr, block, err))
    {
        loadInner (std::move (instance), path);
        return { true, boundCount, "loaded " + path };
    }
```
with:
```cpp
    if (auto instance = SynthLoader::loadVST3 (path, sr, block, err))
    {
        loadInner (std::move (instance), path);
        applyVolumeDefault (path);
        return { true, boundCount, "loaded " + path };
    }
```

- [ ] **Step 5: Build + test**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: all tests PASS (prior + the two new Favorites tests). Build pristine.

- [ ] **Step 6: Commit**

```bash
git add src/CustosProcessor.h src/CustosProcessor.cpp tests/FavoritesTest.cpp tests/CMakeLists.txt
git commit -m "Favorites: processor list (push/setFavorites/loadFavorite) + per-synth volume default on load"
```

---

## Task 4: OSC server — route the push + config persistence

**Files:**
- Modify: `src/CustosOscServer.cpp`

**Interfaces:**
- Consumes: `parseCommand` (fav kinds), `CustosProcessor::{favoritesBegin,favoritesAdd,favoritesEnd,setFavorites,getFavorites}` (Task 3), `favoritesConfigFile`/`readFavorites`/`writeFavorites` (Task 1).
- Produces: on construction the server loads the config into the processor; on `/custos/favorites/end` it commits + writes the config.

- [ ] **Step 1: Load the config on construction — src/CustosOscServer.cpp**

In the constructor, after `proc.outboundSink = …;`, add:
```cpp
    proc.setFavorites (readFavorites (favoritesConfigFile()));   // boot-load the shared machine config
```

- [ ] **Step 2: Route the push + persist on end — src/CustosOscServer.cpp**

In `oscMessageReceived`, add cases before `case Command::Unknown:`:
```cpp
        case Command::FavBegin:
            proc.favoritesBegin();
            break;
        case Command::FavEntry:
            proc.favoritesAdd (cmd.fav);
            break;
        case Command::FavEnd:
            proc.favoritesEnd();
            writeFavorites (favoritesConfigFile(), proc.getFavorites());   // shared machine config
            break;
```

- [ ] **Step 3: Build + test**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: all tests PASS (transport — count unchanged). Build pristine, `Custos.vst3` produced.

- [ ] **Step 4: Commit**

```bash
git add src/CustosOscServer.cpp
git commit -m "Favorites: OSC route (begin/entry/end) + config read on boot / write on end"
```

---

## Task 5: Editor — the favourites picker (ComboBox)

**Files:**
- Modify: `src/CustosEditor.h`, `src/CustosEditor.cpp`

**Interfaces:**
- Consumes: `CustosProcessor::{getFavorites,loadFavorite}` (Task 3).

- [ ] **Step 1: Add the ComboBox — src/CustosEditor.h**

Add a member (after `idStatus`):
```cpp
    juce::ComboBox   favPicker;    // ranked favourites; pick -> load
```

- [ ] **Step 2: Wire it in the constructor — src/CustosEditor.cpp**

After the `idStatus` setup block (before `refresh();`), add:
```cpp
    favPicker.setTextWhenNothingSelected ("Favourites…");
    favPicker.onChange = [this]
    {
        const int i = favPicker.getSelectedItemIndex();
        if (i >= 0) proc.loadFavorite (i);
    };
    addAndMakeVisible (favPicker);
```

- [ ] **Step 3: Rebuild the list in refresh() — src/CustosEditor.cpp**

At the end of `refresh()` (after the id-status block), add:
```cpp
    favPicker.clear (juce::dontSendNotification);   // clear() alone does not fire onChange
    int id = 1;
    for (const auto& f : proc.getFavorites())
        favPicker.addItem (f.name, id++);
```

- [ ] **Step 4: Lay it out + grow the window — src/CustosEditor.cpp**

At the end of `resized()` (after the id row), add:
```cpp
    r.removeFromTop (12);
    favPicker.setBounds (r.removeFromTop (26).removeFromLeft (240));
```
Change `setSize (360, 172);` (end of the constructor) to:
```cpp
    setSize (360, 212);
```

- [ ] **Step 5: Build + test**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: all tests PASS (the `EditorTest` still constructs a non-null editor). Build pristine.

- [ ] **Step 6: Commit**

```bash
git add src/CustosEditor.h src/CustosEditor.cpp
git commit -m "Favorites: editor ComboBox picker (ranked; pick -> loadFavorite)"
```

---

## Task 6: Autonomous E2E

**Goal:** prove the push, persistence, and volume default end-to-end. Record in `experiments/favorites.md`.

- [ ] **Step 1: Deploy + bind an instance**

`.\scripts\deploy.cmd` → restart GP → confirm `/custos/hello :9109` → `/custos/here [9,…]`. Delete any stale `%APPDATA%\Custos\favorites.json` first for a clean run.

- [ ] **Step 2: Push a small favourites list**

Send, to `:9109`, in order:
`/custos/favorites/begin`;
`/custos/favorite 0 "CS-80" "C:/Program Files/Common Files/VST3/CS-80 V4.vst3" 1 -3.0`;
`/custos/favorite 1 "DX7" "C:/Program Files/Common Files/VST3/DX7 V.vst3" 2 0.0`;
`/custos/favorites/end 2`.
Verify `%APPDATA%\Custos\favorites.json` now contains both entries (name/path/favOrder/gainDb).

- [ ] **Step 3: Verify the picker + volume default**

In the Custos editor, confirm the ComboBox lists "CS-80" then "DX7". Pick "DX7" → `/custos/loaded [9, …DX7 V.vst3, 3124]` on the hub, and the trim is set to DX7's `gainDb` (0 dB). Pick "CS-80" → `/custos/loaded [9, …CS-80…, 2797]` and the trim to -3 dB (aural check).

- [ ] **Step 4: Verify standalone persistence**

Restart GP (no KM push) → the ComboBox still lists both (read from the config on boot).

- [ ] **Step 5: Record + commit**

Write `experiments/favorites.md`, then:
```bash
git add experiments/favorites.md
git commit -m "Favorites: autonomous E2E (push, persistence, volume default)"
```

---

## Self-Review

**Spec coverage (contract §5.4 F4 + §4 persistence + §5.3 volume default):**
- Push `begin/entry/end` inbound → Task 2 (`parseCommand`) + Task 4 (route). ✓
- Machine-level shared JSON config `%APPDATA%\Custos\favorites.json`, boot-read + end-write → Task 1 (`FavoritesStore`) + Task 4 (ctor read / end write). ✓
- Ranked picker, pick → load → Task 5 (ComboBox) + Task 3 (`loadFavorite`). ✓
- Load applies the per-synth volume default (wires F5) → Task 3 (`applyVolumeDefault` in `load`). ✓
- Push never loads; a pick or OSC `/custos/load` loads → Task 3/4 (push only mutates the list). ✓
- Standalone works from the persisted config → Task 4 (ctor read). ✓
- Hermetic tests (processor in-memory; store via temp files) → Tasks 1/3. ✓

**Placeholder scan:** every code step complete; no TBD/TODO. The `load()`-with-real-synth path is explicitly deferred to the E2E (a real VST3 can't run in a unit test).

**Type consistency:** `Favorite{name,path,favOrder,gainDb}` (Task 1) is used identically in Tasks 2/3/5. `Command.fav` (Task 2) is consumed in Task 4. `favoritesBegin/Add/End/setFavorites/getFavorites/loadFavorite` (Task 3) match their use in Tasks 4/5. `applyVolumeDefault` calls `setVolumeDb` (F5, on `master`). File I/O (`readFavorites`/`writeFavorites`/`favoritesConfigFile`, Task 1) is used only in Task 4 (production path), keeping Task 3 hermetic.
