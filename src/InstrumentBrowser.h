#pragma once
#include <juce_core/juce_core.h>

namespace custos
{
// Pure cursor step for Prev/Next favourite browsing. `count` = number of favourites.
// cur < 0 = unset: next -> first, prev -> last (no wrap). Otherwise cur+delta, wrapping past either
// end (and flagging `wrapped`). Only ±1 deltas are used in practice.
// A favourite "fits" a facade of `facadeCap` slots when its param count is unknown (0), within the
// cap, or over it by AT MOST 10% (tolerated oversize, 2026-07-19: the top params stay unbound —
// binding clamps — which the rig always survived; e.g. Jup-8000 V 3058 / Memory V 3168 in their
// 3000 rungs. load() acks a warning for these). Gross mismatches (a 4000-param synth in a
// Custos 1000) stay refused — that class froze GP on 2026-07-19.
inline int  facadeFitLimit (int facadeCap)           { return facadeCap + facadeCap / 10; }
inline bool favouriteFits (int slots, int facadeCap) { return slots <= 0 || slots <= facadeFitLimit (facadeCap); }

// Browse-scope predicate. scope 0 = favourites only (favOrder >= 1); scope 1 = all instruments.
inline bool favouriteInScope (int favOrder, int scope) { return scope != 0 || favOrder >= 1; }

struct BrowseStep { int index; bool wrapped; };

inline BrowseStep browseStep (int cur, int delta, int count)
{
    if (count <= 0) return { -1, false };
    if (cur < 0)    return { delta > 0 ? 0 : count - 1, false };
    const int nx = cur + delta;
    if (nx >= count) return { 0, true };
    if (nx < 0)      return { count - 1, true };
    return { nx, false };
}

enum class PatchMethod { Param, Pc, PresetFallback };

// Explicit-only: PC is never inferred. Anything not PARAM/PC (incl. PRESET, NONE, empty) falls
// back to the Preset store — the only implicit method, always available.
inline PatchMethod patchMethodFor (const juce::String& controlType)
{
    if (controlType == "PARAM") return PatchMethod::Param;
    if (controlType == "PC")    return PatchMethod::Pc;
    return PatchMethod::PresetFallback;
}
}
