# Custos

A VST3 **instrument** plugin that is a *host-inside-a-plugin*: it presents a **stable
5000-parameter facade** to its host (Gig Performer), hosts an arbitrary VST3 synthesizer
*inside* itself, mirrors that synth's parameters 1:1 onto the facade, and passes MIDI →
stereo audio straight through it.

Custos is a companion to the [Kapellmeister](https://github.com/Abstraktikus) live-performance
ecosystem and a sibling to [`tacet`](https://github.com/Abstraktikus/tacet).

## Why

Gig Performer does not fully instantiate a plugin loaded at runtime via `ReplacePlugin`
(`GetParameterCount` returns 0, parameter set/get are inert). Only plugins present at gig boot
are parameter-controllable. Custos is boot-loaded **once** and stays fully instantiated; the real
synth is swapped *inside* it, so the host always sees a fixed parameter count and stable parameter
IDs — the `count == 0` problem disappears, and the host's parameter automation keeps working
across inner-synth changes.

- **Mirror only, never interpret.** Facade indices `0..innerCount-1` reflect the inner synth's
  name/value/range/stepped 1:1; the rest are inert. Custos does no mapping or scaling.
- **Stable per-index VST3 ids** (`custos_<i>`) so host macro bindings survive synth changes.
- Vendor: `Kapellmeister` · Product: `Custos`.

See [`docs/superpowers/specs/`](docs/superpowers/specs/) for the full design and
[`docs/superpowers/plans/`](docs/superpowers/plans/) for the milestone plan.

## Status

**Milestone 1** (this branch): buildable VST3 instrument, fixed 5000-param facade, loads one
hard-coded synth inside, MIDI→audio passthrough, parameter mirroring, and a host-call trace for
the GP count-timing experiment. Not yet included (later milestones): OSC control, GUI embedding,
glitch-free synth swap, state persistence, the variant build matrix, and resident mode.

## Build (Windows)

Requires Visual Studio Build Tools 2022 (with the "C++ CMake tools" component — bundles CMake +
Ninja + MSVC). JUCE 8 and Catch2 are fetched automatically.

```
scripts\fetch-deps.cmd     :: one-time: cache JUCE + Catch2 into C:\dev\_deps
scripts\configure.cmd      :: or: scripts\configure.cmd "C:/path/to/Synth.vst3"
scripts\build.cmd          :: builds everything (or: scripts\build.cmd custos_tests Custos)
scripts\test.cmd           :: runs the unit tests via ctest
```

The built plugin lands under `build\Custos_artefacts\...\VST3\Custos.vst3`.

To build and copy it straight into your Gig Performer VST3 folder (override the destination with
`CUSTOS_DEPLOY_DIR`):

```
scripts\deploy.cmd                        :: or: scripts\deploy.cmd "C:/path/to/Synth.vst3"
```

## Licence

MIT — see [LICENSE](LICENSE).
