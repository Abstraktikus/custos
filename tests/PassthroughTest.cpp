#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"
#include "HostTrace.h"
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

TEST_CASE ("trace is gated by setTraceEnabled: silent off, writes on")
{
    auto logFile = custos::traceLogFile();
    logFile.deleteFile();

    custos::setTraceEnabled (false);
    custos::trace ("should-not-appear");
   #if defined(CUSTOS_HOST_TRACE)
    REQUIRE (! logFile.existsAsFile());          // default OFF -> nothing written
   #endif

    custos::setTraceEnabled (true);
    custos::trace ("unit-test-marker");
   #if defined(CUSTOS_HOST_TRACE)
    REQUIRE (logFile.existsAsFile());
    REQUIRE (logFile.loadFileAsString().contains ("unit-test-marker"));
    REQUIRE (! logFile.loadFileAsString().contains ("should-not-appear"));
   #else
    SUCCEED ("tracing compiled out");
   #endif
    custos::setTraceEnabled (false);             // leave the global gate off for other tests
}
