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

TEST_CASE ("parseCommand maps /custos/instrument/set with an index")
{
    juce::OSCMessage msg ("/custos/instrument/set", (juce::int32) 4);
    const auto c = parseCommand (msg);
    REQUIRE (c.kind == Command::BrowseSet);
    REQUIRE (c.count == 4);
    REQUIRE (parseCommand (juce::OSCMessage ("/custos/instrument/set")).kind == Command::Unknown);   // missing arg
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
