# Custos Editor MIDI Route Matrix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a hands-on 16-channel MIDI route/mute matrix to the Custos plugin editor so the operator can set routing locally for testing, driving the existing v2 route model.

**Architecture:** A tiny pure header maps between ComboBox item-ids and route values (unit-tested without JUCE), and `CustosEditor` gains 16 per-input-channel ComboBoxes wired to the existing `proc.setMidiRoute()`/`proc.getMidiRoute()`. No new OSC, no model/persistence change, no KM code.

**Tech Stack:** C++17, JUCE 8.0.14, Catch2 v3.15.1. Branch `custos-editor-midi-matrix` (stacked on `custos-osc-contract-v2`, which supplies `setMidiRoute`/`getMidiRoute`).

## Global Constraints

- **Route value domain:** `std::array<int,16>`, index `i` = input MIDI channel `i+1`, value ∈ `0..16`; `0` = mute (drop), `1..16` = route to that output channel. Identity (`route[i] = i+1`) is the default.
- **ComboBox item-id convention:** id `1` = `"M"` (route value `0`); id `2..17` = `"1".."16"` (route value `1..16`). So `routeValue = itemId - 1`, `itemId = routeValue + 1`.
- **Custos-only.** No new OSC verb, no `emitMidiRoute` from the editor, no change to the route model / state / contract. KM stays master (last writer wins).
- **Repo language:** English (code, comments, commits).
- **Build/test (non-standard env — cmake/ninja/cl NOT on PATH):** from Bash, `cmd //c "C:\Users\marti\AppData\Local\Temp\claude\C--dev\e7d0bc10-3576-48cc-8b73-fa1f0687b13e\scratchpad\custos-bt.cmd" "<catch2-filter>"` builds `custos_tests` (vcvars-loaded) and runs it; no filter = whole suite. Do NOT use plain `cmake`/`ctest`.

---

### Task 1: Pure route ↔ item-id mapping helper

**Files:**
- Create: `src/MidiRouteMatrix.h`
- Create: `tests/MidiRouteMatrixTest.cpp`
- Modify: `tests/CMakeLists.txt` (add the test file)

**Interfaces:**
- Produces: `int custos::routeValueFromItemId(int)`, `int custos::itemIdFromRouteValue(int)`, `std::array<int,16> custos::routeFromItemIds(const std::array<int,16>&)`, `std::array<int,16> custos::itemIdsFromRoute(const std::array<int,16>&)`. All clamp to valid domains; header-only (inline), no `.cpp`.

- [ ] **Step 1: Write the failing test** — `tests/MidiRouteMatrixTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "MidiRouteMatrix.h"
#include <array>

using namespace custos;

TEST_CASE ("route selector item-id <-> route value mapping")
{
    REQUIRE (routeValueFromItemId (1)  == 0);   // "M" -> mute
    REQUIRE (routeValueFromItemId (2)  == 1);   // "1" -> route to output 1
    REQUIRE (routeValueFromItemId (17) == 16);  // "16" -> route to output 16
    REQUIRE (itemIdFromRouteValue (0)  == 1);   // mute -> "M"
    REQUIRE (itemIdFromRouteValue (16) == 17);
    REQUIRE (routeValueFromItemId (0)  == 0);   // no selection -> mute (defensive clamp)
    REQUIRE (routeValueFromItemId (99) == 16);  // clamp high
}

TEST_CASE ("route <-> item-id arrays round-trip (identity and mixed)")
{
    std::array<int,16> ident {}; for (int i = 0; i < 16; ++i) ident[(size_t) i] = i + 1;
    REQUIRE (routeFromItemIds (itemIdsFromRoute (ident)) == ident);

    std::array<int,16> mixed = ident;
    mixed[7] = 0;   // input ch 8 muted
    mixed[3] = 1;   // input ch 4 -> output 1
    REQUIRE (routeFromItemIds (itemIdsFromRoute (mixed)) == mixed);

    const auto ids = itemIdsFromRoute (ident);
    REQUIRE (ids[0]  == 2);    // route value 1  -> item id 2
    REQUIRE (ids[15] == 17);   // route value 16 -> item id 17
}
```

- [ ] **Step 2: Register the test file** — in `tests/CMakeLists.txt`, add `MidiRouteMatrixTest.cpp` to the `add_executable(custos_tests ...)` source list (alongside `MidiRouteTest.cpp`).

- [ ] **Step 3: Run to verify it fails**

Run: `cmd //c "C:\Users\marti\AppData\Local\Temp\claude\C--dev\e7d0bc10-3576-48cc-8b73-fa1f0687b13e\scratchpad\custos-bt.cmd" "route selector*"`
Expected: FAIL — `MidiRouteMatrix.h` not found (compile error).

- [ ] **Step 4: Write the header** — `src/MidiRouteMatrix.h`:

```cpp
#pragma once
#include <array>

namespace custos
{
// Route selector ComboBox item-ids: id 1 = "M" (route value 0 = mute); id 2..17 = "1".."16"
// (route value 1..16). routeValue = itemId - 1 ; itemId = routeValue + 1. Both directions clamp.
inline int routeValueFromItemId (int itemId)
{
    const int v = itemId - 1;
    return v < 0 ? 0 : (v > 16 ? 16 : v);
}

inline int itemIdFromRouteValue (int routeValue)
{
    const int v = routeValue < 0 ? 0 : (routeValue > 16 ? 16 : routeValue);
    return v + 1;
}

inline std::array<int, 16> routeFromItemIds (const std::array<int, 16>& itemIds)
{
    std::array<int, 16> out {};
    for (int i = 0; i < 16; ++i) out[(size_t) i] = routeValueFromItemId (itemIds[(size_t) i]);
    return out;
}

inline std::array<int, 16> itemIdsFromRoute (const std::array<int, 16>& route)
{
    std::array<int, 16> out {};
    for (int i = 0; i < 16; ++i) out[(size_t) i] = itemIdFromRouteValue (route[(size_t) i]);
    return out;
}
}
```

- [ ] **Step 5: Run to verify it passes**

Run: `cmd //c "C:\Users\marti\AppData\Local\Temp\claude\C--dev\e7d0bc10-3576-48cc-8b73-fa1f0687b13e\scratchpad\custos-bt.cmd" "route selector*" "route <-> item-id*"`
Expected: PASS (2 cases). Then run the full suite (no filter) — all green.

- [ ] **Step 6: Commit**

```bash
git add src/MidiRouteMatrix.h tests/MidiRouteMatrixTest.cpp tests/CMakeLists.txt
git commit -m "Editor matrix: pure route <-> ComboBox item-id mapping helper"
```

---

### Task 2: MIDI route matrix in CustosEditor

**Files:**
- Modify: `src/CustosEditor.h` (members + helper decl + include)
- Modify: `src/CustosEditor.cpp` (construct/populate, onChange, refresh sync, resized layout, grow size)

**Interfaces:**
- Consumes: `routeFromItemIds`/`itemIdsFromRoute` (Task 1); `proc.setMidiRoute(const std::array<int,16>&)` / `proc.getMidiRoute()` (from the v2 branch).
- Produces: a `MIDI ch → out` section in the editor; `CustosEditor::gatherRouteFromBoxes()`.

- [ ] **Step 1: Declare members + helper** — in `src/CustosEditor.h`: add `#include <array>` near the top includes, and inside the `private:` section (after the `idField` members) add:

```cpp
    juce::Label      midiLabel;                     // "MIDI ch -> out"
    std::array<juce::Label, 16>    routeChanLabel;  // input channel captions 1..16
    std::array<juce::ComboBox, 16> routeBox;        // per input channel: M / 1..16 (output)
    void gatherRouteFromBoxes();                    // read the 16 boxes -> proc.setMidiRoute
```

- [ ] **Step 2: Include the helper** — at the top of `src/CustosEditor.cpp`, add:

```cpp
#include "MidiRouteMatrix.h"
```

- [ ] **Step 3: Construct + populate the widgets** — in the `CustosEditor` constructor, just before the final `refresh();` call, add:

```cpp
    // MIDI route matrix (local test convenience; drives proc.setMidiRoute). M = mute (route 0).
    midiLabel.setText ("MIDI ch -> out", juce::dontSendNotification);
    addAndMakeVisible (midiLabel);
    for (int ch = 0; ch < 16; ++ch)
    {
        routeChanLabel[(size_t) ch].setText (juce::String (ch + 1), juce::dontSendNotification);
        routeChanLabel[(size_t) ch].setJustificationType (juce::Justification::centred);
        routeChanLabel[(size_t) ch].setFont (juce::Font (11.0f));
        addAndMakeVisible (routeChanLabel[(size_t) ch]);

        auto& b = routeBox[(size_t) ch];
        b.addItem ("M", 1);                                        // id 1 = mute (route 0)
        for (int out = 1; out <= 16; ++out) b.addItem (juce::String (out), out + 1);  // ids 2..17
        b.onChange = [this] { gatherRouteFromBoxes(); };
        addAndMakeVisible (b);
    }
```

- [ ] **Step 4: Implement gatherRouteFromBoxes** — add to `src/CustosEditor.cpp` (e.g. after `commitIdentity`):

```cpp
void CustosEditor::gatherRouteFromBoxes()
{
    std::array<int, 16> ids {};
    for (int i = 0; i < 16; ++i) ids[(size_t) i] = routeBox[(size_t) i].getSelectedId();
    proc.setMidiRoute (routeFromItemIds (ids));
}
```

- [ ] **Step 5: Sync the boxes in refresh()** — in `CustosEditor::refresh()`, before the final identity/size block (before `const bool showId = idVisible();`), add:

```cpp
    // Reflect the current route map into the selectors (identity by default, or whatever KM/state set).
    const auto routeIds = itemIdsFromRoute (proc.getMidiRoute());
    for (int i = 0; i < 16; ++i)
        routeBox[(size_t) i].setSelectedId (routeIds[(size_t) i], juce::dontSendNotification);
```

(`dontSendNotification` prevents this programmatic set from re-triggering `gatherRouteFromBoxes`.)

- [ ] **Step 6: Grow the editor height** — in `refresh()`, change the size block so the editor is tall enough for the matrix section (adds 104 px):

```cpp
    const bool showId = idVisible();
    idLabel.setVisible (showId);
    idField.setVisible (showId);
    const int targetH = (showId ? 240 : 208) + 104;   // + MIDI matrix section
    if (getHeight() != targetH) setSize (360, targetH);
```

- [ ] **Step 7: Lay out the matrix section** — in `CustosEditor::resized()`, insert this block **immediately after** the `onTopRow` block's `r.removeFromTop (8);` and **before** the `// Test row 1` block:

```cpp
    // MIDI route matrix: header + two rows of 8 (input ch caption over an output selector).
    auto midiHdr = r.removeFromTop (20);
    midiLabel.setBounds (midiHdr.removeFromLeft (140));
    r.removeFromTop (4);
    for (int row = 0; row < 2; ++row)
    {
        auto capRow = r.removeFromTop (14);
        auto boxRow = r.removeFromTop (22);
        const int colW = capRow.getWidth() / 8;
        for (int col = 0; col < 8; ++col)
        {
            const int ch = row * 8 + col;
            routeChanLabel[(size_t) ch].setBounds (capRow.removeFromLeft (colW).reduced (1, 0));
            routeBox[(size_t) ch].setBounds       (boxRow.removeFromLeft (colW).reduced (1, 0));
        }
        r.removeFromTop (4);
    }
```

- [ ] **Step 8: Build + verify** — the change is GUI glue (logic is unit-tested in Task 1). Verify the whole suite still builds and passes (the existing `EditorTest` constructs the editor, so a construction/layout regression fails it):

Run: `cmd //c "C:\Users\marti\AppData\Local\Temp\claude\C--dev\e7d0bc10-3576-48cc-8b73-fa1f0687b13e\scratchpad\custos-bt.cmd"`
Expected: PASS — full suite green, including `EditorTest`. Confirm no compile warnings from the new code.

- [ ] **Step 9: Commit**

```bash
git add src/CustosEditor.h src/CustosEditor.cpp
git commit -m "Editor matrix: 16-channel MIDI route/mute strip wired to setMidiRoute/getMidiRoute"
```

---

## Self-Review

**Spec coverage:**
- §3 route model reused verbatim → Task 1 helper maps to `0..16`, mute `0` ✅
- §4 UI: 16 per-channel selectors (M/1..16), identity default, 2 rows of 8, onChange→setMidiRoute, refresh→getMidiRoute, editor grows → Task 2 Steps 3/5/6/7 ✅
- §5 KM-master / no feedback emit → no `emitMidiRoute` anywhere in the plan ✅
- §6 testing: pure mapping helper extracted + unit-tested (Task 1); GUI verified via build + EditorTest (Task 2) ✅
- Non-goals (KM UI, feedback, live auto-refresh, model change) → none present ✅

**Placeholder scan:** No TBD/TODO; every code step shows complete code; build commands are exact.

**Type consistency:** `routeFromItemIds`/`itemIdsFromRoute` take/return `std::array<int,16>` in both tasks; `routeBox`/`routeChanLabel` are `std::array<...,16>`; ComboBox item-id convention (id1=M, id2..17=1..16) matches the helper's `itemId-1`/`+1` in both the populate step and the mapping header. `setMidiRoute`/`getMidiRoute` signatures match the v2 branch. `targetH` base values (240/208) match the current `refresh()` code.
