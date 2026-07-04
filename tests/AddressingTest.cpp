#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"
#include "StateCodec.h"
#include "FakeInnerProcessor.h"
#include <vector>

using namespace custos;

TEST_CASE ("setIdentity stores N and (without OSC) reports it")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;                 // enableOsc = false -> no bind
    REQUIRE (proc.identity() == 0);        // unassigned by default
    proc.setIdentity (4);
    REQUIRE (proc.identity() == 4);
}

TEST_CASE ("getStateInformation/setStateInformation round-trip identity N")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor a;
    a.setIdentity (9);
    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    CustosProcessor b;
    b.setStateInformation (blob.getData(), (int) blob.getSize());
    REQUIRE (b.identity() == 9);
}

TEST_CASE ("loadInner emits /custos/loaded with N, path and boundCount")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.prepareToPlay (48000.0, 64);
    proc.setIdentity (3);

    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    REQUIRE (proc.loadInner (std::make_unique<custos::test::FakeInnerProcessor> (5), "C:/x/Diva.vst3"));

    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/loaded");
    REQUIRE (sent[0][0].getInt32() == 3);
    REQUIRE (sent[0][1].getString() == "C:/x/Diva.vst3");
    REQUIRE (sent[0][2].getInt32() == 5);
}

TEST_CASE ("clear emits /custos/loaded with empty path and boundCount 0")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.prepareToPlay (48000.0, 64);
    proc.setIdentity (3);
    proc.attachInner (std::make_unique<custos::test::FakeInnerProcessor> (2));

    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.clear();
    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0][0].getInt32() == 3);
    REQUIRE (sent[0][1].getString().isEmpty());
    REQUIRE (sent[0][2].getInt32() == 0);
}
