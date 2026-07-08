#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"
#include <memory>

using namespace custos;

TEST_CASE ("loadInner swaps the inner and releases the outgoing one")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;                       // enableOsc defaults to false -> no port bound
    proc.prepareToPlay (48000.0, 128);

    int releasesA = 0;                           // external sink: survives the outgoing inner's destruction
    auto a = std::make_unique<custos::test::FakeInnerProcessor> (3);
    auto* aPtr = a.get();
    a->releaseSink = &releasesA;
    REQUIRE (proc.loadInner (std::move (a)));
    REQUIRE (proc.boundParamCount() == 3);
    REQUIRE (aPtr->prepared);                    // still alive here — prepared to the current play config

    auto b = std::make_unique<custos::test::FakeInnerProcessor> (5);
    REQUIRE (proc.loadInner (std::move (b)));     // swaps in b, then releaseResources()+destroys a's object
    REQUIRE (proc.boundParamCount() == 5);
    REQUIRE (releasesA == 1);                     // outgoing inner got releaseResources() before destruction
}

TEST_CASE ("clear removes the inner and processBlock outputs silence")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.prepareToPlay (48000.0, 64);
    proc.attachInner (std::make_unique<custos::test::FakeInnerProcessor> (2));
    REQUIRE (proc.hasInnerSynth());

    proc.clear();
    REQUIRE_FALSE (proc.hasInnerSynth());
    REQUIRE (proc.boundParamCount() == 0);

    juce::AudioBuffer<float> buffer (2, 64);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 1.0f, 64);
    juce::MidiBuffer midi;
    proc.processBlock (buffer, midi);
    REQUIRE (buffer.getSample (0, 0) == 0.0f);
    REQUIRE (buffer.getSample (1, 63) == 0.0f);
}

TEST_CASE ("load skips re-instantiation when the same synth is already loaded")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.prepareToPlay (48000.0, 64);

    int releases = 0;
    auto fake = std::make_unique<custos::test::FakeInnerProcessor> (2);
    fake->releaseSink = &releases;
    proc.loadInner (std::move (fake), "C:/x/Synth.vst3");
    REQUIRE (proc.currentPath() == "C:/x/Synth.vst3");

    // Same path -> must SKIP: no teardown, no re-instantiation, just re-ack.
    const auto same = proc.load ("C:/x/Synth.vst3");
    REQUIRE (same.ok);
    REQUIRE (same.message.startsWith ("already loaded"));
    REQUIRE (releases == 0);                 // the loaded inner was NOT released/swapped

    // Different path -> NOT skipped -> goes to the loader (bogus path fails), so not "already loaded".
    const auto other = proc.load ("C:/x/Other.vst3");
    REQUIRE_FALSE (other.message.startsWith ("already loaded"));
}
