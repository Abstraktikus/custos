# Addressing Core E2E (2026-07-04)

Autonomous end-to-end verification of Phase-C Feature 1 (addressing core) against a live Gig
Performer 5, with the operator-set identity model (`N` typed in the Custos UI → bind `BASE+N`).

## Build / setup
- New addressing build, deployed **without** a hard-coded synth (production config — Custos boots empty
  and restores from state).
- Gig: `OneDrive\Keyboard\GigPerformer\Probe.gig`, which carried a **real 112 KB saved Custos state**
  (inner synth CS-80 V4 + patch).
- Operator set **N = 9** in the Custos editor → Custos bound `9100 + 9 = 9109`.

## Results
| Step | Command | Result |
|---|---|---|
| Identity + liveness | `/custos/hello` → `:9109` | `/custos/here [9, 1, replace, CS-80 V4, 2797, 9109]` ✓ |
| Load (live swap) | `/custos/load DX7 V.vst3` → `:9109` | `/custos/loaded [9, …DX7 V.vst3, 3124]` + `/custos/ack [9, "loaded … count=3124"]` ✓ |
| Reload | `/custos/load CS-80 V4.vst3` | `loaded … count=2797` ✓ |
| **Persistence** | save gig (SystemActions 89) → kill GP → deploy → relaunch → `/custos/hello :9109` | `/custos/here [9, 1, replace, CS-80 V4, 2797, 9109]` — **N persisted, auto-bound on restore, no crash** ✓ |

Every Custos→KM reply carries `N` as the first argument. `/custos/here` is versioned (`protoVer = 1`).
`/custos/loaded` carries `boundCount` so KM can dump immediately with no `hello` round-trip.

## Crash diagnosis (resolved)
A gig-load crash was reported (GP `ExceptionType 11`, empty module = a jump through a freed vtable).
The host-trace localised it to `setStateInformation` restoring the real 112 KB state. **Root cause:** the
previously-deployed build had a **hard-coded CS-80** (an E2E convenience), so restore did a CS-80→CS-80
swap that **tore down the outgoing synth mid-restore** while the host queried the plugin — and
`getTailLengthSeconds()` read `inner` **unguarded** on the audio thread (the M1/M3-flagged carry-forward)
→ data race → segfault.

- **Production (no hard-coded synth) boots empty**, so restore has no outgoing synth to tear down — the
  112 KB state restores cleanly (verified above). So it was **not a production bug**; it was the
  hard-coded-synth E2E artifact.
- The race is nonetheless real for **live OSC swaps** (which always have an outgoing synth). Fixed:
  `getTailLengthSeconds()` now try-locks `swapLock` like `processBlock` (`swapLock` made `mutable`).

## Not run
- **N collision** (two instances set to the same N → second fails to bind, shows the UI warning) — needs
  a second Custos instance in a GP slot.
