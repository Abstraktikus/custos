# Custos Window Control — F3/F6 (Design)

**Date:** 2026-07-04
**Status:** Approved (brainstorming complete, ready for planning)
**Scope:** OSC control of the inner-synth window: show/hide + pixel-exact, DPI-correct geometry, plus
in-editor test controls. Phase-C feature F3/F6; the last piece of the Custos-2.0 core.

Builds on M2 (`SynthWindow` hosting the inner synth's editor) and the just-landed keep-on-top
(`OnTopMode`). Also the motivating case for Ficta (KM docks a plugin's own UI over a KM panel).

---

## 1. Window model

The inner-synth window becomes **borderless** — **no title bar, no close button**. It is owned by the
processor (message thread only, `std::unique_ptr`), hosts the inner synth's editor
(`createEditorAndMakeActive` + `setContentOwned`, as in M2), and is kept above other windows when
`OnTopMode == OnTopInstrument`. It is **closed only via OSC or the Custos editor** — never a title-bar X.
The M2 `SynthWindow` (a `DocumentWindow` with a native title bar + close button) is replaced by a
borderless top-level (a `DocumentWindow` with title-bar height 0 and no buttons, or an equivalent
desktop component). The weak-token-guarded deferred-close teardown from M2 is preserved.

`/custos/window` **never shows the Custos editor panel** (that window belongs to GP; Custos can't open
it). It only governs the synth window.

## 2. OSC contract (v1 amendment — replaces the `custos|plugin|both|hide` sketch)

| Address | Args | Effect |
|---|---|---|
| `/custos/window` | `mode:string` ∈ {`show`,`hide`} | show / hide the inner-synth window |
| `/custos/window/rect` | `x:int, y:int, w:int, h:int, movable:int` | position + size the synth window |

- **Coordinates are PHYSICAL screen pixels** (the actual desktop pixels of the target rect). Custos maps
  them into JUCE's logical desktop space with `juce::Displays::physicalToLogical(...)` (which finds the
  target display and applies its scale) before setting the window bounds. This is correct in principle
  across multi-monitor / mixed-DPI; v1 is **tested on the main monitor only**.
- **Size handling:** after `physicalToLogical` gives the logical `w/h`, Custos resizes the inner editor
  to it **if the editor is resizable**; otherwise it applies an `AffineTransform` scaling the editor to
  fit the logical `w/h` (**resize-if-possible, else scale**). The window is then placed at the logical
  `x/y`.
- **`movable`:** `1` → the user may drag the (borderless) window by its body (a `ComponentDragger`);
  `0` → the window is fixed in place.

## 3. In-editor test controls

So the window behaviour (borderless open, geometry, multi-monitor, various synths) is testable **without
KM**, the Custos editor gains a test affordance: **`x/y/w/h` fields + a `movable` toggle + an "Open
fixed" action** that opens the synth window borderless at that **physical** rect — running the **same
code path** as the OSC verbs. (These fields are session-only; see §5.)

- The **"Open"** button opens the synth window (borderless; brings it to front if already open).
- **Closing is a hidden feature: double-clicking the "Instrument" label** hides the synth window
  (mirrors the Brand-label → reveal-id-field pattern).
- The **keep-on-top selector** options are **off / Custos / Instrument** (the "Custos" item was labelled
  "This"; the enum value becomes `OnTopCustos`). "Custos" keeps the Custos editor window on top,
  "Instrument" the synth window.

## 4. Architecture / data flow

```
KM (physical px)  ──/custos/window/rect──▶ CustosProcessor::setSynthWindowRect(xywh physical, movable)
editor test fields ─(Open fixed)──────────▶ (same method)
   setSynthWindowRect:
     ensure the synth window exists (show if hidden)
     logical = Displays::physicalToLogical({x,y,w,h})
     if editor resizable -> editor.setSize(logical.w, logical.h)   else -> editor.setTransform(scale)
     window.setBounds(logical) ; window.setDraggable(movable)
KM ──/custos/window show|hide──▶ CustosProcessor::showSynthWindow() / hideSynthWindow()   (M2, borderless)
```

Units:
- **`SynthWindow`** — borderless top-level; hosts the editor; optional body-drag (movable); no title bar
  / close. One responsibility: present the inner editor as a borderless, positionable window.
- **`CustosProcessor`** — `setSynthWindowRect(...)` (mapping + resize/scale + place), plus the existing
  `show/hideSynthWindow`, `setOnTopMode`.
- **`CustosOscServer`** — `parseCommand` for `/custos/window` + `/custos/window/rect`; routes to the
  processor.
- **`CustosEditor`** — the test fields + "Open fixed".

## 5. Persistence

**Transient.** KM re-sends the window state (show + rect) when it drives the rig (e.g. on song load);
the editor test fields are session-only. **Nothing is stored in the VST state.** Keeps the state format
untouched and the window purely externally-driven.

## 6. Error handling

- `hide` / `rect` with no synth loaded → no-op (no window to place).
- Malformed OSC args → ignored (unknown), no crash.
- A rect off all displays → `physicalToLogical` still returns a rectangle; the window may land partly
  off-screen (acceptable; KM sends valid coords). No clamping in v1.
- Scaling a non-resizable editor uses an `AffineTransform`; mouse events remain correct (JUCE maps
  through the transform).

## 7. Testing

- **Unit (hermetic):** `parseCommand` for `window` (`show`/`hide`) and `window/rect` (5 ints incl.
  `movable`) — pure. A small helper for the resize-vs-scale decision if it can be isolated.
- **E2E (operator-driven, via the editor test controls):** open the synth window borderless at a given
  physical rect for a **resizable** synth (verify it resizes to `w/h`) and a **fixed-size** synth (verify
  it scales); toggle `movable`; check position on the main monitor. Drive the same via OSC
  (`/custos/window show`, `/custos/window/rect …`) to confirm parity. Load the full `VstDatabase.txt` as
  favourites into instances N=8 and N=9 to sample many synths.

## 8. GP-/KM-side handoff (cross-project)

- **KM:** `/custos/window <show|hide>`; `/custos/window/rect <x> <y> <w> <h> <movable>` with **physical
  screen pixels** (KM already knows its panel's physical screen rect). To dock a synth GUI over a KM
  panel: send `show` then the panel's physical rect (+ `movable=0` for a fixed dock). Contract file
  `docs/osc-contract.md` updated on this branch.

## 9. Decomposition (implementation tasks)

1. Borderless `SynthWindow` (drop title bar / close; optional body-drag).
2. `parseCommand` for `/custos/window` + `/custos/window/rect` (+ Command fields) — unit-tested.
3. `CustosProcessor::setSynthWindowRect` (physicalToLogical + resize/scale + place) + `show`/`hide`
   routing.
4. OSC server routes the two verbs.
5. Editor: test controls (`x/y/w/h` + `movable` + "Open fixed"); hidden close (double-click the
   "Instrument" label); rename the keep-on-top item "This" → "Custos" (enum `OnTopCustos`).
6. Autonomous/operator E2E (editor + OSC parity; VstDatabase favourites into N=8/9).
