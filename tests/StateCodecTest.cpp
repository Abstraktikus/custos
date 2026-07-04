#include <catch2/catch_test_macros.hpp>
#include "StateCodec.h"
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"

using namespace custos;

static juce::MemoryBlock mbFromString (const juce::String& s)
{
    juce::MemoryBlock mb;
    mb.replaceAll (s.toRawUTF8(), s.getNumBytesAsUTF8());
    return mb;
}

TEST_CASE ("serializeState/parseState round-trips path + inner chunk")
{
    const juce::String path = "C:/Program Files/Common Files/VST3/CS-80 V4.vst3";
    const auto inner = mbFromString ("inner-state-bytes");

    const auto blob = serializeState (path, inner);
    PersistedState out;
    REQUIRE (parseState (blob.getData(), (int) blob.getSize(), out));
    REQUIRE (out.path == path);
    REQUIRE (out.innerState.getSize() == inner.getSize());
    REQUIRE (out.innerState == inner);
}

TEST_CASE ("serializeState/parseState handles the no-synth (empty) case")
{
    const auto blob = serializeState ({}, {});
    PersistedState out;
    REQUIRE (parseState (blob.getData(), (int) blob.getSize(), out));
    REQUIRE (out.path.isEmpty());
    REQUIRE (out.innerState.getSize() == 0);
}

TEST_CASE ("parseState rejects a foreign / truncated blob")
{
    PersistedState out;
    const char junk[] = "not-a-custos-state";
    REQUIRE_FALSE (parseState (junk, (int) sizeof (junk), out));
    REQUIRE_FALSE (parseState (nullptr, 0, out));
}

TEST_CASE ("getStateInformation embeds the inner synth's chunk")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.attachInner (std::make_unique<custos::test::FakeInnerProcessor> (2));   // stateMarker "fake-state"

    juce::MemoryBlock blob;
    proc.getStateInformation (blob);
    PersistedState out;
    REQUIRE (parseState (blob.getData(), (int) blob.getSize(), out));
    REQUIRE (out.path.isEmpty());   // no real load() ran, so currentSynthPath is empty
    REQUIRE (juce::String::fromUTF8 ((const char*) out.innerState.getData(),
                                     (int) out.innerState.getSize()) == "fake-state");
}
