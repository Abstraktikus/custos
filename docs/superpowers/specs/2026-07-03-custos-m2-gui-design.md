# Custos M2 — Synth GUI in a Custos-owned Floating Window (Design)

**Date:** 2026-07-03
**Status:** Approved (brainstorming complete, ready for planning)
**Milestone:** M2 (builds on M1; precedes M3 OSC control)

---

## 1. Purpose

Give Custos a **minimal own editor** so opening it in the host shows a small Custos panel
instead of the host's generic 5000-slider parameter view, and let the user open the **inner
synth's original GUI** in a **Custos-owned floating top-level window** via a button. This proves
the two hard pieces — hosting the inner synth's `AudioProcessorEditor` (its VST3 `IPlugView`) and
owning a decoupled window — and sets up M3, where the same window is shown/hidden/positioned over
OSC from Kapellmeister.

The button is the M2 stand-in for M3's OSC `show`/`hide`: no OSC exists yet in M2.

---

## 2. Scope

### In scope
- `CustosEditor`: a minimal `juce::AudioProcessorEditor` (name/logo + `Synth: <name>` status +
  a Show/Hide-Synth toggle button). `hasEditor()` becomes `true`.
- `SynthWindow`: a Custos-owned top-level window that hosts the inner synth's editor, sized to it,
  closable via its own title bar.
- Ownership of the window by the **processor** (not the editor), so it survives the host closing
  Custos's plugin panel.
- Lifecycle: open/hide/toggle; window-close detaches the view but keeps the synth playing;
  processor destruction tears the window down before the inner synth is destroyed.

### Out of scope (later milestones)
- OSC control of the window (show/hide/setRect/focus) — **M3**.
- Docking the window pixel-precise over a Kapellmeister panel — **M3** (window is a free OS-default
  floating window in M2).
- Glitch-free runtime synth swap and its view teardown — **M4**.
- State persistence — **M3**.
- No changes to the M1 facade/passthrough/loader/trace behavior.

---

## 3. Architecture

### 3.1 `CustosEditor` (`juce::AudioProcessorEditor`)
Minimal, message-thread UI. Shows: product name/logo, a status line `Synth: <innerName>` (or
`Synth: (none)` when no inner is loaded), and a single toggle button **Show Synth** /
**Hide Synth**. The button calls a processor method (§3.3); the button label reflects
`processor.isSynthWindowVisible()`. `CustosProcessor::hasEditor()` returns `true` and
`createEditor()` returns a `CustosEditor`. This intercepts the host's generic parameter view.

The editor holds **no** synth-window state itself — it is a thin view over the processor, so the
host destroying/recreating the editor panel never affects the synth window.

### 3.2 `SynthWindow` (Custos-owned top-level window)
A `juce::DocumentWindow` (or equivalent top-level `Component`) that hosts the inner synth's editor
obtained via `inner->createEditorAndMakeActive()`. It:
- sizes itself to the inner editor's initial bounds and follows the inner editor's resize requests
  (JUCE `AudioProcessorEditor` resize/constrainer callbacks);
- is closable via its own title bar → closing it **destroys the window and the hosted inner editor
  view, but leaves the inner synth loaded and playing** (identical to the Hide button);
- is a normal, closable window, **not** always-on-top; first-open position is the OS default.
  (Always-on-top and precise placement become OSC-driven in M3.)

**Hide == Close == destroy the window + its hosted view; Show == create it fresh.** There is never
a hidden, still-attached view lingering — this keeps the single-parent invariant trivially true and
avoids stale native windows. Recreating the editor on each Show is cheap and acceptable for M2.

Exactly **one** `SynthWindow` exists per Custos instance (the inner `IPlugView` can attach to only
one parent).

### 3.3 Ownership & control flow (the key decision)
The `SynthWindow` is owned by **`CustosProcessor`**, not by `CustosEditor`:

```
CustosProcessor
  ├─ std::unique_ptr<juce::AudioProcessor> inner        (M1)
  ├─ std::unique_ptr<SynthWindow>          synthWindow   (M2, message-thread only; null = hidden)
  ├─ void toggleSynthWindow()      // visible -> hide; hidden -> show
  ├─ void showSynthWindow()        // create the window fresh + host the inner editor; if it already
  │                                //   exists, just bring it to front
  ├─ void hideSynthWindow()        // destroy the window (+ its hosted view); synth keeps playing
  └─ bool isSynthWindowVisible() const   // == (synthWindow != nullptr)
```

`CustosEditor`'s button calls `processor.toggleSynthWindow()`. Because the window lives on the
processor, closing the host's plugin panel (which destroys `CustosEditor`) leaves the synth window
untouched — the decoupling from the host UI that the project wants.

All window create/show/hide/destroy happens on the **message thread** (button presses and plugin
lifecycle are message-thread). The audio thread never touches `synthWindow`.

### 3.4 Data flow
```
open Custos in host → host calls createEditor() → CustosEditor (name + status + button)
button "Show Synth"  → processor.toggleSynthWindow()
                     → SynthWindow created, hosts inner->createEditorAndMakeActive(), sized, shown
button "Hide Synth"  → processor.hideSynthWindow() → window+view destroyed, inner keeps playing
window title-bar X   → same as Hide (destroy window+view, synth keeps playing)
Custos destroyed     → processor dtor: synthWindow reset FIRST, then inner destroyed
```

---

## 4. Lifecycle & safety

- **One window per instance.** The toggle button shows when hidden and hides (destroys) when
  shown; `synthWindow == nullptr` is the single source of truth for visibility.
- **Single parent.** The inner editor is hosted only by `SynthWindow`, and only while it exists.
- **Close keeps the synth.** Closing the window (button or title bar) destroys the window and the
  hosted *view*, not the synth.
- **Destruction order (critical).** In `~CustosProcessor`, reset `synthWindow` **before** `inner`
  is destroyed, so the inner editor (child of the window) is gone before its owning processor is
  freed. No dangling view → inner pointer.
- **No runtime swap in M2.** The inner synth is still attached once at construction (M1); swap-time
  view teardown is M4. (Forward note from M1 review already tracks the M4 swap safety.)

---

## 5. Testing

- **Unit (headless, no real synth):**
  - `CustosProcessor::hasEditor() == true`; `createEditor()` returns non-null.
  - Synth-window visibility state machine: `isSynthWindowVisible()` starts false; `toggle`
    flips it; `show`/`hide` set it; toggling with no inner attached is a safe no-op.
    (Test the state/flag logic; do not instantiate real native views headlessly.)
- **E2E in Gig Performer (manual, then autonomous):** open Custos → minimal editor visible (not
  the generic slider view) → click **Show Synth** → CS-80 V4 GUI floats in its own window →
  play → close the window → synth keeps playing → reopen. Confirm closing GP's Custos panel does
  **not** close the synth window.
- **Autonomous later:** once the GP-side `/KM/Plugin/*` OSC handlers exist, use `PCount`/`PList`
  to confirm the facade unchanged (still 5000, real names on `0..innerCount-1`) — GUI work must
  not alter the parameter facade.

---

## 6. Milestones (M2 tasks)

- **T1:** `CustosEditor` — minimal editor (name + `Synth: <name>` status + toggle button);
  `hasEditor()==true`, `createEditor()` returns it; button wired to a processor toggle method
  (window creation stubbed). Unit test: hasEditor/createEditor + visibility flag.
- **T2:** `SynthWindow` — host `inner->createEditorAndMakeActive()` in a top-level window, size to it,
  follow resizes; processor owns it; `show/hide/toggle/isVisible` implemented; close detaches the
  view but keeps the synth.
- **T3:** lifecycle safety — processor destructor tears the window down before `inner`; status line
  reflects the inner name; button label reflects visibility. E2E verification in GP.

---

## 7. Relationship to neighbouring milestones

- **From M1:** consumes the existing `inner` (`std::unique_ptr<juce::AudioProcessor>`) and its
  editor. Does not touch the facade, passthrough, loader, or host-trace.
- **To M3:** `toggleSynthWindow`/`show`/`hide` + a future `setRect`/`focus` become the targets of
  the OSC geometry contract; the free-floating window becomes dockable over a Kapellmeister panel.
- **To M4:** the swap sequence must tear down/rebuild the hosted view under bypass; the destruction
  ordering established here is the foundation.
