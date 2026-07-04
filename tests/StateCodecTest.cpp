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

    const auto blob = serializeState (path, inner, 0);
    PersistedState out;
    REQUIRE (parseState (blob.getData(), (int) blob.getSize(), out));
    REQUIRE (out.path == path);
    REQUIRE (out.innerState.getSize() == inner.getSize());
    REQUIRE (out.innerState == inner);
}

TEST_CASE ("serializeState/parseState handles the no-synth (empty) case")
{
    const auto blob = serializeState ({}, {}, 0);
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

TEST_CASE ("serializeState/parseState round-trips identity N (v2)")
{
    const auto blob = serializeState ("C:/x/Diva.vst3", {}, 7);
    PersistedState out;
    REQUIRE (parseState (blob.getData(), (int) blob.getSize(), out));
    REQUIRE (out.path == "C:/x/Diva.vst3");
    REQUIRE (out.identityN == 7);
}

TEST_CASE ("parseState reads a v1 blob with identityN defaulting to 0")
{
    // A hand-built v1 blob: "CUS1" + version 1 + empty path + empty inner (no N field).
    juce::MemoryBlock mb;
    juce::MemoryOutputStream os (mb, false);
    os.write ("CUS1", 4);
    os.writeByte (1);
    os.writeInt (0);   // pathLen
    os.writeInt (0);   // innerLen
    os.flush();

    PersistedState out;
    REQUIRE (parseState (mb.getData(), (int) mb.getSize(), out));
    REQUIRE (out.path.isEmpty());
    REQUIRE (out.identityN == 0);
}
