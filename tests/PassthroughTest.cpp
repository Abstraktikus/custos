#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"
#include <memory>

using namespace custos;

// Test seam: expose attachInner on a plain CustosProcessor (already public).
TEST_CASE ("CustosProcessor forwards processBlock to the attached inner processor")
{
    CustosProcessor proc;
    auto fakeOwned = std::make_unique<custos::test::FakeInnerProcessor> (3);
    auto* fake = fakeOwned.get();
    proc.attachInner (std::move (fakeOwned));

    REQUIRE (proc.boundParamCount() == 3);

    proc.prepareToPlay (48000.0, 128);
    REQUIRE (fake->prepared);
    REQUIRE (fake->lastSampleRate == 48000.0);

    juce::AudioBuffer<float> buffer (2, 128);
    buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);

    proc.processBlock (buffer, midi);

    REQUIRE (fake->processCalls == 1);
    REQUIRE (fake->lastNumMidi == 1);
    REQUIRE (buffer.getSample (0, 0) == 0.5f);   // marker written by FakeInnerProcessor
}

TEST_CASE ("CustosProcessor outputs silence when no inner is attached")
{
    CustosProcessor proc;
    REQUIRE (proc.boundParamCount() == 0);

    proc.prepareToPlay (48000.0, 64);
    juce::AudioBuffer<float> buffer (2, 64);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 1.0f, 64);
    juce::MidiBuffer midi;

    proc.processBlock (buffer, midi);

    REQUIRE (buffer.getSample (0, 0) == 0.0f);
    REQUIRE (buffer.getSample (1, 63) == 0.0f);
}
