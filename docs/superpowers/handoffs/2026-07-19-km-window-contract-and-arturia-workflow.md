# KM handoff — window/rect contract deltas + Arturia dock workflow support

**From:** Custos session 2026-07-19 (evening; PRs #16/#19/#18 merged to master in that order, rig
runs the identical build since 19:26)
**To:** a Kapellmeister session (`C:\dev\kapellmeister`)
**Prior handoff still applies:** `2026-07-19-km-oversized-load-and-favorder.md` (slot-fit
authoring, full-record DB pushes, refusal ack).

---

Paste everything below into a fresh Kapellmeister session.

---

## Context

Custos master now contains (all live on the rig): DIP window coordinates, a dock-fit that
negotiates instead of fighting clamping editors (JD-800 pixel-perfect; stubborn editors —
verified for all Arturias — end centre-cropped with the GUI middle visible), a facade-guard
oversize tolerance, boot DB-read retry, and explicit empty-browse feedback. Contract:
`docs/osc-contract.md` in the custos repo (authoritative).

## Contract deltas KM must adopt

1. **`/custos/window/rect` units are DIPs in BOTH directions.** KM already sends DIPs (KM
   PR #98). The FEEDBACK is now also raw DIPs — if KM's geometry-capture path still converts
   the echo from physical px, remove that conversion.
2. **The rect feedback gained two trailing args: `contentW, contentH`** — the hosted editor's
   ACHIEVED size in DIPs. `contentW/H > w/h` = the GUI is centre-cropped (plugin minimum
   exceeds the dock area). Use it for (a) an operator hint in the SYNTH view ("GUI 1280x626,
   Dock 746x278 — Mitte sichtbar") and (b) the auto-learning described under the Arturia
   workflow below.
3. **New acks to surface (all on the hub :8000; "error" acks also mirror to GP :54344):**
   - `warning instrument oversized (slots N > facade M, K params unbound)` — a load in the
     tolerated band went through; show as an authoring smell, do NOT block or retry.
   - `instrument DB loaded late: <n> entries (attempt <k>)` / `error instrument DB empty: <path>`
     — boot DB-read retry outcome (OneDrive hydration case).
   - `error instrument list empty` / `error no browsable instrument (scope <s>, facade <cap>)`
     — browse on an empty/effectively-empty set (was silent before).
   - `error instrument too large (slots N > facade M)` — unchanged, still the hard refusal.
4. **Guard semantics now:** `slots <= facadeCap + 10%` loads WITH the warning ack; beyond that
   it is refused. Live precedent inside the tolerance: Jup-8000 V (3058) and Memory V (3168)
   in their 3000 rungs. KM's authoring (Layer slot-fit, Parameter Measurement B2–B5) should
   still flag ANY oversize — the tolerance is an operator-accepted compromise, not license.

## Martin's Arturia dock workflow (user-level; KM support wanted)

Arturia editors ignore host sizing entirely (measured 2026-07-19: neither
`setContentScaleFactor` nor exact step multiples of natural size move them — the zoom steps
exist only inside the plugin's own UI, and Arturia persists that zoom per plugin). Martin's
plan as the operator:

> Alle Synths mit Fenster laden, in der Plugin-UI auf 50% Größe stellen (kleinstmöglich),
> dann bis zur Fenstergröße zoomen, Ränder merken, und die Größe des Hintergrunds anpassen.
> Einmal eingestellt, für lange Zeit ein Vergnügen.

So: the plugin's own persisted zoom makes its NATURAL size small enough to fit; Custos then
letterbox-centres it (or shows it 1:1 when it matches). What KM should provide:

- **Per-synth dock geometry in the DB** ("braucht natürlich DB-Einträge"): store each synth's
  preferred dock/content size (and margins) — suggested source: **auto-learn from the rect
  feedback's `contentW/contentH`** after each dock fit, rather than manual entry. A synth's
  achieved size IS its current zoom's natural size.
- **SYNTH-view background/area sized per synth** from those entries, so the dock matches the
  plugin's discrete size instead of a one-size area (Martin: "Ränder merken und die Größe des
  Hintergrunds anpassen").
- UX details are the KM session's call; the data flow (feedback → DB → per-synth dock area)
  is the requirement.

## Explicitly NOT needed Custos-side

- No `fitHint` per-vendor field: there are no Arturia step sizes to encode (measured), and the
  one futile scale attempt per fit is a harmless no-op. Centre-crop/letterbox is automatic.
