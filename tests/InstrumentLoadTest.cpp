#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"

using namespace custos;

TEST_CASE ("loadByName resolves a list entry's path; unknown name is a no-op")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setFavorites ({ { "Diva", "C:/does/not/exist/Diva.vst3", 1, 0.0f } });

    // Unknown name -> false, no synth.
    REQUIRE (proc.loadByName ("Nope") == false);
    REQUIRE (proc.hasInnerSynth() == false);

    // Known name -> load is attempted with the mapped path. The .vst3 is fake, so the actual
    // plugin load fails and no inner is attached, but the resolver returned true (name matched).
    REQUIRE (proc.loadByName ("Diva") == true);
}
