#include <catch2/catch_test_macros.hpp>
#include "CustosOscServer.h"

using namespace custos;

TEST_CASE ("parseCommand maps /custos/load with a path")
{
    juce::OSCMessage msg ("/custos/load", juce::String ("C:/x/Diva.vst3"));
    const auto c = parseCommand (msg);
    REQUIRE (c.kind == Command::Load);
    REQUIRE (c.path == "C:/x/Diva.vst3");
}

TEST_CASE ("parseCommand maps /custos/clear")
{
    juce::OSCMessage msg ("/custos/clear");
    REQUIRE (parseCommand (msg).kind == Command::Clear);
}

TEST_CASE ("parseCommand rejects load without a string arg and unknown addresses")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/load")).kind == Command::Unknown);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/load", 42)).kind == Command::Unknown);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/bogus")).kind == Command::Unknown);
}

TEST_CASE ("parseCommand maps /custos/midi/route with 16 ints")
{
    juce::OSCMessage m ("/custos/midi/route");
    for (int i = 1; i <= 16; ++i) m.addInt32 (i);   // identity
    const auto c = parseCommand (m);
    REQUIRE (c.kind == Command::MidiRoute);
    REQUIRE (c.route[0] == 1);
    REQUIRE (c.route[15] == 16);

    juce::OSCMessage remap ("/custos/midi/route");
    for (int i = 0; i < 16; ++i) remap.addInt32 (i == 7 ? 1 : 0);   // input8 -> out1, rest dropped
    REQUIRE (parseCommand (remap).route[7] == 1);
}

TEST_CASE ("parseCommand rejects /custos/midi/route without 16 ints")
{
    juce::OSCMessage m ("/custos/midi/route");
    for (int i = 0; i < 15; ++i) m.addInt32 (1);
    REQUIRE (parseCommand (m).kind == Command::Unknown);
}

TEST_CASE ("parseCommand maps /custos/midi/query")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/midi/query")).kind == Command::MidiQuery);
}
