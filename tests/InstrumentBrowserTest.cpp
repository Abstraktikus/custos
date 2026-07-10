#include <catch2/catch_test_macros.hpp>
#include "InstrumentBrowser.h"

using namespace custos;

TEST_CASE ("browseStep: empty list yields unset")
{
    auto s = browseStep (-1, +1, 0);
    REQUIRE (s.index == -1);
    REQUIRE (s.wrapped == false);
}

TEST_CASE ("browseStep: unset start -> first on next, last on prev (no wrap flag)")
{
    REQUIRE (browseStep (-1, +1, 5).index == 0);
    REQUIRE (browseStep (-1, +1, 5).wrapped == false);
    REQUIRE (browseStep (-1, -1, 5).index == 4);
    REQUIRE (browseStep (-1, -1, 5).wrapped == false);
}

TEST_CASE ("browseStep: mid-range advances without wrap")
{
    REQUIRE (browseStep (2, +1, 5).index == 3);
    REQUIRE (browseStep (2, +1, 5).wrapped == false);
    REQUIRE (browseStep (2, -1, 5).index == 1);
    REQUIRE (browseStep (2, -1, 5).wrapped == false);
}

TEST_CASE ("browseStep: wrap at both ends sets wrapped")
{
    auto up = browseStep (4, +1, 5);
    REQUIRE (up.index == 0);
    REQUIRE (up.wrapped == true);
    auto dn = browseStep (0, -1, 5);
    REQUIRE (dn.index == 4);
    REQUIRE (dn.wrapped == true);
}

TEST_CASE ("favouriteFits: unknown (0) always fits; else slots <= facadeCap")
{
    REQUIRE (favouriteFits (0, 1000)    == true);    // unknown -> allow
    REQUIRE (favouriteFits (1000, 1000) == true);    // exact fit
    REQUIRE (favouriteFits (999, 1000)  == true);
    REQUIRE (favouriteFits (1001, 1000) == false);   // one over -> skip
    REQUIRE (favouriteFits (4000, 1000) == false);   // 4000-param synth in a Custos 1000 -> skip
    REQUIRE (favouriteFits (4000, 10000) == true);   // fits the big rung
}

TEST_CASE ("favouriteInScope: scope 0 = favourites only, scope 1 = all")
{
    REQUIRE (custos::favouriteInScope (1, 0) == true);    // fav, fav-scope
    REQUIRE (custos::favouriteInScope (0, 0) == false);   // non-fav, fav-scope
    REQUIRE (custos::favouriteInScope (0, 1) == true);    // non-fav, all-scope
    REQUIRE (custos::favouriteInScope (5, 1) == true);
}
