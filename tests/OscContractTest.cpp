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

TEST_CASE ("buildHere carries N, protoVer, mode, inner, count, port, facadeCap")
{
    const auto m = buildHere (3, "replace", "CS-80 V4", 2797, 9103, 5000);
    REQUIRE (m.getAddressPattern().toString() == "/custos/here");
    REQUIRE (m.size() == 7);
    REQUIRE (m[4].getInt32() == 2797);
    REQUIRE (m[5].getInt32() == 9103);
    REQUIRE (m[6].getInt32() == 5000);   // facadeCap
}

TEST_CASE ("buildLoaded carries N, path, boundCount, innerTotal")
{
    const auto l = buildLoaded (5, "C:/x/Diva.vst3", 3124, 9035);
    REQUIRE (l.getAddressPattern().toString() == "/custos/loaded");
    REQUIRE (l.size() == 4);
    REQUIRE (l[2].getInt32() == 3124);
    REQUIRE (l[3].getInt32() == 9035);   // innerTotal (full inner param count)
}

TEST_CASE ("buildParam carries N, idx, val, name, defaultVal, numSteps, label")
{
    const auto p = buildParam (7, 5, 0.5f, "Cutoff", 0.25f, 128, "Hz");
    REQUIRE (p.getAddressPattern().toString() == "/custos/param");
    REQUIRE (p.size() == 7);
    REQUIRE (p[1].getInt32()  == 5);
    REQUIRE (p[3].getString() == "Cutoff");
    REQUIRE (p[4].getFloat32() == 0.25f);
    REQUIRE (p[5].getInt32()  == 128);
    REQUIRE (p[6].getString() == "Hz");
}

TEST_CASE ("buildParamsDone carries N first")
{
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

TEST_CASE ("gpMirrorsFeedback mirrors browse/loaded/here + error-acks, not success chatter")
{
    // GP-Script drives Voice-Selector + direct load autonomously -> it needs these:
    REQUIRE (gpMirrorsFeedback ("/custos/browsing", {}));
    REQUIRE (gpMirrorsFeedback ("/custos/loaded",   {}));
    REQUIRE (gpMirrorsFeedback ("/custos/here",      {}));           // liveness/discovery
    REQUIRE (gpMirrorsFeedback ("/custos/ack", "error no such file")); // load FAILURE

    // ...but NOT success acks (success already conveyed by /custos/loaded):
    REQUIRE (! gpMirrorsFeedback ("/custos/ack", "loaded C:/x.vst3 count=1234"));
    REQUIRE (! gpMirrorsFeedback ("/custos/ack", "cleared"));
    REQUIRE (! gpMirrorsFeedback ("/custos/ack", "mode replace (applies after reload)"));

    // ...and NOT the param-dump flood or other KM-hub-only feedback:
    REQUIRE (! gpMirrorsFeedback ("/custos/param",        {}));
    REQUIRE (! gpMirrorsFeedback ("/custos/params/done",  {}));
    REQUIRE (! gpMirrorsFeedback ("/custos/window/rect",  {}));
    REQUIRE (! gpMirrorsFeedback ("/custos/midi/route",   {}));
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

TEST_CASE ("parseCommand maps /custos/window/rect optional clamp arg")
{
    const auto c = parseCommand (juce::OSCMessage ("/custos/window/rect", 1, 2, 3, 4, 0, 1));
    REQUIRE (c.kind == Command::WindowRect);
    REQUIRE (c.clamp == true);

    const auto c2 = parseCommand (juce::OSCMessage ("/custos/window/rect", 1, 2, 3, 4, 0));
    REQUIRE (c2.kind == Command::WindowRect);
    REQUIRE (c2.clamp == false);   // omitted -> no clamp (live)
}

TEST_CASE ("buildWindowRect carries N first, then rect and movable")
{
    const auto m = buildWindowRect (9, 100, 200, 640, 480, true);
    REQUIRE (m.getAddressPattern().toString() == "/custos/window/rect");
    REQUIRE (m.size() == 6);
    REQUIRE (m[0].getInt32() == 9);
    REQUIRE (m[1].getInt32() == 100);
    REQUIRE (m[2].getInt32() == 200);
    REQUIRE (m[3].getInt32() == 640);
    REQUIRE (m[4].getInt32() == 480);
    REQUIRE (m[5].getInt32() == 1);
}

TEST_CASE ("buildMidiRoute carries N first, then 16 targets")
{
    std::array<int, 16> r {}; for (int i = 0; i < 16; ++i) r[(size_t) i] = i + 1;
    const auto m = buildMidiRoute (9, r);
    REQUIRE (m.getAddressPattern().toString() == "/custos/midi/route");
    REQUIRE (m.size() == 17);
    REQUIRE (m[0].getInt32() == 9);
    REQUIRE (m[1].getInt32() == 1);
    REQUIRE (m[16].getInt32() == 16);
}

TEST_CASE ("buildMainLR carries N first, then the fold flag")
{
    const auto on = buildMainLR (9, true);
    REQUIRE (on.getAddressPattern().toString() == "/custos/mainlr");
    REQUIRE (on.size() == 2);
    REQUIRE (on[0].getInt32() == 9);
    REQUIRE (on[1].getInt32() == 1);

    const auto off = buildMainLR (9, false);
    REQUIRE (off[1].getInt32() == 0);
}

TEST_CASE ("proto version is 3")
{
    REQUIRE (custos::kProtoVersion == 3);
}

TEST_CASE ("GP mirrors preset recall feedback")
{
    REQUIRE (gpMirrorsFeedback ("/custos/preset/browsing", {}));
    REQUIRE (gpMirrorsFeedback ("/custos/preset/loaded", {}));
    REQUIRE (gpMirrorsFeedback ("/custos/preset/error", {}));
    REQUIRE_FALSE (gpMirrorsFeedback ("/custos/preset/saved", {}));   // management noise stays hub-only
    REQUIRE_FALSE (gpMirrorsFeedback ("/custos/preset/list", {}));
}

TEST_CASE ("buildPatchStepped carries n, controlType, detail")
{
    const auto m = custos::buildPatchStepped (2, "PC", "17");
    REQUIRE (m.getAddressPattern().toString() == "/custos/patch/stepped");
    REQUIRE (m[0].getInt32()  == 2);
    REQUIRE (m[1].getString() == "PC");
    REQUIRE (m[2].getString() == "17");
}

TEST_CASE ("gpMirrorsFeedback mirrors /custos/patch/stepped")
{
    REQUIRE (custos::gpMirrorsFeedback ("/custos/patch/stepped", {}) == true);
}
