#include <catch2/catch_test_macros.hpp>
#include "OscContract.h"
#include "CustosOscServer.h"

using namespace custos;

TEST_CASE ("oscPortForIdentity maps 1..15 to BASE+N and rejects the rest")
{
    REQUIRE (oscPortForIdentity (1)  == CUSTOS_OSC_PORT + 1);
    REQUIRE (oscPortForIdentity (15) == CUSTOS_OSC_PORT + 15);
    REQUIRE (oscPortForIdentity (0)  == 0);      // unassigned
    REQUIRE (oscPortForIdentity (16) == 0);      // out of range
    REQUIRE (oscPortForIdentity (-3) == 0);
}

TEST_CASE ("parseCommand maps /custos/hello")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/hello")).kind == Command::Hello);
}

TEST_CASE ("buildHere carries N first, then protoVer/mode/inner/count/port")
{
    const auto m = buildHere (3, "replace", "CS-80 V4", 2797, 9103);
    REQUIRE (m.getAddressPattern().toString() == "/custos/here");
    REQUIRE (m.size() == 6);
    REQUIRE (m[0].getInt32() == 3);
    REQUIRE (m[1].getInt32() == kProtoVersion);
    REQUIRE (m[2].getString() == "replace");
    REQUIRE (m[3].getString() == "CS-80 V4");
    REQUIRE (m[4].getInt32() == 2797);
    REQUIRE (m[5].getInt32() == 9103);
}

TEST_CASE ("buildAck and buildLoaded carry N first")
{
    const auto a = buildAck (5, "cleared");
    REQUIRE (a.getAddressPattern().toString() == "/custos/ack");
    REQUIRE (a[0].getInt32() == 5);
    REQUIRE (a[1].getString() == "cleared");

    const auto l = buildLoaded (5, "C:/x/Diva.vst3", 3124);
    REQUIRE (l.getAddressPattern().toString() == "/custos/loaded");
    REQUIRE (l[0].getInt32() == 5);
    REQUIRE (l[1].getString() == "C:/x/Diva.vst3");
    REQUIRE (l[2].getInt32() == 3124);
}
