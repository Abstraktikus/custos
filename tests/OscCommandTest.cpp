#include <catch2/catch_test_macros.hpp>
#include "CustosOscServer.h"
#include "CustosProcessor.h"

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

TEST_CASE ("parseCommand rejects /custos/midi/route with a non-int arg among 16")
{
    juce::OSCMessage m ("/custos/midi/route");
    for (int i = 0; i < 16; ++i) { if (i == 5) m.addFloat32 (1.0f); else m.addInt32 (1); }
    REQUIRE (parseCommand (m).kind == Command::Unknown);
}

TEST_CASE ("parseCommand maps /custos/midi/query")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/midi/query")).kind == Command::MidiQuery);
}

TEST_CASE ("emitMidiRoute sends the current map with N first")
{
    custos::CustosProcessor proc;
    proc.setIdentity (9);
    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    std::array<int, 16> r {}; for (int i = 0; i < 16; ++i) r[(size_t) i] = i == 7 ? 1 : 0;
    proc.setMidiRoute (r);
    proc.emitMidiRoute();

    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/midi/route");
    REQUIRE (sent[0].size() == 17);
    REQUIRE (sent[0][0].getInt32() == 9);
    REQUIRE (sent[0][8].getInt32() == 1);   // input ch 8 (arg index 8) -> out 1
}

TEST_CASE ("emitMainLR sends the current fold flag with N first")
{
    custos::CustosProcessor proc;
    proc.setIdentity (9);
    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.setMainLROnly (true);
    proc.emitMainLR();

    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/mainlr");
    REQUIRE (sent[0].size() == 2);
    REQUIRE (sent[0][0].getInt32() == 9);
    REQUIRE (sent[0][1].getInt32() == 1);
}

TEST_CASE ("parseCommand maps /custos/instrument/next and /prev")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/instrument/next")).kind == Command::BrowseNext);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/instrument/prev")).kind == Command::BrowsePrev);
}

TEST_CASE ("parseCommand reads optional scope on /custos/instrument/next|prev")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/instrument/next")).scope == 0);      // default
    juce::OSCMessage all ("/custos/instrument/next"); all.addInt32 (1);
    const auto c = parseCommand (all);
    REQUIRE (c.kind == Command::BrowseNext);
    REQUIRE (c.scope == 1);
}

TEST_CASE ("parseCommand maps /custos/instrument/set with an index")
{
    juce::OSCMessage msg ("/custos/instrument/set", (juce::int32) 4);
    const auto c = parseCommand (msg);
    REQUIRE (c.kind == Command::BrowseSet);
    REQUIRE (c.count == 4);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/instrument/set")).kind == Command::Unknown);   // missing arg
}

TEST_CASE ("parseCommand maps /custos/instrument/load with a name")
{
    juce::OSCMessage m ("/custos/instrument/load", juce::String ("Analog Lab V"));
    const auto c = parseCommand (m);
    REQUIRE (c.kind == Command::InstrumentLoad);
    REQUIRE (c.path == "Analog Lab V");
}

TEST_CASE ("parseCommand maps /custos/window titled")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/window", juce::String ("titled"))).kind == Command::WindowTitled);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/window", juce::String ("show"))).kind   == Command::WindowShow);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/window", juce::String ("hide"))).kind   == Command::WindowHide);
}

TEST_CASE ("parseCommand maps preset verbs")
{
    {
        juce::OSCMessage m ("/custos/preset/save"); m.addString ("Warm Pad");
        const auto c = parseCommand (m);
        REQUIRE (c.kind == Command::PresetSave);
        REQUIRE (c.presetName == "Warm Pad");
    }
    {
        juce::OSCMessage m ("/custos/preset/setroot"); m.addString ("C:/Rig/CustosPresets");
        REQUIRE (parseCommand (m).kind == Command::PresetSetRoot);
        REQUIRE (parseCommand (m).rootPath == "C:/Rig/CustosPresets");
    }
    {
        juce::OSCMessage m ("/custos/preset/set"); m.addInt32 (3);
        const auto c = parseCommand (m);
        REQUIRE (c.kind == Command::PresetSet);
        REQUIRE (c.presetIndex == 3);
    }
    {
        juce::OSCMessage m ("/custos/preset/load"); m.addString ("Lead");
        const auto c = parseCommand (m);
        REQUIRE (c.kind == Command::PresetLoad);
        REQUIRE (c.presetName == "Lead");
        REQUIRE (c.presetIndex == -1);
    }
    {
        juce::OSCMessage m ("/custos/preset/load"); m.addInt32 (2);
        const auto c = parseCommand (m);
        REQUIRE (c.kind == Command::PresetLoad);
        REQUIRE (c.presetIndex == 2);
    }
    {
        juce::OSCMessage m ("/custos/preset/rename"); m.addString ("Old"); m.addString ("New");
        const auto c = parseCommand (m);
        REQUIRE (c.kind == Command::PresetRename);
        REQUIRE (c.presetName == "Old");
        REQUIRE (c.presetNewName == "New");
    }
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/preset/next")).kind == Command::PresetNext);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/preset/prev")).kind == Command::PresetPrev);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/preset/list")).kind == Command::PresetList);
    { juce::OSCMessage m ("/custos/preset/delete"); m.addString ("Gone");
      REQUIRE (parseCommand (m).kind == Command::PresetDelete); }
}

TEST_CASE ("parseCommand maps /custos/mainlr")
{
    { juce::OSCMessage m ("/custos/mainlr"); m.addInt32 (1);
      const auto c = parseCommand (m); REQUIRE (c.kind == Command::MainLR); REQUIRE (c.mainLROn); }
    { juce::OSCMessage m ("/custos/mainlr"); m.addInt32 (0);
      const auto c = parseCommand (m); REQUIRE (c.kind == Command::MainLR); REQUIRE_FALSE (c.mainLROn); }
}

TEST_CASE ("parseCommand maps /custos/mainlr/query")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/mainlr/query")).kind == Command::MainLRQuery);
}

TEST_CASE ("parseCommand reads controlType/paramDown/paramUp on /custos/favorite")
{
    juce::OSCMessage m ("/custos/favorite");
    m.addInt32 (0); m.addString ("Analog Lab V"); m.addString ("C:/AL.vst3");
    m.addInt32 (3); m.addFloat32 (-3.0f); m.addString ("Arturia"); m.addInt32 (500);
    m.addString ("PARAM"); m.addInt32 (498); m.addInt32 (499);
    const auto c = parseCommand (m);
    REQUIRE (c.kind == Command::FavEntry);
    REQUIRE (c.fav.controlType == "PARAM");
    REQUIRE (c.fav.paramDown == 498);
    REQUIRE (c.fav.paramUp  == 499);
}

TEST_CASE ("parseCommand keeps v2 /custos/favorite (5 args) working")
{
    juce::OSCMessage m ("/custos/favorite");
    m.addInt32 (0); m.addString ("X"); m.addString ("C:/x.vst3"); m.addInt32 (1); m.addFloat32 (0.0f);
    const auto c = parseCommand (m);
    REQUIRE (c.kind == Command::FavEntry);
    REQUIRE (c.fav.controlType == "PRESET");   // struct default
}

TEST_CASE ("parseCommand maps /custos/patch/next and /custos/patch/prev")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/patch/next")).kind == Command::PatchNext);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/patch/prev")).kind == Command::PatchPrev);
}

TEST_CASE ("parseCommand maps /custos/learn/start and /custos/learn/stop")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/learn/start")).kind == Command::LearnStart);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/learn/stop")).kind  == Command::LearnStop);
}

TEST_CASE ("parseCommand maps /custos/preset/queryroot")
{
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/preset/queryroot")).kind
             == Command::PresetQueryRoot);
}
