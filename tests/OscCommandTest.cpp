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
