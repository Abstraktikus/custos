# Param Dump E2E (2026-07-04)

Autonomous end-to-end verification of F1 (param dump) against a live Gig Performer 5. Custos instance
`N = 9` (port 9109), inner synth CS-80 V4 (`boundCount = 2797`).

## Results
| Step | Command | Result |
|---|---|---|
| Dump a range | `/custos/params 0 8` → `:9109` | 8× `/custos/param [9, idx, val, name]` (idx 0..7, real CS-80 names/values: `1.0 VCO LFO1 Waveform`, `0.428 VCO LFO1 Speed`, …) then `/custos/params/done [9, 0, 8]` ✓ |
| Clamp at boundary | `/custos/params 2795 100` | only idx 2795, 2796 (`boundCount = 2797`) + `/custos/params/done [9, 2795, 2]` — clamped, start echoed ✓ |
| Past end | `/custos/params 3000 5` | only `/custos/params/done [9, 3000, 0]` — nothing sent, start echoed ✓ |

Every message carries `N` first. `/custos/param` is `N, idx:int, val:float, name:string`.
`/custos/params/done` reports the **actually-sent** count and **echoes** the requested start.

The names/values match exactly what the GP `/KM/Plugin` PList harness returns for CS-80 — so KM can now
read a Custos instance's bound params **directly over OSC**, no GP-Script round-trip.
