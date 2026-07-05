#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"
#include <vector>

using namespace custos;

TEST_CASE ("dumpParams streams bound params then a done marker, clamped to boundCount")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setIdentity (7);                                                       // no OSC bind (enableOsc=false)
    proc.attachInner (std::make_unique<custos::test::FakeInnerProcessor> (3));  // boundCount = 3

    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.dumpParams (0, 10);   // over-request -> clamps to [0,3)

    REQUIRE (sent.size() == 4);                                     // 3 params + 1 done
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/param");
    REQUIRE (sent[0].size() == 7);                                 // N, idx, val, name, defaultVal, numSteps, label
    REQUIRE (sent[0][0].getInt32() == 7);                          // N
    REQUIRE (sent[0][1].getInt32() == 0);                          // idx
    REQUIRE (sent[0][3].getString() == "Fake 0");                  // name (mirrored from the fake inner)
    REQUIRE (sent[0][5].getInt32() >= 0);                          // numSteps present
    REQUIRE (sent[3].getAddressPattern().toString() == "/custos/params/done");
    REQUIRE (sent[3][0].getInt32() == 7);                          // N
    REQUIRE (sent[3][1].getInt32() == 0);                          // start echoed
    REQUIRE (sent[3][2].getInt32() == 3);                          // actually sent
}

TEST_CASE ("dumpParams past boundCount sends only the done marker with count 0")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setIdentity (7);
    proc.attachInner (std::make_unique<custos::test::FakeInnerProcessor> (3));

    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.dumpParams (5, 4);    // start >= boundCount
    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/params/done");
    REQUIRE (sent[0][1].getInt32() == 5);   // start echoed
    REQUIRE (sent[0][2].getInt32() == 0);   // nothing sent
}

TEST_CASE ("dumpParams honours a sub-range")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setIdentity (2);
    proc.attachInner (std::make_unique<custos::test::FakeInnerProcessor> (5));  // boundCount = 5

    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.dumpParams (1, 2);    // [1,3)
    REQUIRE (sent.size() == 3);                          // 2 params + done
    REQUIRE (sent[0][1].getInt32() == 1);
    REQUIRE (sent[1][1].getInt32() == 2);
    REQUIRE (sent[2].getAddressPattern().toString() == "/custos/params/done");
    REQUIRE (sent[2][2].getInt32() == 2);                // sent
}
