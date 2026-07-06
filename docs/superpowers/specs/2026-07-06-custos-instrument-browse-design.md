# Custos Instrument Prev/Next Browse over OSC — Design

**Status:** design (2026-07-06). Small OSC addition so GP can flip through Custos's OWN favourites list
(Prev/Next) without parsing the VstDatabase — the knowledge already lives in the plugin.

## 1. Goal & principle

Custos owns the favourites (`favorites.json` + `getFavorites()` + `loadFavorite`). GP flipping through
instruments must NOT force GP to parse the VstDatabase — GP just sends an OSC verb; Custos advances its
own cursor. Confirmed with Martin: **all favourites** (sorted by `favOrder`; brand filter ignored — GP
doesn't know it), **wrap-around** with an end signal.

## 2. Browsing ≠ loading (the critical part)

Flipping (`next`/`prev`) must **NOT load** the synth immediately — rapid flipping would be a catastrophe.
Instead:
1. A **browse cursor** (`browseIndex`) advances and Custos reports back only the **name** (fast).
2. A **400 ms debounce**: only when flipping STOPS does Custos actually load the cursor's synth.
3. **De-dup:** rapid next-then-back landing on the already-loaded synth → no reload (cursor == loaded path).
4. **No concurrent loads:** `load()` runs atomically on the message thread; while it runs no Timer fires and
   no OSC is processed → naturally serialized. Flips during a load are processed after and re-debounced.

The name update and the actual load are **separate OSC messages** so GP can show the name while flipping
and only treat a completed load as playable.

## 3. OSC wire (all N-tagged; replies to the hub :8000)

**GP → Custos** (to `9100+N`):
| addr | args | effect |
|---|---|---|
| `/custos/instrument/next` | — | cursor +1 (wrap); report name; arm debounce. NO load. |
| `/custos/instrument/prev` | — | cursor −1 (wrap); report name; arm debounce. NO load. |
| `/custos/instrument/set` | `i:int` | cursor = clamp(i); report name; arm debounce. |

**Custos → hub**:
| addr | args | when |
|---|---|---|
| `/custos/browsing` | `N, index:int, name:string, wrapped:int` | every browse step; `wrapped=1` on the step that wraps past an end |
| `/custos/loaded` | `N, path, boundCount, innerTotal` (existing) | after the debounced load completes (= **ready/playable**: load() runs prepareToPlay before emitting) |

## 4. Cursor semantics (pure helper `browseStep`)

`struct BrowseStep { int index; bool wrapped; }; BrowseStep browseStep (int cur, int delta, int count);`
- `count == 0` → `{-1, false}` (nothing to browse).
- `cur < 0` (unset): `next` → `{0,false}` (first), `prev` → `{count-1,false}` (last).
- else `nx = cur+delta`; `nx >= count` → `{0, true}`; `nx < 0` → `{count-1, true}`; else `{nx, false}`.

`browseInstrument(delta)`: if `browseIndex < 0`, seed it from the loaded synth's index
(`indexOfPath(currentSynthPath)`, −1 if none) so flipping is relative to what's loaded; then `browseStep`,
store, emit `/custos/browsing`, restart the 400 ms debounce.

`commitBrowseLoad()` (debounce fired): if `browseIndex` valid and `favs[browseIndex].path != currentSynthPath`
→ `load(favs[browseIndex].path)` (emits `/custos/loaded`). Otherwise no-op (de-dup).

`loadInner` resets `browseIndex = -1` so any load (browse, OSC `/custos/load`, editor pick) re-syncs the next
flip to the actually-loaded synth.

## 5. Non-goals
- No editor UI for browse (GP-Script drives it). No OSC feedback toggle. Brand filter not applied to browse.
- Debounce is fixed 400 ms (not configurable).

## 6. Testing
- Pure `browseStep`: wrap at both ends, unset→first/last, empty list, mid-range no-wrap.
- OSC parse: `/custos/instrument/{next,prev,set}` → Command kinds (`OscCommandTest`).
- De-dup: `commitBrowseLoad` is a no-op when the cursor path == currentSynthPath (path-compare unit).
- (Timer firing is integration — not unit-tested; the message-thread serialization is inherent.)

## 7. Cross-project handoff (GP session)
GP-Script must: send `next/prev/set`, receive `/custos/browsing` (show the name while flipping), treat
`/custos/loaded` as "ready/playable". Delivered as a copy-paste prompt for the GP session once the Custos
side lands.
