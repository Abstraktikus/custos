#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"

using namespace custos;

// The docked synth window gets a second on-top strategy, switchable by KM at runtime.
// Mode A (default) = today's unconditional always-on-top. Mode B = follow KM's foreground
// state, reported over /custos/window/ontop. See the 2026-07-20 dock-ontop-mode spec.

TEST_CASE ("dock on-top defaults to Mode A, effective = true")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    REQUIRE (proc.getDockOnTopMode() == DockOnTopAlways);
    REQUIRE (proc.dockOnTopEffective() == true);
}

TEST_CASE ("ontop 0 enters Mode B and drops the effective flag")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setDockOnTopState (0);                       // KM: not foreground
    REQUIRE (proc.getDockOnTopMode() == DockOnTopFollowKm);
    REQUIRE (proc.dockOnTopEffective() == false);
}

TEST_CASE ("ontop 1 enters Mode B with the effective flag raised")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setDockOnTopState (1);                       // KM: foreground
    REQUIRE (proc.getDockOnTopMode() == DockOnTopFollowKm);
    REQUIRE (proc.dockOnTopEffective() == true);
}

TEST_CASE ("ontop toggles within Mode B flip the effective flag both ways")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setDockOnTopState (1);
    REQUIRE (proc.dockOnTopEffective() == true);
    proc.setDockOnTopState (0);
    REQUIRE (proc.dockOnTopEffective() == false);
    proc.setDockOnTopState (1);
    REQUIRE (proc.dockOnTopEffective() == true);
}

TEST_CASE ("ontop -1 returns to Mode A regardless of prior KM state")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setDockOnTopState (0);                       // Mode B, dropped
    REQUIRE (proc.dockOnTopEffective() == false);
    proc.setDockOnTopState (-1);                      // hands off -> Mode A
    REQUIRE (proc.getDockOnTopMode() == DockOnTopAlways);
    REQUIRE (proc.dockOnTopEffective() == true);      // back to unconditional on-top
}
