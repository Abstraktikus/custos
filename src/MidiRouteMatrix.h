#pragma once
#include <array>

namespace custos
{
// Route selector ComboBox item-ids: id 1 = "M" (route value 0 = mute); id 2..17 = "1".."16"
// (route value 1..16). routeValue = itemId - 1 ; itemId = routeValue + 1. Both directions clamp.
inline int routeValueFromItemId (int itemId)
{
    const int v = itemId - 1;
    return v < 0 ? 0 : (v > 16 ? 16 : v);
}

inline int itemIdFromRouteValue (int routeValue)
{
    const int v = routeValue < 0 ? 0 : (routeValue > 16 ? 16 : routeValue);
    return v + 1;
}

inline std::array<int, 16> routeFromItemIds (const std::array<int, 16>& itemIds)
{
    std::array<int, 16> out {};
    for (int i = 0; i < 16; ++i) out[(size_t) i] = routeValueFromItemId (itemIds[(size_t) i]);
    return out;
}

inline std::array<int, 16> itemIdsFromRoute (const std::array<int, 16>& route)
{
    std::array<int, 16> out {};
    for (int i = 0; i < 16; ++i) out[(size_t) i] = itemIdFromRouteValue (route[(size_t) i]);
    return out;
}
}
