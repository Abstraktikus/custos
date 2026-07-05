# Favorites E2E (2026-07-04)

F4 favourites against a live Gig Performer 5. Custos `N = 9` (port 9109). Stale
`%APPDATA%\Custos\favorites.json` deleted first for a clean run.

## Push → persist (autonomous)
Sent to `:9109`, in order:
```
/custos/favorites/begin
/custos/favorite 0 "CS-80" "C:/Program Files/Common Files/VST3/CS-80 V4.vst3" 1 -3.0
/custos/favorite 1 "DX7"  "C:/Program Files/Common Files/VST3/DX7 V.vst3"    2  0.0
/custos/favorites/end 2
```
`%APPDATA%\Custos\favorites.json` was written correctly (sorted by favOrder):
```json
[ { "name": "CS-80", "path": ".../CS-80 V4.vst3", "favOrder": 1, "gainDb": -3.0 },
  { "name": "DX7",   "path": ".../DX7 V.vst3",    "favOrder": 2, "gainDb":  0.0 } ]
```
→ the OSC push → machine-config write path is verified. ✓

## Operator checks (UI / audio — not observable over OSC)
- The editor ComboBox lists "CS-80" then "DX7" (ranked by favOrder).
- Picking a favourite loads it (`/custos/loaded` on the hub) and applies its `gainDb` as the trim
  (CS-80 → -3 dB, DX7 → 0 dB; aural).
- After a GP restart with no KM push, the ComboBox still lists both (config read on boot). The config
  file persists on disk across restarts (verified present).

The in-memory list + sort + volume-default lookup are covered by the unit tests
(`FavoritesTest.cpp`, `FavoritesStoreTest.cpp`).
