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

TEST_CASE ("Mode B arms the heartbeat watchdog; Mode A does not")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    REQUIRE (proc.kmHeartbeatArmed() == false);       // Mode A default: no watchdog
    proc.setDockOnTopState (1);
    REQUIRE (proc.kmHeartbeatArmed() == true);         // Mode B: armed
    proc.setDockOnTopState (0);
    REQUIRE (proc.kmHeartbeatArmed() == true);         // each message re-arms
    proc.setDockOnTopState (-1);
    REQUIRE (proc.kmHeartbeatArmed() == false);        // hands off: stopped
}

TEST_CASE ("heartbeat expiry drops the window but stays in Mode B")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setKmHeartbeatTimeoutMs (10);                 // fast for the test
    proc.setDockOnTopState (1);                        // Mode B, foreground, armed 10 ms
    REQUIRE (proc.dockOnTopEffective() == true);

    // Pump the real message loop so the watchdog Timer's message-thread callback is delivered.
    // runDispatchLoopUntil() needs JUCE_MODAL_LOOPS_PERMITTED (off in this build's test target),
    // so drive the always-available runDispatchLoop() and end it from a helper thread after
    // 200 ms (>> the 10 ms timeout), giving the watchdog time to fire first.
    juce::Thread::launch ([] {
        juce::Thread::sleep (200);
        juce::MessageManager::getInstance()->stopDispatchLoop();
    });
    juce::MessageManager::getInstance()->runDispatchLoop();

    REQUIRE (proc.dockOnTopEffective() == false);      // KM silent -> dropped to not-on-top
    REQUIRE (proc.getDockOnTopMode() == DockOnTopFollowKm);   // fail-safe: NOT back to Mode A
    REQUIRE (proc.kmHeartbeatArmed() == false);        // one-shot: not re-armed on its own
}
