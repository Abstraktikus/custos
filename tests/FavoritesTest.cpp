#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"

using namespace custos;
using Catch::Approx;

TEST_CASE ("favourites push accumulates then commits, sorted by favOrder")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.favoritesBegin();
    proc.favoritesAdd ({ "B", "C:/b.vst3", 2, 0.0f });
    proc.favoritesAdd ({ "A", "C:/a.vst3", 1, 0.0f });
    proc.favoritesEnd();

    const auto& favs = proc.getFavorites();
    REQUIRE (favs.size() == 2);
    REQUIRE (favs[0].name == "A");   // favOrder 1 first
    REQUIRE (favs[1].name == "B");
}

TEST_CASE ("setFavorites replaces the list, sorted")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setFavorites ({ { "Z", "C:/z.vst3", 9, 0.0f }, { "Y", "C:/y.vst3", 4, 0.0f } });
    REQUIRE (proc.getFavorites().size() == 2);
    REQUIRE (proc.getFavorites()[0].name == "Y");
}
