# M3 E2E — OSC runtime synth load (2026-07-04)

Autonomous end-to-end run of the M3 OSC channel against a live Gig Performer 5,
with Custos in Global slot **G|9** (facade count 5001 = 5000 + bypass).

## Setup
- Built + deployed the M3 `Custos.vst3` (Debug) hard-coded to CS-80 V4 as the boot default.
- GP save-first (SystemActions param 89) → terminate (no "Quit" action param in this GP) →
  deploy → relaunch the actually-open gig (path read from the GP process command line, **not**
  assumed from a folder) → GP reloads Custos with the OSC receiver on port 9100.
- Drivers: `tools/kmplugin.py` (GP per-plugin harness, hub :8000) and a small OSC send/listen
  helper for `/custos/*` (→ :9100) and `/KM/Sys/*` (→ :54344).

## Results
| Step | Command | Result |
|---|---|---|
| Baseline | `kmplugin plist Custos 0 5` | CS-80 params ("VCO LFO1 …"), count 5001 |
| Load | `/custos/load .../DX7 V.vst3` → :9100 | ack `loaded … DX7 V.vst3 count=3124` |
| Clear | `/custos/clear` → :9100 | ack `cleared` |
| Restore | `/custos/load .../CS-80 V4.vst3` | ack `loaded … count=2797` |

**OSC channel, safe swap, and ack all work.** `load` rebinds the facade to the new inner
(bound-count changes per synth: DX7 3124, CS-80 2797) and acks to the KM hub.

## Finding: GP caches the facade param info; the swap is invisible to the host
After `/custos/load DX7` (ack OK), `kmplugin plist Custos` still reported **CS-80** names *and*
values. Decisive control test: after `/custos/clear` (facade fully inert — every param unbound,
`getValue`→0, `getName`→{}), PList **still** reported CS-80.

→ **GP caches parameter names + values at plugin-load time and never re-queries live.**
`FacadeParameter::getValue/getName` do read the bound inner live, so the swap itself is correct;
what's missing is a host notification (`updateHostDisplay(ChangeDetails().withParameterInfoChanged(true))`
→ VST3 `restartComponent`) to invalidate GP's cache.

## Decision (Martin): accept by-design — no code change
- KM addresses Custos by **stable index** (`custos_<i>`); the facade IDs and count never change.
- Param **names are cosmetic** for KM-driven control; `setValue` routes live to the swapped inner
  regardless of GP's cached display.
- Therefore `updateHostDisplay` is intentionally **not** added in M3. The runtime swap is verified
  by the OSC **ack/count**, not by the `/KM/Plugin` harness (which reflects GP's cache).

**Consequence for future testing:** the per-plugin harness cannot observe a runtime swap — assert
on the ack/count, not on `PList`.

## Not run
- Persistence gig save/reload E2E (would mutate the real working gig; the Task-2 unit round-trip
  covers the `StateCodec` path + `get/setStateInformation`).
