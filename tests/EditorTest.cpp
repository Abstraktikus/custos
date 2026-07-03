#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"
#include <memory>

using namespace custos;

TEST_CASE ("Synth window starts hidden and is a no-op without an inner synth")
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // message manager for GUI-adjacent construction
    CustosProcessor proc;                        // empty hard-coded path -> no inner attached

    REQUIRE_FALSE (proc.hasInnerSynth());
    REQUIRE_FALSE (proc.isSynthWindowVisible());

    proc.showSynthWindow();                      // no inner -> no window
    REQUIRE_FALSE (proc.isSynthWindowVisible());

    proc.toggleSynthWindow();                    // still no inner -> no window
    REQUIRE_FALSE (proc.isSynthWindowVisible());
}

TEST_CASE ("Show is a graceful no-op when the inner synth has no editor")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.attachInner (std::make_unique<custos::test::FakeInnerProcessor> (2));  // FakeInner has no editor

    REQUIRE (proc.hasInnerSynth());
    proc.showSynthWindow();                      // inner has no editor -> no window created
    REQUIRE_FALSE (proc.isSynthWindowVisible());
}

TEST_CASE ("innerSynthName reflects the attached inner processor")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    REQUIRE (proc.innerSynthName() == "(none)");

    proc.attachInner (std::make_unique<custos::test::FakeInnerProcessor> (2));
    REQUIRE (proc.innerSynthName() == "FakeInner");
}
