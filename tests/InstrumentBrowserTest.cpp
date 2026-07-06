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
