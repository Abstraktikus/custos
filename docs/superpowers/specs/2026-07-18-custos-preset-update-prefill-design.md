# Custos — Preset "Update" via name-field prefill (design)

Date: 2026-07-18 · Status: approved (Martin: "mach (a)"; variant decided autonomously)

## Problem

Updating a stored `.cuspreset` (e.g. after tweaking the instrument's own volume) requires
re-typing its exact name into the editor's "New" field — the Save button is a no-op on an
empty field, and the field is never prefilled (and is even cleared after each save). Over
OSC the overwrite already works (`/custos/preset/save` with the same name), but the plugin
UI has no one-click path.

## Decision

**Prefill the preset-name field with the loaded preset's name.** Save then means "update
the loaded preset" with one click; typing a different name remains save-as-new. The
alternative — "empty field = overwrite current" — was rejected as invisible magic with
accidental-overwrite risk.

## Design

### Processor (`CustosProcessor`)

New state: `juce::String currentPresetName` — the name of the preset last loaded or saved
for the currently loaded synth; empty = none. Exposed as `loadedPresetName()`. Every
(re)assignment bumps an `int presetNameRevision` so the editor can detect *events* (e.g.
re-loading the same preset), not just name changes.

Transitions (all followed by `refreshEditor()` where not already called):

| Event | Effect |
|---|---|
| `loadPresetByName` success (covers picker, OSC load/set, browse-commit, pending recall) | `currentPresetName = name` |
| `savePreset` success (UI or OSC) | `currentPresetName = name` (a save-as-new makes the new preset current) |
| `renamePreset(old, new)` success, `old == current` | `currentPresetName = new` |
| `deletePreset(name)` success, `name == current` | cleared |
| synth load/swap/unload (`load()`) | cleared (next to the existing `presetCursor = -1` reseed) |

`savePreset`/`loadPresetByName`/`renamePreset`/`deletePreset` gain a `refreshEditor()`
call so OSC-driven store edits also refresh the editor's picker — previously only the
editor's own button handlers rebuilt the list.

### Editor (`CustosEditor`)

New member: `int lastPresetNameRev = -1`. In `refresh()`:

```cpp
if (const int rev = proc.presetNameRevision(); rev != lastPresetNameRev)
{
    presetNameField.setText (proc.loadedPresetName(), false);
    lastPresetNameRev = rev;
}
```

Sync-on-revision only, so a user's half-typed new name is never clobbered by unrelated
refreshes (window toggles, favourites, route changes) — but any load/save event re-syncs,
including re-loading the same preset. Save button: stops clearing the field (the saved
name stays, ready for the next update); list rebuild is covered by the processor's
`refreshEditor()`. Two test seams expose the field: `presetNameText()` /
`setPresetNameText()`.

Empty-name Save stays a no-op. OSC contract unchanged.

## Testing

- `PresetProcessorTest`: `loadedPresetName()` lifecycle — empty initially; tracks save,
  load, rename-of-current; clears on delete-of-current and on synth swap.
- `EditorTest`: field prefills after a preset load + refresh; a typed (different) name
  survives a further refresh when no new load happened.
