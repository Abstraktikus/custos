# F3/F6 Window Control — E2E results

Deployed build (empty-passthrough Custos.vst3) in GigPerformer5, gig `Probe.gig` with two Custos
instances: **N=8** (empty) and **N=9** (CS-80 V4, boundCount 2797). Driven over OSC from
`scratchpad/custosctl.py` (send to `127.0.0.1:9100+N`, listen on hub `:8000`).

## Verified autonomously (OSC level, no screen)

- **Verbs recognised** — `/custos/window show|hide` and `/custos/window/rect …` produce no
  `error unknown` ack → present in the deployed build.
- **Show + place** — `/custos/window show` then `/custos/window/rect 300 300 800 600 0`.
- **Position feedback (Custos → KM)** — the rect apply emits back on the hub:
  - sent `[300,300,800,600,0]` → `<< /custos/window/rect [9, 300, 300, 800, 600, 0]` (exact round-trip;
    main monitor at 100% DPI so physical == logical).
- **Monitor clamp (config phase)** — `/custos/window/rect 5000 3000 900 700 0 1` (far off-screen, clamp=1)
  → `<< /custos/window/rect [9, 540, 212, 900, 700, 0]`. Window constrained into the work area
  (~1440×912): size preserved (900×700), position shifted so the borders stay reachable. With clamp=0 the
  rect is applied as-is (live phase).
- **Hide** — `/custos/window hide` → window torn down (borderless, no title bar; closable only via
  OSC / editor).

## Operator-verified (visual; screen access declined for the agent)

Confirmed by Martin during the review:

- Borderless synth window — **OK**.
- `movable=1` → body-draggable — **OK**.
- Hide via editor (Open/Close button) — **OK**.
- Editor "Open fixed" + on-top (Custos / Instrument) — **OK**.

### Follow-up round (this build)

- Double-click "Instrument" label now **toggles** the window (was hide-only → felt dead when hidden).
- Editor x/y/w/h fields reflect the window's real physical rect, updated live while dragging.
- Operator-drag of the window emits `/custos/window/rect` (N-first) on mouse-up so KM can capture the
  chosen geometry — mechanism shares `emitWindowRect()` with the verified apply path above.
- `clamp` checkbox in the editor test row mirrors the OSC `clamp` arg.

## Notes

- **Per-synth resize vs scale**: `applyRect` resizes the editor to w/h if `isResizable()`, else scales via
  `AffineTransform`. Arturia editors carry their **own** internal zoom (private plugin state, not a VST
  param, not in the facade) which persists in the inner state chunk — initial oversize can hide the drag
  borders until the operator sets a sane zoom once; then it survives reloads. The `clamp` option keeps the
  window on-screen in the meantime.
- Favourites seeding over OSC (`/custos/favorites/*`) did **not** update `%APPDATA%\Custos\favorites.json`
  during this session (mtime unchanged) although the verbs are recognised and `/custos/load` works — likely
  an environment artifact (GP launched from a sandboxed shell); flagged for a separate look, orthogonal to
  window control.
