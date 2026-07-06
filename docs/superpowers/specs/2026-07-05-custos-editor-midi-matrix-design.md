# Custos Editor MIDI Route Matrix — Design

**Status:** design (2026-07-05). Small, Custos-only editor addition on top of the v2 route model.
Stacked on branch `custos-osc-contract-v2` (needs `setMidiRoute`/`getMidiRoute` + state v3).

## 1. Context & goal

Contract v2 gave Custos a persisted 16→16 MIDI channel route map (`setMidiRoute`/`getMidiRoute`,
applied in `processBlock`, persisted in state v3, driveable over OSC). There is **no way to touch it
from the plugin itself** — today only KM (over OSC) can set it.

Goal: a small **hands-on matrix in the Custos editor** so the operator can set routing/muting
**locally, for testing without an incoming signal or a running KM**. That's the whole ask.

**Framing (locked in brainstorming):**
- **Custos-only.** No KM code, no new OSC verb, no architecture change — strictly on top of what v2
  already built. KM remains the master driver.
- **No editor→OSC feedback.** Editing the matrix in the editor does **not** emit `/custos/midi/route`;
  it just calls the existing `setMidiRoute`. (Explicitly dropped in brainstorming — KM stays master
  and may overwrite via OSC at any time; last writer wins.)
- The editor shows the **same 16 parameters** the OSC route map carries.

## 2. Scope

**In:** a 16-channel route/mute control strip in `CustosEditor`, wired to the existing
`proc.setMidiRoute()` / `proc.getMidiRoute()`.

**Out (non-goals):** any KM UI; an editor→KM feedback emit; live auto-refresh when KM changes the
route while the editor is open (refresh-on-open/refresh is enough for a test convenience); any change
to the route *model*, persistence, or OSC contract.

## 3. Data model (unchanged — reused verbatim)

The route is a `std::array<int,16>`, index `i` = input MIDI channel `i+1`, value ∈ `0..16`:
`0` = **mute** (drop that input channel), `1..16` = **route** it to that output channel. Identity
(`route[i] = i+1`) is the default. The editor is a pure front-end over this — one control per input
channel whose value is exactly that `0..16`, so there is **no extra UI state** (mute and target live
in the single value; muting to `0` and back to a target is a normal re-pick, acceptable for a test
tool).

## 4. UI

A new **"MIDI"** section in the editor, below the "On top" row (before the dev-only window-test rows).

- **16 per-channel selectors**, one per input channel 1..16, laid out as **two rows of 8** to fit the
  360-px editor. Each selector is a `ComboBox` with items **`M`** (= mute, value `0`) and **`1`..`16`**
  (= route target). A small header/label marks the input channel number for each cell (e.g. the cell
  is captioned by its input channel `1..16`; the selected item is the output).
- **Default / display:** on `refresh()` each selector is set from `proc.getMidiRoute()` — identity by
  default (channel `i` shows `i`), or whatever KM/state last set. Muted channels show `M`.
- **On change:** the selector's `onChange` reads all 16 selectors into a `std::array<int,16>` and calls
  `proc.setMidiRoute(...)`. That applies immediately in `processBlock` and persists via state v3.
- **Editor size:** the section adds a fixed block of height; `refresh()`'s adaptive `setSize` grows the
  editor accordingly (the existing `targetH` calc gains the matrix block; width stays 360).

There is intentionally **one control per channel** (not a separate enable checkbox + target) because
the route value is a single `0..16` — this keeps the UI a faithful, stateless mirror of the model.
Exact pixels are a layout detail (operator said "Umsetzungsart egal"); the plan pins them.

## 5. Interaction with KM-as-master

KM drives the same `setMidiRoute` over OSC. There is no locking or arbitration: whoever writes last
wins. The editor reflects the current map only at `refresh()` time (open, or any editor refresh), so a
KM change made while the editor is open may show stale until the next refresh — acceptable for a test
convenience and explicitly agreed. No `emitMidiRoute` from the editor path.

## 6. Testing

The matrix is GUI glue over a pure model that already has unit tests (`setMidiRoute`/`getMidiRoute`
round-trip, clamp, apply, persist — all green from v2). New coverage:
- A small **pure mapping helper** (selector-item-id ↔ route value, and the 16-selector ↔
  `std::array<int,16>` gather) extracted so it is unit-testable without a live editor: `M`→`0`,
  `k`→`k`, and gathering 16 items yields the expected array; round-trips with a scatter (set selectors
  from an array) so identity and a mixed map both survive.
- Manual/screenshot verification that the section renders, the editor grows, and edits reach
  `setMidiRoute` (observed via the existing route unit path).

## 7. Open questions

None blocking. Layout exact px deferred to the plan. If the two-row-of-8 strip proves too cramped in
practice, widening the editor is a follow-up tweak, not a design change.
