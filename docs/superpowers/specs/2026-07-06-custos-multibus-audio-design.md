# Custos Multi-Bus Audio Facade — Design

**Status:** design (2026-07-06). Fixes the multi-output inner-synth crash AND exposes a fixed multi-bus
audio facade, mirroring the fixed-param-count facade philosophy.

## 1. Context & problem

Custos hosts an arbitrary inner synth and passes audio straight through: `processBlock` calls
`inner->processBlock(buffer, midi)` with **Custos's own stereo (2-channel) buffer**, and configures the
inner via the legacy `setPlayConfigDetails(0, 2, …)`.

**Crash root cause (confirmed 2026-07-06 with Martin):** a multi-output inner (e.g. KORG **M1**, which has
a Main bus + several individual output buses) keeps its full output-bus layout. JUCE's `processBlock` uses
ONE buffer for in+out sized `max(totalIn, totalOut)`; the inner writes to output channels 2,3,…,n that do
**not exist** in Custos's 2-channel buffer → out-of-bounds write → `memcpy` access violation (matches the
crash stack `memcpy ← M1.vst3 ← Custos 3000.vst3 ← GP init`). It is an **audio-channel/bus** problem, not
parameters and not state-restore (both ruled out earlier).

## 2. Goal

Two separable layers:
- **Inner safety (mandatory — the fix):** always give the inner a buffer sized to ITS real channel count,
  so it can never overrun. Agnostic to how many ports the inner has (1..n).
- **Outer exposure (the facade):** Custos presents a **fixed** audio bus layout to the host — **1 stereo
  input, 5 stereo outputs** — the audio analog of the fixed 5000-param facade. The inner's outputs map onto
  these buses; where the inner has none, the bus is silent ("swallowed at the facade").

## 3. External bus layout (FIXED, set in ctor)

- **1× stereo input** ("Input").
- **5× stereo output** ("Out 1".."Out 5").
- `isBusesLayoutSupported` accepts exactly this (input stereo-or-disabled, each output stereo).
- GP will show 2 in + 10 out pins per Custos; pin visibility is GP's concern (hideable — not Custos's
  problem). Bus count is fixed at construction (like param count) — 5 is the chosen reserve.

## 4. Internal processing (per block)

Prepared once (in `prepareToPlay`/`loadInner`, NOT per block — no audio-thread allocation): a **scratch
`AudioBuffer<float>`** sized `max(inner.getTotalNumInputChannels(), inner.getTotalNumOutputChannels())`
channels × block size. Reused every block. If no inner: all output buses cleared, done.

Per block:
1. **Input:** copy Custos input bus (ch 0/1) into the scratch's first stereo input pair; zero any further
   scratch input channels. (Weitere Inner-Inputs bekommen Stille — regardless of how many inputs the inner
   has, only the first stereo pair is fed.)
2. Apply the existing MIDI route matrix to `midi` (unchanged), then `inner->processBlock(scratch, midi)`.
   The inner writes safely into its own channel count — **no overrun → crash fixed.**
3. **Output mapping** (scratch → Custos's 10-channel output buffer = 5 stereo buses):
   - **Default ("Main L/R only" OFF):** inner output pair `k` → Custos out-bus `k` for `k = 1..4`; inner
     pairs `5,6,…,n` are **summed** into out-bus 5. (Pairs = scratch channels `[0,1],[2,3],…`.)
   - **"Main L/R only" ON:** **sum ALL** inner output channels into out-bus 1 (all L→out1-L, all R→out1-R);
     out-bus 2..5 = silence. (Variant B, chosen by Martin: the facade stays dumb; if a synth mirrors Main
     to an Aux and this doubles/clips, the operator turns the toggle OFF and routes in GP instead — the
     facade cannot know the use-case.)
4. Apply master gain (existing F5) to the output buffer.

**Mono/odd channels:** a lone mono scratch channel is duplicated to L+R of its target bus.

## 5. "Main L/R only" toggle — LOCAL, not OSC

- New editor control (checkbox/toggle), **local only** — NOT driven or reported over OSC (same stance as
  the editor MIDI matrix). Operator sets it per instance in the Custos UI.
- **Persisted in the Custos VST state** so it survives gig save/reload: bump `StateCodec` to **v4** —
  append one byte (`mainLROnly`) after the v3 route block; v1/v2/v3 blobs parse with the flag defaulting
  to `false` (back-compat).
- Read at load via `setStateInformation`; applied atomically (like `masterGain`) so `processBlock` reads it
  lock-free.

## 6. Non-goals

- No OSC verb for the toggle; no OSC for the bus config. (Meta-over-OSC stays for load/window/etc.; audio
  routing shape is a local/host concern.)
- No smart de-duplication / auto-gain — the facade never inspects "which use-case is loaded".
- Input is only ever the first stereo pair — no multi-input mapping.
- Bus count stays fixed at 5 stereo out (not per-inner dynamic — same rationale as fixed param count).

## 7. Testing

- **Pure mapping helper** (unit-tested without JUCE audio device): given inner output channel data and the
  mode flag, produce the 5-bus (10-channel) output. Cases: 1 pair, 2 pairs, exactly 5, >5 (fold into bus 5
  = sum), mono/odd, empty inner (all silent); "Main L/R only" ON sums all into bus 1 and silences 2..5.
- **StateCodec v4** round-trip test (flag true/false; v1/v2/v3 back-compat → false).
- **Scratch sizing:** unit-test the size = `max(innerIn, innerOut)`.
- **Build + real M1 boot test:** deploy, boot `VSTProbe.gig` with M1 in N=2 → loads WITHOUT crash
  (confirms the audio-bus root cause and the fix in one).

## 8. Bundled small editor fixes (Martin 2026-07-06)

Small, unrelated-to-audio polish carried in the same branch/PR:

- **Instrument picker placeholder:** `favPicker.setTextWhenNothingSelected("Instrument…")` renders a
  garbled ellipsis (encoding). Change to an **empty string** at BOTH sites — the ctor and the no-synth
  branch of `rebuildInstrumentList` — so the picker shows **nothing** when nothing is selected (the
  "Instrument" label already names it). When a synth IS loaded, keep showing its name.
- **Titled vs borderless synth window:** split the two open paths —
  - **"Open"** opens the hosted synth's editor in a **titled window** (native title bar; movable via the
    bar, has a close button). This is the new expected default.
  - **"Open fixed"** stays **borderless** (current behavior) and is the only path affected by the
    **movable / clamp** toggles and the x/y/w/h rect.
  - Fixes the earlier "can't open the synth window with a title bar in any setting" observation. Keep the
    existing OSC window verbs (`/custos/window …`) working for the borderless/fixed path.

## 9. Open questions

None blocking. Exact editor placement of the "Main L/R only" toggle is a plan-level layout detail.
