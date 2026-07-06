# Custos Multi-Bus Audio Facade — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the hosted inner synth a correctly-sized internal audio buffer (fixes the multi-output-synth `memcpy` crash, e.g. KORG M1) and present a fixed **1 stereo in / 5 stereo out** audio facade, plus a local "Main L/R only" toggle; bundle two small editor fixes (empty picker placeholder; "Open"=titled window / "Open fixed"=borderless).

**Architecture:** A pure `AudioBusMapper` folds the inner's N output channels into Custos's fixed 5 stereo output buses (default = pair→bus, pairs ≥5 summed into bus 5; "Main L/R only" = sum all into bus 1). `CustosProcessor` declares the fixed bus layout, keeps a scratch buffer sized to the inner's real channel count, and runs the inner into it. The toggle persists in Custos state v4. Editor gains the toggle, an empty picker placeholder, and a titled-window "Open" path alongside the borderless "Open fixed".

**Tech Stack:** C++17, JUCE 8.0.14, Catch2 v3.15.1. Branch `custos-multibus-audio` (off `custos-editor-midi-matrix`; design: `docs/superpowers/specs/2026-07-06-custos-multibus-audio-design.md`).

## Global Constraints

- **External bus layout (FIXED, ctor):** 1× stereo **input**, 5× stereo **output**. `isBusesLayoutSupported` accepts exactly this.
- **Inner safety:** the inner is always run into a scratch buffer sized `max(inner totalIn, inner totalOut)` — never Custos's own 10-ch buffer directly, never a forced-down count.
- **Output mapping:** default → inner pair `k`→out-bus `k` for k=1..4, pairs ≥5 **summed** into out-bus 5; **Main L/R only** → sum ALL inner output channels into out-bus 1, buses 2..5 silent. Mono/lone channel → duplicated to L+R of its target bus.
- **"Main L/R only" is LOCAL only** — editor toggle + persisted in state; **no OSC** verb, no OSC feedback.
- **State:** bump `StateCodec` to **v4** (append 1 byte `mainLROnly` after the v3 route block); v1/v2/v3 blobs parse with `mainLROnly=false`.
- **Repo language:** English (code, comments, commits).
- **Build/test (cmake/ninja/cl NOT on PATH):** from Bash, `cmd //c "C:\Users\marti\AppData\Local\Temp\claude\C--dev\e7d0bc10-3576-48cc-8b73-fa1f0687b13e\scratchpad\custos-bt.cmd" "<catch2-filter>"` builds+runs `custos_tests`; no filter = whole suite. Do NOT use plain `cmake`/`ctest`.

---

### Task 1: Pure audio bus mapper

**Files:**
- Create: `src/AudioBusMapper.h`
- Create: `tests/AudioBusMapperTest.cpp`
- Modify: `tests/CMakeLists.txt` (add the test file)

**Interfaces:**
- Produces: `void custos::mapInnerToOutputs (const juce::AudioBuffer<float>& inner, juce::AudioBuffer<float>& out, bool mainLROnly)` — `out` MUST have ≥10 channels and the same number of samples as `inner`; fills out channels 0..9 (5 stereo buses), clears unused ones.

- [ ] **Step 1: Write the failing test** — `tests/AudioBusMapperTest.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "AudioBusMapper.h"
#include <juce_audio_basics/juce_audio_basics.h>

using namespace custos;

// Fill each channel c with the constant value (c+1) so we can trace where it lands.
static juce::AudioBuffer<float> ramp (int chans, int n = 4)
{
    juce::AudioBuffer<float> b (chans, n);
    for (int c = 0; c < chans; ++c) b.clear (c, 0, n), juce::FloatVectorOperations::add (b.getWritePointer (c), (float) (c + 1), n);
    return b;
}
static float v (const juce::AudioBuffer<float>& b, int ch) { return b.getSample (ch, 0); }

TEST_CASE ("default mapping: pairs 1..4 -> buses 1..4, pairs >=5 summed into bus 5")
{
    auto inner = ramp (12);                 // 6 stereo pairs: ch values 1..12
    juce::AudioBuffer<float> out (10, 4);
    mapInnerToOutputs (inner, out, false);
    REQUIRE (v (out, 0) == 1.0f);  REQUIRE (v (out, 1) == 2.0f);   // bus1 = pair1
    REQUIRE (v (out, 6) == 7.0f);  REQUIRE (v (out, 7) == 8.0f);   // bus4 = pair4
    // bus5 = pair5 + pair6 = (9+11, 10+12)
    REQUIRE (v (out, 8) == 20.0f); REQUIRE (v (out, 9) == 22.0f);
}

TEST_CASE ("default mapping: fewer than 5 pairs leaves upper buses silent")
{
    auto inner = ramp (2);                  // just the main pair
    juce::AudioBuffer<float> out (10, 4);
    mapInnerToOutputs (inner, out, false);
    REQUIRE (v (out, 0) == 1.0f); REQUIRE (v (out, 1) == 2.0f);
    for (int c = 2; c < 10; ++c) REQUIRE (v (out, c) == 0.0f);
}

TEST_CASE ("Main L/R only: all channels summed into bus 1, buses 2..5 silent")
{
    auto inner = ramp (6);                  // ch 1..6
    juce::AudioBuffer<float> out (10, 4);
    mapInnerToOutputs (inner, out, true);
    REQUIRE (v (out, 0) == 1.0f + 3.0f + 5.0f);   // all L channels
    REQUIRE (v (out, 1) == 2.0f + 4.0f + 6.0f);   // all R channels
    for (int c = 2; c < 10; ++c) REQUIRE (v (out, c) == 0.0f);
}

TEST_CASE ("mono lone channel duplicated to L+R of its bus")
{
    auto inner = ramp (1);                  // single channel value 1
    juce::AudioBuffer<float> out (10, 4);
    mapInnerToOutputs (inner, out, false);
    REQUIRE (v (out, 0) == 1.0f); REQUIRE (v (out, 1) == 1.0f);
}

TEST_CASE ("empty inner -> all buses silent")
{
    juce::AudioBuffer<float> inner (0, 4), out (10, 4);
    mapInnerToOutputs (inner, out, false);
    for (int c = 0; c < 10; ++c) REQUIRE (v (out, c) == 0.0f);
}
```

- [ ] **Step 2: Register the test** — in `tests/CMakeLists.txt`, add `AudioBusMapperTest.cpp` to the `add_executable(custos_tests …)` source list (next to `MidiRouteMatrixTest.cpp`).

- [ ] **Step 3: Run to verify it fails** — `cmd //c "…\custos-bt.cmd" "*mapping*,*Main L/R*,*mono*,*empty inner*"` → FAIL (`AudioBusMapper.h` not found).

- [ ] **Step 4: Write the header** — `src/AudioBusMapper.h`:

```cpp
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace custos
{
// Fold the inner synth's output channels into Custos's fixed 5 stereo output buses (out ch 0..9).
// Default: inner pair k -> bus k (k=1..4); pairs >=5 summed into bus 5. mainLROnly: sum all -> bus 1.
// A lone/mono trailing channel is duplicated to L+R of its target bus. `out` must have >=10 channels.
inline void mapInnerToOutputs (const juce::AudioBuffer<float>& inner,
                               juce::AudioBuffer<float>& out, bool mainLROnly)
{
    const int n  = out.getNumSamples();
    const int ic = inner.getNumChannels();
    out.clear();

    auto addPair = [&] (int bus, int lSrc, int rSrc)
    {
        if (lSrc >= 0 && lSrc < ic) out.addFrom (bus * 2,     0, inner, lSrc, 0, n);
        if (rSrc >= 0 && rSrc < ic) out.addFrom (bus * 2 + 1, 0, inner, rSrc, 0, n);
    };

    if (mainLROnly)
    {
        for (int c = 0; c < ic; ++c) out.addFrom (c % 2, 0, inner, c, 0, n);   // L->0, R->1, alternating
        return;
    }

    for (int pair = 0; pair * 2 < ic; ++pair)
    {
        const int l = pair * 2, r = pair * 2 + 1;
        const int bus = pair < 4 ? pair : 4;                 // pairs >=5 fold into bus 5 (index 4)
        const int rSrc = (r < ic) ? r : l;                   // mono -> duplicate L to R
        addPair (bus, l, rSrc);
    }
}
}
```

- [ ] **Step 5: Run to verify it passes** — same filter as Step 3 → PASS (5 cases). Then whole suite (no filter) → all green.

- [ ] **Step 6: Commit**

```bash
git add src/AudioBusMapper.h tests/AudioBusMapperTest.cpp tests/CMakeLists.txt
git commit -m "Multi-bus: pure inner->5-stereo-bus mapper (default fold + Main-L/R-only)"
```

---

### Task 2: StateCodec v4 — persist the mainLROnly flag

**Files:**
- Modify: `src/StateCodec.h` (add `bool mainLROnly` to `PersistedState`; new `serializeState` param)
- Modify: `src/StateCodec.cpp` (write v4 + read the byte)
- Modify: `tests/StateCodecTest.cpp` (round-trip + back-compat)

**Interfaces:**
- Consumes: nothing new.
- Produces: `PersistedState.mainLROnly` (bool, default false); `serializeState (path, innerState, identityN, route, bool mainLROnly)`.

- [ ] **Step 1: Write the failing test** — append to `tests/StateCodecTest.cpp`:

```cpp
TEST_CASE ("state v4 round-trips mainLROnly")
{
    std::array<std::uint8_t,16> route {}; for (int i=0;i<16;++i) route[(size_t)i]=(std::uint8_t)(i+1);
    juce::MemoryBlock inner; inner.append ("abc", 3);
    auto blob = custos::serializeState ("C:/x.vst3", inner, 7, route, true);
    custos::PersistedState ps;
    REQUIRE (custos::parseState (blob.getData(), (int) blob.getSize(), ps));
    REQUIRE (ps.mainLROnly == true);
    REQUIRE (ps.identityN == 7);
}

TEST_CASE ("pre-v4 blob parses with mainLROnly defaulting to false")
{
    std::array<std::uint8_t,16> route {}; for (int i=0;i<16;++i) route[(size_t)i]=(std::uint8_t)(i+1);
    // v4 writer with false must equal behaviour; and a v3-style blob (no flag byte) -> false.
    auto blob = custos::serializeState ("C:/x.vst3", {}, 3, route, false);
    custos::PersistedState ps;
    REQUIRE (custos::parseState (blob.getData(), (int) blob.getSize(), ps));
    REQUIRE (ps.mainLROnly == false);
}
```

- [ ] **Step 2: Run to verify it fails** — `cmd //c "…\custos-bt.cmd" "*mainLROnly*,*pre-v4*"` → FAIL (compile: `serializeState` arity / `mainLROnly` missing).

- [ ] **Step 3: Update `src/StateCodec.h`** — add the field + new param:

```cpp
    juce::String path; juce::MemoryBlock innerState; int identityN = 0;
    std::array<std::uint8_t, 16> route { {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16} };
    bool mainLROnly = false;
```
and update the `serializeState` declaration to take `bool mainLROnly` as a trailing parameter.

- [ ] **Step 4: Update `src/StateCodec.cpp`** — in `serializeState`, bump the version byte to `4` and append the flag after the route:

```cpp
    os.writeByte (4);                       // was 3
    …                                       // (path, innerState, identityN, route bytes unchanged)
    os.write (route.data(), route.size());
    os.writeByte (mainLROnly ? 1 : 0);      // v4: +1 byte
```
and in `parseState`, accept version 4 and read the flag when present:

```cpp
    if (version < 1 || version > 4) return false;   // was: != 1 && != 2 && != 3
    …
    if (version >= 4)
    {
        if (is.getNumBytesRemaining() < 1) return false;
        out.mainLROnly = is.readByte() != 0;
    }
```

- [ ] **Step 5: Run to verify it passes** — same filter → PASS; then whole suite (existing StateCodec v1/v2/v3 tests must stay green — update any call site of `serializeState` in tests/impl to pass the new arg).

- [ ] **Step 6: Commit**

```bash
git add src/StateCodec.h src/StateCodec.cpp tests/StateCodecTest.cpp
git commit -m "State v4: persist mainLROnly flag (v1-3 back-compat -> false)"
```

---

### Task 3: Processor — fixed bus layout, scratch buffer, processBlock mapping, mainLROnly member

**Files:**
- Modify: `src/CustosProcessor.h` (members: `mainLROnly` atomic, `innerScratch` buffer, getters/setters; include `<atomic>`)
- Modify: `src/CustosProcessor.cpp` (ctor bus layout, `isBusesLayoutSupported`, `prepareToPlay`/`loadInner` scratch sizing, `processBlock` mapping, state get/set wiring)

**Interfaces:**
- Consumes: `mapInnerToOutputs` (Task 1); `PersistedState.mainLROnly` + `serializeState(…, bool)` (Task 2).
- Produces: `void setMainLROnly (bool)`, `bool mainLROnly() const`.

- [ ] **Step 1: ctor bus layout** — `src/CustosProcessor.cpp:15-16`, replace the `BusesProperties()` with 1 stereo in + 5 stereo out:

```cpp
    : juce::AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Out 1",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Out 2",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Out 3",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Out 4",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Out 5",  juce::AudioChannelSet::stereo(), true))
```

- [ ] **Step 2: `isBusesLayoutSupported`** — replace the body (`CustosProcessor.cpp:241-245`) to accept the fixed layout (input stereo-or-disabled; all 5 outputs stereo):

```cpp
bool CustosProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    if (! (in.isDisabled() || in == juce::AudioChannelSet::stereo())) return false;
    for (int b = 0; b < layouts.outputBuses.size(); ++b)
        if (layouts.outputBuses.getReference (b) != juce::AudioChannelSet::stereo()) return false;
    return layouts.outputBuses.size() == 5;
}
```

- [ ] **Step 3: members** — in `src/CustosProcessor.h` add `#include <atomic>` (if absent) and, in `private:`:

```cpp
    std::atomic<bool>        mainLROnlyFlag { false };
    juce::AudioBuffer<float> innerScratch;      // sized to the inner's real channel count (prepare-time)
```
and in `public:`:
```cpp
    void setMainLROnly (bool on) noexcept { mainLROnlyFlag.store (on); }
    bool mainLROnly() const noexcept       { return mainLROnlyFlag.load(); }
```

- [ ] **Step 4: size the scratch on prepare/load** — add a private helper and call it wherever the inner or play-config changes. In `src/CustosProcessor.cpp`, add:

```cpp
void CustosProcessor::resizeInnerScratch()
{
    const int ch = (inner != nullptr)
        ? juce::jmax (inner->getTotalNumInputChannels(), inner->getTotalNumOutputChannels(), 2)
        : 2;
    innerScratch.setSize (ch, preparedBlockSize > 0 ? preparedBlockSize : 512, false, false, true);
}
```
Change `prepareToPlay` (`:186-198`) so it prepares the inner with ITS OWN layout (do NOT force `(0, 2)`), then sizes the scratch:

```cpp
    if (inner != nullptr)
    {
        inner->setRateAndBufferSizeDetails (sampleRate, samplesPerBlock);
        inner->prepareToPlay (sampleRate, samplesPerBlock);
    }
    resizeInnerScratch();
```
and in `loadInner` (`:61-65`) replace the pre-lock prepare with the inner's own config + rescale scratch after binding:

```cpp
    if (newInner != nullptr && isPrepared)
    {
        newInner->setRateAndBufferSizeDetails (preparedSampleRate, preparedBlockSize);
        newInner->prepareToPlay (preparedSampleRate, preparedBlockSize);
    }
```
and after the lock block (after `boundCount = …`), add `resizeInnerScratch();`.

- [ ] **Step 5: processBlock mapping** — replace the inner-call block in `processBlock` (`:225-239`) so the inner runs into the scratch and is folded to the 5 buses:

```cpp
void CustosProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    std::array<std::uint8_t, 16> snap {};
    for (int i = 0; i < 16; ++i) snap[(size_t) i] = midiRoute[(size_t) i].load (std::memory_order_relaxed);
    applyMidiRoute (midi, snap, routeScratch);

    const int n = buffer.getNumSamples();
    {
        const juce::SpinLock::ScopedTryLockType tl (swapLock);
        if (tl.isLocked() && inner != nullptr)
        {
            if (innerScratch.getNumSamples() < n) innerScratch.setSize (innerScratch.getNumChannels(), n, false, false, true);
            innerScratch.clear();
            // Feed Custos input bus (ch 0/1) into the inner's first stereo pair.
            for (int c = 0; c < juce::jmin (2, innerScratch.getNumChannels(), buffer.getNumChannels()); ++c)
                innerScratch.copyFrom (c, 0, buffer, c, 0, n);
            juce::AudioBuffer<float> innerView (innerScratch.getArrayOfWritePointers(), innerScratch.getNumChannels(), n);
            inner->processBlock (innerView, midi);
            mapInnerToOutputs (innerView, buffer, mainLROnlyFlag.load());
        }
        else
        {
            buffer.clear();
        }
    }
    buffer.applyGain (masterGain.load());
}
```
Add `#include "AudioBusMapper.h"` to the top of `CustosProcessor.cpp`.

- [ ] **Step 6: state wiring** — in `getStateInformation` (`:335-343`) pass the flag; in `setStateInformation` (`:345-367`) apply it:

```cpp
    dest = serializeState (currentSynthPath, innerChunk, identityN, route, mainLROnlyFlag.load());
```
and in `setStateInformation`, after `identityN = ps.identityN;`:
```cpp
    mainLROnlyFlag.store (ps.mainLROnly);
```

- [ ] **Step 7: Build + verify whole suite** — `cmd //c "…\custos-bt.cmd"` → PASS (existing `PassthroughTest`, `EditorTest`, etc. still green; a stereo passthrough now goes through the scratch+mapper but the main pair is unchanged). Confirm no new warnings.

- [ ] **Step 8: Commit**

```bash
git add src/CustosProcessor.h src/CustosProcessor.cpp
git commit -m "Multi-bus: 1 stereo in / 5 stereo out, inner runs into a right-sized scratch (fixes multi-out crash)"
```

---

### Task 4: Editor — "Main L/R only" toggle (local)

**Files:**
- Modify: `src/CustosEditor.h` (a `juce::ToggleButton mainLR`)
- Modify: `src/CustosEditor.cpp` (construct/wire, refresh sync, layout)

**Interfaces:**
- Consumes: `proc.setMainLROnly(bool)` / `proc.mainLROnly()` (Task 3).

- [ ] **Step 1: member** — in `src/CustosEditor.h` add near the other controls: `juce::ToggleButton mainLR { "Main L/R only" };`

- [ ] **Step 2: construct + wire** — in the `CustosEditor` ctor (before the final `refresh();`):

```cpp
    mainLR.onClick = [this] { proc.setMainLROnly (mainLR.getToggleState()); };
    addAndMakeVisible (mainLR);
```

- [ ] **Step 3: sync in refresh()** — in `CustosEditor::refresh()`, before the size block:

```cpp
    mainLR.setToggleState (proc.mainLROnly(), juce::dontSendNotification);
```

- [ ] **Step 4: layout + grow** — in `resized()`, add a row for `mainLR` just after the MIDI-matrix block (before `// Test row 1`):

```cpp
    mainLR.setBounds (r.removeFromTop (22).removeFromLeft (160));
    r.removeFromTop (6);
```
and add `28` to the `targetH` in `refresh()` (the `(showId ? 240 : 208) + 104` becomes `+ 132`).

- [ ] **Step 5: Build + verify** — `cmd //c "…\custos-bt.cmd"` → PASS (EditorTest constructs the editor). No warnings.

- [ ] **Step 6: Commit**

```bash
git add src/CustosEditor.h src/CustosEditor.cpp
git commit -m "Editor: local 'Main L/R only' toggle (drives setMainLROnly, no OSC)"
```

---

### Task 5: Editor — empty Instrument-picker placeholder

**Files:**
- Modify: `src/CustosEditor.cpp` (two `setTextWhenNothingSelected` sites)

- [ ] **Step 1: ctor site** — change `favPicker.setTextWhenNothingSelected ("Instrument…");` to `favPicker.setTextWhenNothingSelected (juce::String());`

- [ ] **Step 2: rebuildInstrumentList site** — change the no-synth branch
`favPicker.setTextWhenNothingSelected (proc.hasInnerSynth() ? proc.innerSynthName() : juce::String ("Instrument…"));`
to
`favPicker.setTextWhenNothingSelected (proc.hasInnerSynth() ? proc.innerSynthName() : juce::String());`

- [ ] **Step 3: Build + verify** — `cmd //c "…\custos-bt.cmd"` → PASS. (No garbled ellipsis; empty when nothing selected.)

- [ ] **Step 4: Commit**

```bash
git add src/CustosEditor.cpp
git commit -m "Editor: empty Instrument-picker placeholder (drop garbled ellipsis)"
```

---

### Task 6: "Open" = titled window, "Open fixed" = borderless

**Files:**
- Create: `src/TitledSynthWindow.h` (a `juce::DocumentWindow` hosting the inner editor, native title bar + close)
- Modify: `src/CustosProcessor.h` (a `std::unique_ptr<juce::DocumentWindow> titledWindow`; `showSynthWindowTitled()`)
- Modify: `src/CustosProcessor.cpp` (implement titled path; `toggleSynthWindow` → titled)

**Interfaces:**
- Consumes: `inner->createEditorAndMakeActive()` (existing).
- Produces: `void CustosProcessor::showSynthWindowTitled()`; borderless path unchanged (`setSynthWindowRect` / `showSynthWindow`).

- [ ] **Step 1: TitledSynthWindow** — `src/TitledSynthWindow.h`:

```cpp
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace custos
{
// A normal titled window (native title bar + close button) hosting the inner synth's editor.
class TitledSynthWindow : public juce::DocumentWindow
{
public:
    TitledSynthWindow (const juce::String& name, juce::Component* editor)
        : juce::DocumentWindow (name, juce::Colours::black, juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (true, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }
    std::function<void()> onCloseButton;
    void closeButtonPressed() override { if (onCloseButton) onCloseButton(); }
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TitledSynthWindow)
};
}
```

- [ ] **Step 2: processor member + include** — in `src/CustosProcessor.h`, add `std::unique_ptr<juce::DocumentWindow> titledWindow;` next to `synthWindow`; declare `void showSynthWindowTitled();`. Add `#include "TitledSynthWindow.h"` to `CustosProcessor.cpp`.

- [ ] **Step 3: implement titled path** — in `src/CustosProcessor.cpp`, add:

```cpp
void CustosProcessor::showSynthWindowTitled()
{
    if (inner == nullptr) return;
    if (titledWindow != nullptr) { titledWindow->toFront (true); return; }
    if (auto* ed = inner->createEditorAndMakeActive())
    {
        auto w = std::make_unique<TitledSynthWindow> (inner->getName(), ed);
        w->setAlwaysOnTop (onTopMode == OnTopInstrument);
        w->onCloseButton = [this] { titledWindow.reset(); refreshEditor(); };
        titledWindow = std::move (w);
    }
    refreshEditor();
}
```
Update `isSynthWindowVisible()` to also count the titled window; update `hideSynthWindow()` to reset BOTH windows; in `setOnTopMode` also apply to `titledWindow` if present.

- [ ] **Step 4: route the buttons** — `toggleSynthWindow()` (the "Open" button) opens/closes the **titled** window:

```cpp
void CustosProcessor::toggleSynthWindow()
{
    if (titledWindow != nullptr) { titledWindow.reset(); refreshEditor(); }
    else                          showSynthWindowTitled();
}
```
"Open fixed" (`setSynthWindowRect`) keeps using the borderless `SynthWindow` unchanged. movable/clamp already only feed `setSynthWindowRect`, so they affect only the borderless path — no change needed.

- [ ] **Step 5: Build + verify** — `cmd //c "…\custos-bt.cmd"` → PASS (EditorTest). Confirm no warnings.

- [ ] **Step 6: Commit**

```bash
git add src/TitledSynthWindow.h src/CustosProcessor.h src/CustosProcessor.cpp
git commit -m "Window: 'Open' opens a titled window; 'Open fixed' stays borderless (movable/clamp fixed-only)"
```

---

### Task 7: Deploy + real M1 boot test (proof)

- [ ] **Step 1: Release build all 6 rungs** — `cmd //c "C:\Users\marti\AppData\Local\Temp\claude\C--dev\e7d0bc10-3576-48cc-8b73-fa1f0687b13e\scratchpad\release-build.cmd"` then `…\release-vst3.cmd` → `RELEASE_BUILD_OK` / `VST3_BUILD_OK`.

- [ ] **Step 2: Deploy** — GP MUST be closed (else file lock). `cmd //c "C:\Users\marti\AppData\Local\Temp\claude\C--dev\f1bca76b-2c56-410a-a46f-c7cd0eeae1a8\scratchpad\release-deploy.cmd"` → `LADDER_DEPLOY_OK`.

- [ ] **Step 3: Boot the M1 gig** — open `VSTProbe.gig` (has M1 persisted in N=2) with GP:
`& "C:\Program Files\Gig Performer 5\GigPerformer5.exe" "C:\Users\marti\OneDrive\Keyboard\GigPerformer\VSTProbe.gig"`; wait ~30 s; verify GP process is alive (no crash). Expected: **M1 loads, no `memcpy` crash** → root cause fixed.

- [ ] **Step 4: (Manual, Martin)** confirm in the Custos UI: Instrument picker empty when nothing selected; "Open" gives a titled window; "Open fixed" borderless; "Main L/R only" toggle present; M1 audible on Out 1.

---

## Self-Review

**Spec coverage:** §3 bus layout → T3.1/T3.2; §4 scratch + mapping → T1 + T3.4/T3.5; §4 Main-L/R-only sum → T1 + T3; §5 local toggle + state v4 → T2 + T3.6 + T4; §8 picker placeholder → T5; §8 titled/borderless window → T6; §7 tests → T1/T2 unit + T7 M1 boot. ✅
**Placeholder scan:** every code step shows complete code; build commands exact. ✅
**Type consistency:** `mapInnerToOutputs(inner, out, bool)` identical in T1/T3; `serializeState(…, bool mainLROnly)` + `PersistedState.mainLROnly` identical in T2/T3; `setMainLROnly`/`mainLROnly` identical in T3/T4; `showSynthWindowTitled`/`titledWindow` identical in T6. ✅
