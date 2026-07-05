# Custos Window Control Implementation Plan (Phase C, F3/F6)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let Kapellmeister show/hide and pixel-exactly position the inner-synth window over OSC (DPI-correct), with a borderless window and in-editor test controls so it's verifiable without KM.

**Architecture:** The synth window becomes a borderless top-level (`ResizableWindow`, no title bar/close). `/custos/window <show|hide>` and `/custos/window/rect <x y w h movable>` (physical px) route to the processor, which maps physical→logical with `juce::Displays::physicalToLogical` and asks the window to resize-or-scale its editor to fit and place itself. The editor grows test controls (x/y/w/h + movable + "Open fixed"), a hidden close (double-click "Instrument"), and renames the keep-on-top item "This"→"Custos".

**Tech Stack:** C++17, CMake, JUCE 8.0.14, Catch2 v3.15.1, VST3 (Windows). Builds on `master` (M2 window, addressing, param dump, volume, favourites, OnTopMode).

## Global Constraints

- **Spec:** `docs/superpowers/specs/2026-07-04-custos-window-control-design.md`. Contract v1 amendment.
- **OSC:** `/custos/window <mode:string>` (`show`|`hide`); `/custos/window/rect <x:int y:int w:int h:int movable:int>` — coordinates are **physical screen pixels**; `movable` 0/1.
- **Borderless:** the synth window has **no title bar, no close button**. Closed via OSC/editor only.
- **Coordinates:** map physical→logical via `juce::Desktop::getInstance().getDisplays().physicalToLogical(...)`. DPI-correct in principle; **tested on the main monitor only**.
- **Size:** resize the inner editor to the logical `w/h` if `editor->isResizable()`, else `setTransform` scale it to fit (**resize-if-possible, else scale**).
- **`movable`:** `1` → draggable by body (`ComponentDragger`); `0` → fixed.
- **Never shows the Custos panel** via OSC — only the synth window.
- **Transient:** nothing about the window is stored in the VST state.
- **Build the `Custos_VST3` target.** Build/test: `.\scripts\ci.cmd`.
- **UI/window geometry is not unit-testable** (needs a display); it is verified in the Task 6 operator E2E. Unit tests cover `parseCommand`.

---

## File Structure

```
src/
  SynthWindow.h/.cpp     # MODIFY: borderless (ResizableWindow); movable body-drag; applyRect(); drop title/close/onClose
  CustosOscServer.h      # MODIFY: Command gains WindowShow/WindowHide/WindowRect + rx/ry/rw/rh/movable
  CustosOscServer.cpp    # MODIFY: parseCommand maps /custos/window + /custos/window/rect; route to processor
  CustosProcessor.h/.cpp # MODIFY: showSynthWindow uses borderless SynthWindow; setSynthWindowRect(); OnTopThis->OnTopCustos
  CustosEditor.h/.cpp    # MODIFY: test controls (x/y/w/h + movable + Open fixed); hidden close (dbl-click Instrument); on-top "Custos"
tests/
  OscContractTest.cpp    # MODIFY: parseCommand(window / window/rect) tests
```

---

## Task 1: Borderless SynthWindow with movable drag + applyRect

**Files:**
- Modify: `src/SynthWindow.h`, `src/SynthWindow.cpp`

**Interfaces:**
- Produces: `custos::SynthWindow` is now a borderless `juce::ResizableWindow` hosting the inner editor;
  `SynthWindow (juce::Component* editor)`; `void setDraggable (bool)`;
  `void applyRect (juce::Rectangle<int> logical, bool movable)` (resize-or-scale the editor to fit + set bounds + set draggable).

- [ ] **Step 1: Rewrite src/SynthWindow.h**

```cpp
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace custos
{
// A borderless top-level window hosting a hosted plugin's editor. No title bar, no close button;
// closed by the owner (CustosProcessor) via OSC / the editor. Optionally draggable by its body.
class SynthWindow : public juce::ResizableWindow
{
public:
    explicit SynthWindow (juce::Component* editor);   // takes ownership of the editor (content)

    void setDraggable (bool shouldBeDraggable) noexcept { draggable = shouldBeDraggable; }
    // Resize the editor to the logical size (if it is resizable) else scale it to fit; place the window.
    void applyRect (juce::Rectangle<int> logical, bool movable);

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    juce::ComponentDragger dragger;
    bool draggable = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthWindow)
};
}
```

- [ ] **Step 2: Rewrite src/SynthWindow.cpp**

```cpp
#include "SynthWindow.h"
#include <juce_audio_processors/juce_audio_processors.h>   // for AudioProcessorEditor (dynamic_cast in applyRect)

namespace custos
{
SynthWindow::SynthWindow (juce::Component* editor)
    : juce::ResizableWindow ("Custos Synth", juce::Colour (0xff2b2b2b), true)   // addToDesktop; no title bar
{
    setContentOwned (editor, true);   // window tracks the editor's native size until applyRect overrides it
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

void SynthWindow::applyRect (juce::Rectangle<int> logical, bool movable)
{
    if (auto* ed = dynamic_cast<juce::AudioProcessorEditor*> (getContentComponent()))
    {
        if (ed->isResizable())
        {
            ed->setTransform ({});                                    // clear any prior scale
            ed->setSize (logical.getWidth(), logical.getHeight());
        }
        else if (ed->getWidth() > 0 && ed->getHeight() > 0)
        {
            ed->setTransform (juce::AffineTransform::scale (
                (float) logical.getWidth()  / (float) ed->getWidth(),
                (float) logical.getHeight() / (float) ed->getHeight()));
        }
    }
    setBounds (logical);
    draggable = movable;
}

void SynthWindow::mouseDown (const juce::MouseEvent& e)
{
    if (draggable) dragger.startDraggingComponent (this, e);
}

void SynthWindow::mouseDrag (const juce::MouseEvent& e)
{
    if (draggable) dragger.dragComponent (this, e, nullptr);
}
}
```

- [ ] **Step 3: Build (compile check — no unit test; window is visual)**

Run (PowerShell): `.\scripts\build.cmd custos_tests Custos_VST3`
Expected: **This step will not fully build yet** — `CustosProcessor.cpp` still constructs the old
`SynthWindow(title, editor, onClose)` and calls `->setAlwaysOnTop` on it. That is fixed in Task 3;
Tasks 1 and 3 are compile-coupled. If running Task 1 alone, expect an error at the `SynthWindow` call
site; proceed to Task 3, then build.

- [ ] **Step 4: Commit**

```bash
git add src/SynthWindow.h src/SynthWindow.cpp
git commit -m "Window control: borderless SynthWindow (ResizableWindow) + movable drag + applyRect"
```

---

## Task 2: parseCommand for /custos/window and /custos/window/rect

**Files:**
- Modify: `src/CustosOscServer.h`, `src/CustosOscServer.cpp`, `tests/OscContractTest.cpp`

**Interfaces:**
- Produces: `Command` gains `WindowShow`, `WindowHide`, `WindowRect` and `int rx, ry, rw, rh; bool movable;`;
  `parseCommand` maps `/custos/window <show|hide>` and `/custos/window/rect <x y w h movable>` (5 ints).

- [ ] **Step 1: Write the failing tests — append to tests/OscContractTest.cpp**

```cpp
TEST_CASE ("parseCommand maps /custos/window show and hide")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/window", juce::String ("show"))).kind == Command::WindowShow);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/window", juce::String ("hide"))).kind == Command::WindowHide);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/window", juce::String ("bogus"))).kind == Command::Unknown);
}

TEST_CASE ("parseCommand maps /custos/window/rect with five ints")
{
    const auto c = parseCommand (juce::OSCMessage ("/custos/window/rect", 100, 200, 640, 480, 1));
    REQUIRE (c.kind == Command::WindowRect);
    REQUIRE (c.rx == 100);
    REQUIRE (c.ry == 200);
    REQUIRE (c.rw == 640);
    REQUIRE (c.rh == 480);
    REQUIRE (c.movable == true);

    REQUIRE (parseCommand (juce::OSCMessage ("/custos/window/rect", 1, 2, 3)).kind == Command::Unknown);
}
```

- [ ] **Step 2: Run to verify failure**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: FAIL — `Command::WindowShow/WindowHide/WindowRect`, `.rx`/etc. don't exist.

- [ ] **Step 3: Extend Command — src/CustosOscServer.h**

In the `Command` struct's `enum Kind`, add `WindowShow, WindowHide, WindowRect` (before `Unknown`), and add the fields after `Favorite fav;`:
```cpp
    int rx = 0, ry = 0, rw = 0, rh = 0;   // WindowRect (physical px)
    bool movable = false;                 // WindowRect
```

- [ ] **Step 4: Map the addresses — src/CustosOscServer.cpp**

In `parseCommand`, before the final `return { Command::Unknown, {} };`, add:
```cpp
    if (addr == "/custos/window")
    {
        if (msg.size() >= 1 && msg[0].isString())
        {
            const auto m = msg[0].getString();
            if (m == "show") return { Command::WindowShow, {} };
            if (m == "hide") return { Command::WindowHide, {} };
        }
        return { Command::Unknown, {} };
    }
    if (addr == "/custos/window/rect")
    {
        if (msg.size() >= 5 && msg[0].isInt32() && msg[1].isInt32() && msg[2].isInt32()
            && msg[3].isInt32() && msg[4].isInt32())
        {
            Command c;
            c.kind = Command::WindowRect;
            c.rx = msg[0].getInt32(); c.ry = msg[1].getInt32();
            c.rw = msg[2].getInt32(); c.rh = msg[3].getInt32();
            c.movable = msg[4].getInt32() != 0;
            return c;
        }
        return { Command::Unknown, {} };
    }
```

- [ ] **Step 5: Build + test**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: the two new tests PASS. (The full build still fails via the Task 1/3 coupling if Task 3 is
not yet done — the `custos_tests` target may build while the plugin does not. Do Task 3 next.)

- [ ] **Step 6: Commit**

```bash
git add src/CustosOscServer.h src/CustosOscServer.cpp tests/OscContractTest.cpp
git commit -m "Window control: parseCommand for /custos/window + /custos/window/rect"
```

---

## Task 3: Processor — borderless window, setSynthWindowRect, OnTopCustos rename

**Files:**
- Modify: `src/CustosProcessor.h`, `src/CustosProcessor.cpp`

**Interfaces:**
- Consumes: `SynthWindow(editor)` / `applyRect` (Task 1); `Displays::physicalToLogical`.
- Produces: `void CustosProcessor::setSynthWindowRect (int x, int y, int w, int h, bool movable);`
  `OnTopMode` value `OnTopThis` renamed to `OnTopCustos`. `showSynthWindow` builds the borderless window.

- [ ] **Step 1: Rename OnTopThis -> OnTopCustos — src/CustosProcessor.h**

Change:
```cpp
enum OnTopMode { OnTopOff, OnTopThis, OnTopInstrument };
```
to:
```cpp
enum OnTopMode { OnTopOff, OnTopCustos, OnTopInstrument };
```

- [ ] **Step 2: Update setOnTopMode's use of the renamed value — src/CustosProcessor.cpp**

In `setOnTopMode`, change `top->setAlwaysOnTop (mode == OnTopThis);` to
`top->setAlwaysOnTop (mode == OnTopCustos);`.

- [ ] **Step 3: Declare setSynthWindowRect — src/CustosProcessor.h**

After `setOnTopMode`/`getOnTopMode`, add:
```cpp
    // F3/F6: show (if needed) + place the synth window at a PHYSICAL-pixel rect (DPI-mapped). Message thread.
    void setSynthWindowRect (int x, int y, int w, int h, bool movable);
```

- [ ] **Step 4: Rebuild showSynthWindow for the borderless window — src/CustosProcessor.cpp**

Replace the `showSynthWindow` body's window construction. Change:
```cpp
    if (auto* ed = inner->createEditorAndMakeActive())        // null if the synth has no editor (JUCE 8 API)
    {
        // The window's close button defers this callback via the message queue; it can outlive
        // both the window and this processor. Guard with a weak token so a late dispatch after
        // ~CustosProcessor is a safe no-op (not a use-after-free).
        std::weak_ptr<bool> weak = aliveToken;
        synthWindow = std::make_unique<SynthWindow> (
            kProduct + juce::String (" - ") + inner->getName(),
            ed,
            [this, weak] { if (! weak.expired()) hideSynthWindow(); });
        synthWindow->setAlwaysOnTop (onTopMode == OnTopInstrument);
    }
```
with (borderless — no title/close callback needed; the window is torn down by hideSynthWindow only):
```cpp
    if (auto* ed = inner->createEditorAndMakeActive())        // null if the synth has no editor (JUCE 8 API)
    {
        synthWindow = std::make_unique<SynthWindow> (ed);
        synthWindow->setAlwaysOnTop (onTopMode == OnTopInstrument);
    }
```
(The `aliveToken` member stays — it still guards nothing here but is used elsewhere; leave it.)

- [ ] **Step 5: Define setSynthWindowRect — src/CustosProcessor.cpp**

Add (e.g. after `setOnTopMode`):
```cpp
void CustosProcessor::setSynthWindowRect (int x, int y, int w, int h, bool movable)
{
    if (synthWindow == nullptr) showSynthWindow();   // ensure it exists
    if (synthWindow == nullptr) return;              // no inner / editor-less synth

    const auto logical = juce::Desktop::getInstance().getDisplays()
                             .physicalToLogical (juce::Rectangle<int> (x, y, w, h));
    synthWindow->applyRect (logical, movable);
}
```

- [ ] **Step 6: Build + test**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: all tests PASS (Tasks 1–3 now compile together). Build pristine.

- [ ] **Step 7: Commit**

```bash
git add src/CustosProcessor.h src/CustosProcessor.cpp
git commit -m "Window control: borderless synth window + setSynthWindowRect (physical->logical) + OnTopCustos"
```

---

## Task 4: OSC server routes the window verbs

**Files:**
- Modify: `src/CustosOscServer.cpp`

**Interfaces:**
- Consumes: `parseCommand` (window kinds), `CustosProcessor::{showSynthWindow,hideSynthWindow,setSynthWindowRect}`.

- [ ] **Step 1: Handle the window verbs — src/CustosOscServer.cpp**

In `oscMessageReceived`, add cases before `case Command::Unknown:`:
```cpp
        case Command::WindowShow:
            proc.showSynthWindow();
            break;
        case Command::WindowHide:
            proc.hideSynthWindow();
            break;
        case Command::WindowRect:
            proc.setSynthWindowRect (cmd.rx, cmd.ry, cmd.rw, cmd.rh, cmd.movable);
            break;
```

- [ ] **Step 2: Build + test**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: all tests PASS (transport). Build pristine, `Custos.vst3` produced.

- [ ] **Step 3: Commit**

```bash
git add src/CustosOscServer.cpp
git commit -m "Window control: route /custos/window + /custos/window/rect"
```

---

## Task 5: Editor — test controls, hidden close, on-top "Custos"

**Files:**
- Modify: `src/CustosEditor.h`, `src/CustosEditor.cpp`

**Interfaces:**
- Consumes: `CustosProcessor::{setSynthWindowRect,hideSynthWindow,showSynthWindow}` (Tasks 1/3); `ClickableLabel`.

- [ ] **Step 1: Add members — src/CustosEditor.h**

Change `juce::Label instrLabel;` to `ClickableLabel instrLabel;` (double-click closes the synth window).
Add, after `openButton`:
```cpp
    juce::TextEditor testX, testY, testW, testH;   // physical rect for the "Open fixed" test
    juce::ToggleButton testMovable { "movable" };
    juce::TextButton   openFixedButton { "Open fixed" };
```

- [ ] **Step 2: Rename the on-top item + wire the hidden close — src/CustosEditor.cpp**

In the constructor, change `onTopBox.addItem ("This", 2);` to `onTopBox.addItem ("Custos", 2);`.
In the `instrLabel` setup, add the double-click close (it is now a `ClickableLabel`):
```cpp
    instrLabel.setInterceptsMouseClicks (true, false);
    instrLabel.onDoubleClick = [this] { proc.hideSynthWindow(); refresh(); };
```

- [ ] **Step 3: Wire the test controls — src/CustosEditor.cpp (constructor)**

After the `openButton` setup, add:
```cpp
    auto setupNum = [this] (juce::TextEditor& t, const juce::String& placeholder)
    {
        t.setInputRestrictions (5, "0123456789");
        t.setTextToShowWhenEmpty (placeholder, juce::Colours::grey);
        addAndMakeVisible (t);
    };
    setupNum (testX, "x"); setupNum (testY, "y"); setupNum (testW, "w"); setupNum (testH, "h");
    addAndMakeVisible (testMovable);
    openFixedButton.onClick = [this]
    {
        proc.setSynthWindowRect (testX.getText().getIntValue(), testY.getText().getIntValue(),
                                 testW.getText().getIntValue(), testH.getText().getIntValue(),
                                 testMovable.getToggleState());
        refresh();
    };
    addAndMakeVisible (openFixedButton);
```

- [ ] **Step 4: Lay out the test row + grow the window — src/CustosEditor.cpp (resized + refresh)**

In `resized()`, before the id row, add a test row:
```cpp
    auto testRow = r.removeFromTop (24);
    testX.setBounds (testRow.removeFromLeft (44)); testRow.removeFromLeft (4);
    testY.setBounds (testRow.removeFromLeft (44)); testRow.removeFromLeft (4);
    testW.setBounds (testRow.removeFromLeft (44)); testRow.removeFromLeft (4);
    testH.setBounds (testRow.removeFromLeft (44)); testRow.removeFromLeft (8);
    testMovable.setBounds (testRow.removeFromLeft (78));
    openFixedButton.setBounds (testRow.removeFromRight (84));
    r.removeFromTop (8);
```
In `refresh()`, bump the two `targetH` values by 32 to make room: change `showId ? 178 : 146` to
`showId ? 210 : 178`.

- [ ] **Step 5: Build + test**

Run (PowerShell): `.\scripts\ci.cmd`
Expected: all tests PASS (the `EditorTest` still constructs a non-null editor). Build pristine.

- [ ] **Step 6: Commit**

```bash
git add src/CustosEditor.h src/CustosEditor.cpp
git commit -m "Window control: editor test controls + hidden close (dbl-click Instrument) + on-top Custos"
```

---

## Task 6: Autonomous / operator E2E

**Goal:** verify show/hide + geometry (resize + scale) + movable, editor and OSC parity. Record in
`experiments/window.md`.

- [ ] **Step 1: Deploy + seed many synths**

`.\scripts\deploy.cmd` → restart GP. Read `VstDatabase.txt` (KM's list; from the rackspaceDir/Snapshots
location) and push every entry as a favourite (name/path/favOrder/gainDb/brand) to **N=8 and N=9** via
`/custos/favorites/*`, so the pickers offer a wide range of synths.

- [ ] **Step 2: OSC show/hide + rect (parity with the editor)**

To `:9109`: `/custos/window show` → the synth window appears borderless. `/custos/window/rect 200 200 700 500 0`
→ it moves to that physical rect (on the main monitor). For a **resizable** synth confirm it resized to
700×500; for a **fixed-size** synth confirm it scaled to fill. `/custos/window/rect … 1` → the window is
draggable by its body. `/custos/window hide` → it disappears.

- [ ] **Step 3: Editor test controls**

In the Custos editor, type a rect into x/y/w/h, toggle `movable`, click **Open fixed** → the window opens
borderless at that rect (same behaviour as OSC). **Double-click the "Instrument" label** → the window
closes. Set on-top to **Custos** (Custos window stays on top) and **Instrument** (synth window stays on top).

- [ ] **Step 4: Record + commit**

Write `experiments/window.md` with the observed behaviour (per-synth: resized vs scaled, position), then:
```bash
git add experiments/window.md
git commit -m "Window control: E2E results (show/hide, rect resize+scale, movable, hidden close)"
```

---

## Self-Review

**Spec coverage (design §1–§8):**
- Borderless window, no title/close → Task 1 (`ResizableWindow`). ✓
- `/custos/window <show|hide>` + `/custos/window/rect <x y w h movable>` physical px → Task 2 (parse) + Task 4 (route). ✓
- physical→logical (`physicalToLogical`) → Task 3 (`setSynthWindowRect`). ✓
- resize-if-resizable else scale → Task 1 (`applyRect`). ✓
- movable body-drag → Task 1 (`ComponentDragger`). ✓
- Never shows the Custos panel via OSC → Task 4 (only show/hide/rect the synth window). ✓
- In-editor test controls + hidden close (dbl-click Instrument) + on-top "Custos" → Task 5. ✓
- Transient (no VST-state change) → nothing touches `StateCodec`. ✓

**Placeholder scan:** every code step is complete. The UI/geometry that can't be unit-tested is explicitly
verified in Task 6 (operator E2E) — stated, not a placeholder.

**Type consistency:** `SynthWindow(editor)` / `applyRect(Rectangle,bool)` (Task 1) are used in Task 3.
`Command::{WindowShow,WindowHide,WindowRect}` + `rx/ry/rw/rh/movable` (Task 2) are consumed in Task 4.
`setSynthWindowRect(int,int,int,int,bool)` (Task 3) is called in Tasks 4 and 5. `OnTopCustos` (Task 3)
replaces `OnTopThis` everywhere (`setOnTopMode`, the editor item). Tasks 1+3 are compile-coupled (the
`SynthWindow` ctor signature change) — build them together; called out in Task 1 Step 3.
