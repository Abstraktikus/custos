#include <catch2/catch_test_macros.hpp>
#include "MidiRouteMatrix.h"
#include <array>

using namespace custos;

TEST_CASE ("route selector item-id <-> route value mapping")
{
    REQUIRE (routeValueFromItemId (1)  == 0);   // "M" -> mute
    REQUIRE (routeValueFromItemId (2)  == 1);   // "1" -> route to output 1
    REQUIRE (routeValueFromItemId (17) == 16);  // "16" -> route to output 16
    REQUIRE (itemIdFromRouteValue (0)  == 1);   // mute -> "M"
    REQUIRE (itemIdFromRouteValue (16) == 17);
    REQUIRE (routeValueFromItemId (0)  == 0);   // no selection -> mute (defensive clamp)
    REQUIRE (routeValueFromItemId (99) == 16);  // clamp high
}

TEST_CASE ("route <-> item-id arrays round-trip (identity and mixed)")
{
    std::array<int,16> ident {}; for (int i = 0; i < 16; ++i) ident[(size_t) i] = i + 1;
    REQUIRE (routeFromItemIds (itemIdsFromRoute (ident)) == ident);

    std::array<int,16> mixed = ident;
    mixed[7] = 0;   // input ch 8 muted
    mixed[3] = 1;   // input ch 4 -> output 1
    REQUIRE (routeFromItemIds (itemIdsFromRoute (mixed)) == mixed);

    const auto ids = itemIdsFromRoute (ident);
    REQUIRE (ids[0]  == 2);    // route value 1  -> item id 2
    REQUIRE (ids[15] == 17);   // route value 16 -> item id 17
}
