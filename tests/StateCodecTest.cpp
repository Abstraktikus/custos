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

static const std::array<std::uint8_t, 16> identityRoute { {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16} };

TEST_CASE ("serializeState/parseState round-trips path + inner chunk")
{
    const juce::String path = "C:/Program Files/Common Files/VST3/CS-80 V4.vst3";
    const auto inner = mbFromString ("inner-state-bytes");

    const auto blob = serializeState (path, inner, 0, identityRoute);
    PersistedState out;
    REQUIRE (parseState (blob.getData(), (int) blob.getSize(), out));
    REQUIRE (out.path == path);
    REQUIRE (out.innerState.getSize() == inner.getSize());
    REQUIRE (out.innerState == inner);
}

TEST_CASE ("serializeState/parseState handles the no-synth (empty) case")
{
    const auto blob = serializeState ({}, {}, 0, identityRoute);
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
    const auto blob = serializeState ("C:/x/Diva.vst3", {}, 7, identityRoute);
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

TEST_CASE ("state v3 round-trips the MIDI route map")
{
    std::array<std::uint8_t, 16> r {}; for (int i = 0; i < 16; ++i) r[(size_t) i] = (std::uint8_t) (16 - i);
    juce::MemoryBlock inner; inner.append ("xyz", 3);
    const auto blob = custos::serializeState ("C:/x/Diva.vst3", inner, 7, r);

    custos::PersistedState ps;
    REQUIRE (custos::parseState (blob.getData(), (int) blob.getSize(), ps));
    REQUIRE (ps.identityN == 7);
    REQUIRE (ps.route == r);
}

TEST_CASE ("v2 blob parses with identity route (back-compat)")
{
    // Build a v2 blob by hand: CUS1, ver=2, path, inner, identityN — no route bytes.
    juce::MemoryBlock mb; juce::MemoryOutputStream os (mb, false);
    os.write ("CUS1", 4); os.writeByte (2);
    const char* p = "C:/a.vst3"; const int pl = (int) std::strlen (p);
    os.writeInt (pl); os.write (p, (size_t) pl);
    os.writeInt (0); os.writeInt (4); os.flush();   // empty inner, identityN=4

    custos::PersistedState ps;
    REQUIRE (custos::parseState (mb.getData(), (int) mb.getSize(), ps));
    REQUIRE (ps.identityN == 4);
    for (int i = 0; i < 16; ++i) REQUIRE (ps.route[(size_t) i] == (std::uint8_t) (i + 1));
}
