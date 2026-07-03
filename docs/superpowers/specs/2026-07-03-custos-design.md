# Custos — Design Specification

**Date:** 2026-07-03
**Status:** Approved (brainstorming complete, ready for planning)
**Author:** Martin Nafzger (design), drafted with Claude

---

## 1. Purpose

Custos is a VST3 **instrument** plugin that acts as a *host-inside-a-plugin*: Gig Performer (GP)
boot-loads Custos **once** (so GP fully instantiates it), Custos hosts an arbitrary synthesizer
**inside** itself, passes the inner synth's original GUI through, and presents a **stable parameter
facade** to the outside.

- **Signal:** MIDI in → **always Stereo out**. No audio input.
- **Facade:** a fixed parameter count that never changes at runtime, so GP never sees
  `GetParameterCount == 0` and never sees a count change.

### The problem it solves

GP does not fully instantiate a plugin loaded via runtime `ReplacePlugin`: `GetParameterCount`
returns 0, `SetParameter`/`GetParameter` are inert. Only plugins present at gig boot are
parameter-controllable. This is a proven, hard GP platform limit. Consequence: dynamically loaded
synths (favourites, voice changes) cannot be parameter-controlled and their parameters cannot be
read.

Custos removes the problem: GP boot-loads Custos (always fully instantiated). Custos hosts the real
synth inside (as a real host it is immune to GP's limit), and always presents a fixed parameter
count. The synth change happens *inside*; GP only notices that parameter **names** change (GP's
name resolution works again).

### Big secondary win (motivation)

Because GP no longer needs `ReplacePlugin`, the whole Local-Rackspace voice-slot apparatus + the
Global↔Local OSC bridge + the `count==0` misery can eventually be retired. Long term this enables a
slow migration away from GP (GP stays the backend for song list / tempo / playhead / audio mixer
for now).

---

## 2. Scope

### In scope
Transparent proxy: MIDI/transport in, Stereo out, parameters mirrored 1:1, GUI passed through,
inner synth state persisted, metadata control via OSC.

### Out of scope (hard boundaries)
No audio routing, no effect chaining, no own mapping UI, no file interpretation. Custos
**reads/computes/interprets nothing** — it mirrors. GP does the hardware→parameter mapping exactly
as today (its macros/automation bind to the stable Custos params). Custos does **not** map itself.

---

## 3. Framework & Stack

- **JUCE / C++.** JUCE does both roles in one framework:
  - `juce::AudioProcessor` → Custos *is* a VST3.
  - `juce::AudioPluginFormatManager` + `juce::AudioPluginInstance` → Custos *hosts* the inner VST3
    (audio + parameters + GUI embedding via `IPlugView`/HWND).
- **Build:** CMake (`juce_add_plugin`), not Projucer.
- **Branding:** VST3 vendor/manufacturer = **"Kapellmeister"** (company name + shared
  `PLUGIN_MANUFACTURER_CODE` across all variants). Product name stays **"Custos"**. So GP lists the
  plugins under the Kapellmeister vendor, named `Custos 01` … `Custos Joker`.
- **Rationale for not using Rust/nih-plug (the `tacet` stack):** nih-plug can only *be* a plugin, it
  cannot *host* one. Hosting a VST3 inside a plugin (factory, `IComponent`, `IEditController`,
  audio processing, GUI embedding) is a solved, battle-tested problem in JUCE and unbuilt territory
  in Rust. The OSC reference (`drlight-code/osccontrol-light`) is also JUCE.
- **Platform:** Windows / VST3 first; macOS later.

---

## 4. Variant model ("the n wrappers")

A single Custos instance cannot know which GP slot it sits in or which instrument to load — that
identity **must come from the build/outside**. Solution: N build variants, each with a distinct,
stable VST3 identity so GP pins each to its slot.

- **One** source tree.
- A **CMake build matrix** produces N `.vst3` binaries: `Custos 01`, `Custos 02`, … + `Custos
  Joker`. Each gets a distinct `PLUGIN_CODE` → distinct **FUID + product name** → GP treats them as
  distinct plugins and pins each to a fixed slot. All variants share the vendor/manufacturer
  **"Kapellmeister"** (same `PLUGIN_MANUFACTURER_CODE`); only `PLUGIN_CODE` differs per variant.
- The only per-variant difference is compile-time constants:
  - `CUSTOS_SLOT` — integer slot index (drives FUID, name, OSC address).
  - `CUSTOS_MODE` — `resident` | `joker`.
- **Variant = identity only.** *Which* synth runs inside is a runtime decision (persisted state /
  OSC), never baked into the build. This keeps the "reads/computes nothing" principle intact: Custos
  loads a synth *on command*, it does not interpret config or perform mapping.

---

## 5. Parameter facade

- Compile-time constant `kFacadeParamCount`. **Start at 5000**, measure GP's UI load in GP, then
  reduce if warranted. (Runtime-configurable count is technically impossible: GP queries the count
  at boot, before any OSC can arrive — so the count must be fixed at construction.)
- Index `0..innerCount-1` mirror the inner synth **1:1**: name + value + range + stepped/discrete
  behaviour. Index `innerCount..kFacadeParamCount-1` are **inert** (no effect, stable neutral
  name/value).
- **Pure passthrough.** Custos does not map, scale, compute, or interpret. It mirrors.
- **Joker mode:** always reports `kFacadeParamCount` (5000). Inner synth swappable at any time.
- **Resident mode:** reports the *effective* inner count (GP-UI optimisation), **locked until
  restart** (a swap would require a special override command). Changing the resident synth takes
  effect at the **next GP boot** (the natural count-refresh point — we never change the count
  mid-session).
- **Graceful degradation:** if GP does not adopt the effective count early enough at boot, a
  resident variant simply reports `kFacadeParamCount` too. Nothing breaks; only the GP-UI
  optimisation is lost. The optimisation is a bonus, never a load-bearing assumption.

---

## 6. Two control planes

### 6.1 Parameters — GP-native (v1 primary)
GP binds its macros/automation to the stable Custos parameters. Custos writes those values 1:1 into
the inner synth's host-parameter API (exactly as a DAW would → works with *any* synth, without the
synth needing OSC). This is the primary control path for v1.

### 6.2 Metadata — OSC (channel built in v1, KM integration later)
OSC carries **only metadata**: "load synth `<path>`", swap, window show/hide/rect/focus. Custos does
not receive parameter automation over OSC (GP does that natively).

- **Addressing:** slot-indexed, e.g. `/custos/<slot>/load`, `/custos/<slot>/swap`,
  `/custos/<slot>/window/*`.
- **Port strategy:** all Custos instances live in GP's single process, so they cannot each bind the
  same UDP listen port. A process-wide shared OSC listener (singleton) receives on one port and
  dispatches to the addressed slot; replies/acks go back to the sender's address. (Exact port
  numbers and ack schema finalised during planning; KM's diagnostics hub is on :8000 as the
  established pattern.)
- **v1 stance:** the channel is built, but v1 does **not** depend on KM. The sender can be a
  GP-side OSC script (driven by Song.ini) or KM later — Custos does not care who sends.

---

## 7. GUI / window strategy

- Custos has a **minimal own editor** (logo + status "Synth: `<name>` — UI in window"). Rationale:
  otherwise GP's generic "Show Plugin" would render the 5000-slider generic view. The mini-editor
  intercepts that.
- The **real synth GUI lives in a free-standing, Custos-owned window** whose
  visibility/position/z-order is driven by OSC → **GP is never involved in showing it, so GP never
  comes to front.** This solves the two-app workflow pain (working "in Kapellmeister" by default).
- **Why not embed in GP's editor:** that ties the window to GP, which raises GP to front on open.
- **Why a native window (not embedded in Kapellmeister's DOM):** the synth GUI is a native HWND;
  Kapellmeister is a WebView2. Native windows and web content cannot interleave ("airspace"
  problem) — a native window always sits *on top* of web content and cannot be embedded in the DOM.
- **Target look:** *docked over a Kapellmeister panel* — KM reserves a placeholder region and
  streams its screen rectangle via OSC; Custos overlays its window there and tracks it on
  resize/move. **v1 look:** free floating window. **Both use the same OSC geometry contract**
  (`show` / `hide` / `setRect` / `focus`) — so the final look is not decided now; v1 ships floating,
  docked is a later enhancement with no throwaway code.
- **Explicitly excluded:** cross-process `SetParent` reparenting of the synth HWND into KM's window
  (Stufe 3) — too fragile (focus/DPI/z-order, synth lives in GP's process).

---

## 8. State persistence

Custos is **self-contained** so a gig loads standalone (even with KM not running):

- `getStateInformation` persists: (a) inner synth **identity** (path/UID from the VstDatabase) +
  (b) the inner synth's **full state chunk** (its own `getStateInformation`).
- `setStateInformation` (at gig load) loads the synth **synchronously** and restores its state
  exactly, before returning.
- KM is **not** required for restore. Runtime synth assignment (OSC) updates what will be persisted;
  it is not a precondition for loading a saved gig.

---

## 9. Glitch-free swap (Joker)

Sequence, driven on the message thread:

1. Bypass → output silence.
2. Settle (let tails/voices die within a short window).
3. Unload the current inner synth.
4. Load the new inner synth **synchronously** (message thread — never the audio thread).
5. Create its state / parameters, apply any restored state.
6. Activate (clear bypass).
7. OSC ack to the sender.

The audio thread only ever sees silence during a swap. Plugin loading never happens on the audio
thread.

---

## 10. Threading

- Parameter writes: lock-free from the audio thread into the inner synth.
- Load / swap / GUI lifecycle: strictly on the message thread.
- Audio is guarded by an atomic bypass flag during load/swap.

---

## 11. Test strategy

- **M1 key experiment (make-or-break for resident mode):** does GP read parameter info at boot
  *before* or *after* `setStateInformation`? Does GP accept an effective (non-5000) count? Result
  decides whether the resident optimisation is feasible.
- **Unit:** facade mapping (index↔inner, inert slots, range/stepped fidelity), swap state machine.
- **Integration:** Custos hosts a test synth headless — MIDI→Audio path, parameter round-trip
  (set via facade → observed on inner synth).
- **E2E in GP:** boot, parameter bind, swap glitch-freeness, gig save/load, CPU measurement (to
  decide "wrap all VSTs" vs "only 1–2 Jokers").

---

## 12. Milestones

- **M1 (smallest meaningful):** empty VST3 instrument, fixed 5000 params, loads a **hard-coded**
  synth inside, passes MIDI→Audio + params `0..innerCount-1` through. No OSC, no GUI polish, no
  swap. **Plus** the GP count-timing experiment.
- **M2:** synth GUI in floating window + minimal own editor.
- **M3:** OSC metadata channel (load/swap) + self-contained state persistence.
- **M4:** glitch-free Joker swap + variant build matrix (N identities).
- **M5:** resident mode (effective count) — contingent on the M1 experiment.
- **Later:** KM integration, docked window, macOS.

---

## 13. Ecosystem context

- Kapellmeister (Rust/Tauri) already speaks OSC (diagnostics hub on port 8000); the OSC pattern is
  established.
- A VstDatabase (plugin list with paths) is managed by KM — the synth path comes from there.
- Reference pattern for "plugin speaks OSC": `drlight-code/osccontrol-light` (JUCE).
- Sibling projects: `tacet` (Rust/nih-plug silent placeholder instrument), `kapellmeister`.

---

## 14. Open items deferred to planning / later

- Exact OSC port numbers, address grammar, and ack schema.
- Number of variants N and their slot naming convention.
- CPU cost of universal wrapping (measured in M1/E2E) → decides "wrap all" vs "only Jokers".
- Resident synth reassignment UX (override command; effect-on-next-boot semantics).
- Public GitHub repo (like `tacet`, `Abstraktikus/…`) — TBD.
