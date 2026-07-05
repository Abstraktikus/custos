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

TEST_CASE ("buildParam and buildParamsDone carry N first")
{
    const auto p = buildParam (7, 5, 0.5f, "Cutoff");
    REQUIRE (p.getAddressPattern().toString() == "/custos/param");
    REQUIRE (p[0].getInt32() == 7);
    REQUIRE (p[1].getInt32() == 5);
    REQUIRE (p[3].getString() == "Cutoff");

    const auto d = buildParamsDone (7, 0, 3);
    REQUIRE (d.getAddressPattern().toString() == "/custos/params/done");
    REQUIRE (d[0].getInt32() == 7);
    REQUIRE (d[1].getInt32() == 0);
    REQUIRE (d[2].getInt32() == 3);
}

TEST_CASE ("parseCommand maps /custos/params with start and count")
{
    juce::OSCMessage msg ("/custos/params", 10, 50);
    const auto c = parseCommand (msg);
    REQUIRE (c.kind == Command::Params);
    REQUIRE (c.start == 10);
    REQUIRE (c.count == 50);
}

TEST_CASE ("parseCommand rejects /custos/params without two ints")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/params")).kind == Command::Unknown);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/params", 10)).kind == Command::Unknown);
}

TEST_CASE ("parseCommand maps /custos/volume with a dB float")
{
    juce::OSCMessage msg ("/custos/volume", -6.0f);
    const auto c = parseCommand (msg);
    REQUIRE (c.kind == Command::Volume);
    REQUIRE (c.gainDb == -6.0f);   // -6.0f is exactly representable
}

TEST_CASE ("parseCommand rejects /custos/volume without a float")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/volume")).kind == Command::Unknown);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/volume", 3)).kind == Command::Unknown);  // int, not float
}

TEST_CASE ("parseCommand maps the favourites push")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/favorites/begin")).kind == Command::FavBegin);

    juce::OSCMessage entry ("/custos/favorite", 0, juce::String ("Diva"),
                            juce::String ("C:/x/Diva.vst3"), 3, -2.0f, juce::String ("u-he"));
    const auto e = parseCommand (entry);
    REQUIRE (e.kind == Command::FavEntry);
    REQUIRE (e.fav.name == "Diva");
    REQUIRE (e.fav.path == "C:/x/Diva.vst3");
    REQUIRE (e.fav.favOrder == 3);
    REQUIRE (e.fav.gainDb == -2.0f);
    REQUIRE (e.fav.brand == "u-he");

    // brand is optional (back-compat): a 5-arg entry parses with an empty brand.
    juce::OSCMessage noBrand ("/custos/favorite", 0, juce::String ("Old"),
                              juce::String ("C:/x/Old.vst3"), 1, 0.0f);
    const auto nb = parseCommand (noBrand);
    REQUIRE (nb.kind == Command::FavEntry);
    REQUIRE (nb.fav.brand.isEmpty());

    const auto end = parseCommand (juce::OSCMessage ("/custos/favorites/end", 12));
    REQUIRE (end.kind == Command::FavEnd);
    REQUIRE (end.count == 12);
}

TEST_CASE ("parseCommand rejects a malformed favourite entry")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/favorite", 0, juce::String ("x"))).kind == Command::Unknown);
}

TEST_CASE ("parseCommand maps /custos/window show and hide")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/window", juce::String ("show"))).kind == Command::WindowShow);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/window", juce::String ("hide"))).kind == Command::WindowHide);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/window", juce::String ("bogus"))).kind == Command::Unknown);
}

TEST_CASE ("parseCommand maps /custos/window/rect with five ints")
{
    const auto c = parseCommand (juce::OSCMessage ("/custos/window/rect", 100, 200, 640, 480, 1));
    REQUIRE (c.kind == Command::WindowRect);
    REQUIRE (c.rx == 100);
    REQUIRE (c.ry == 200);
    REQUIRE (c.rw == 640);
    REQUIRE (c.rh == 480);
    REQUIRE (c.movable == true);

    REQUIRE (parseCommand (juce::OSCMessage ("/custos/window/rect", 1, 2, 3)).kind == Command::Unknown);
}
