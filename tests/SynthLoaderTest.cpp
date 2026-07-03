#include <catch2/catch_test_macros.hpp>
#include "SynthLoader.h"

using namespace custos;

TEST_CASE ("SynthLoader rejects an empty path")
{
    juce::String err;
    auto p = SynthLoader::loadVST3 ("", 44100.0, 512, err);
    REQUIRE (p == nullptr);
    REQUIRE (err.isNotEmpty());
}

TEST_CASE ("SynthLoader rejects a missing file")
{
    juce::String err;
    auto p = SynthLoader::loadVST3 ("C:/does/not/exist.vst3", 44100.0, 512, err);
    REQUIRE (p == nullptr);
    REQUIRE (err.contains ("not found"));
}

// Gated: only runs when CUSTOS_TEST_SYNTH points at a real VST3 synth. Tag [.] hides it
// from the default run so CI without a synth stays green.
TEST_CASE ("SynthLoader loads a real VST3 synth", "[.integration]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("CUSTOS_TEST_SYNTH", {});
    if (path.isEmpty()) { WARN ("set CUSTOS_TEST_SYNTH to a .vst3 to run this"); return; }

    juce::ScopedJuceInitialiser_GUI juceInit;   // message manager for plugin instantiation
    juce::String err;
    auto p = SynthLoader::loadVST3 (path, 44100.0, 512, err);
    REQUIRE (err.isEmpty());
    REQUIRE (p != nullptr);
    REQUIRE (p->getParameters().size() > 0);
}
