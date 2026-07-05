#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"

using namespace custos;
using Catch::Approx;

TEST_CASE ("master volume trims the output (dB gain applied after the inner)")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.prepareToPlay (48000.0, 8);
    proc.attachInner (std::make_unique<custos::test::FakeInnerProcessor> (1));  // fills ch0 with 0.5

    juce::AudioBuffer<float> buf (2, 8);
    juce::MidiBuffer midi;

    // Default = unity gain (0 dB).
    buf.clear(); proc.processBlock (buf, midi);
    REQUIRE (buf.getSample (0, 0) == Approx (0.5f));

    // ~ -6.02 dB = 0.5 linear -> output halved.
    proc.setVolumeDb (juce::Decibels::gainToDecibels (0.5f));
    buf.clear(); proc.processBlock (buf, midi);
    REQUIRE (buf.getSample (0, 0) == Approx (0.25f));

    // ~ +6.02 dB = 2.0 linear -> output doubled.
    proc.setVolumeDb (juce::Decibels::gainToDecibels (2.0f));
    buf.clear(); proc.processBlock (buf, midi);
    REQUIRE (buf.getSample (0, 0) == Approx (1.0f));
}
