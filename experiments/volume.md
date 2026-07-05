# Volume Trim E2E (2026-07-04)

F5 uniform volume trim against a live Gig Performer 5. Custos `N = 9` (port 9109), inner synth CS-80 V4.

## Results
| Step | Command | Result |
|---|---|---|
| Set trim | `/custos/volume -6.0` → `:9109` | accepted, no crash |
| Liveness | `/custos/hello :9109` | still answers `/custos/here [9, 1, replace, CS-80 V4, 2797, 9109]` ✓ |
| Restore | `/custos/volume 0.0` | back to unity |

The DSP is proven by the unit test (`VolumeTest.cpp`: dB gain applied to the output, unity by default,
-6 dB → half, +6 dB → double). Audio level is not measurable over OSC; the operator confirms the
level change aurally while playing. The trim is OSC-only (not a facade parameter) and transient
(the per-synth default from the machine config lands with F4).
