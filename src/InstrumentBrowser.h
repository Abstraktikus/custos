#pragma once

namespace custos
{
// Pure cursor step for Prev/Next favourite browsing. `count` = number of favourites.
// cur < 0 = unset: next -> first, prev -> last (no wrap). Otherwise cur+delta, wrapping past either
// end (and flagging `wrapped`). Only ±1 deltas are used in practice.
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
}
