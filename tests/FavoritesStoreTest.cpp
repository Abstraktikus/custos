#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "FavoritesStore.h"

using namespace custos;
using Catch::Approx;

TEST_CASE ("favorites JSON round-trips")
{
    std::vector<Favorite> favs { { "Diva", "C:/x/Diva.vst3", 1, -3.0f },
                                 { "DX7",  "C:/x/DX7.vst3",  2,  0.0f } };
    const auto back = favoritesFromJson (favoritesToJson (favs));
    REQUIRE (back.size() == 2);
    REQUIRE (back[0].name == "Diva");
    REQUIRE (back[0].path == "C:/x/Diva.vst3");
    REQUIRE (back[0].favOrder == 1);
    REQUIRE (back[0].gainDb == Approx (-3.0f));
    REQUIRE (back[1].name == "DX7");
}

TEST_CASE ("writeFavorites/readFavorites round-trip via a temp file")
{
    auto tmp = juce::File::createTempFile (".json");
    writeFavorites (tmp, { { "Pigments", "C:/x/Pigments.vst3", 5, 1.5f } });
    const auto back = readFavorites (tmp);
    REQUIRE (back.size() == 1);
    REQUIRE (back[0].name == "Pigments");
    REQUIRE (back[0].gainDb == Approx (1.5f));
    tmp.deleteFile();
}

TEST_CASE ("readFavorites on a missing file returns empty")
{
    REQUIRE (readFavorites (juce::File ("C:/nonexistent/nope.json")).empty());
}
