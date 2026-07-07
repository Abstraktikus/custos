#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"

using namespace custos;

TEST_CASE ("innerSynthKey is empty with no synth and non-empty once loaded")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    REQUIRE (proc.innerSynthKey().isEmpty());
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());
    REQUIRE (proc.innerSynthKey().isNotEmpty());   // falls back to the synth name for a non-plugin inner
}

TEST_CASE ("capture and restore inner state round-trips through the fake")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    auto fake = std::make_unique<test::FakeInnerProcessor>();
    fake->stateMarker = "abc";
    auto* raw = fake.get();
    proc.attachInner (std::move (fake));

    const auto captured = proc.captureInnerState();
    REQUIRE (captured.matches ("abc", 3));
    REQUIRE (proc.restoreInnerState (captured));
    REQUIRE (raw->restoredMarker == "abc");
}
