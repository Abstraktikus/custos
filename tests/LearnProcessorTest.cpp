#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"
#include <vector>

using namespace custos;

TEST_CASE ("startLearn emits /custos/learn/started with N")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setIdentity (4);
    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.startLearn();

    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/learn/started");
    REQUIRE (sent[0][0].getInt32() == 4);
    REQUIRE (proc.learnActiveForTest());

    proc.stopLearn ("stop");   // stop timers before the processor is torn down
}

TEST_CASE ("stopLearn emits /custos/learn/stopped with the reason")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setIdentity (4);
    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.startLearn();
    sent.clear();
    proc.stopLearn ("stop");

    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/learn/stopped");
    REQUIRE (sent[0][0].getInt32() == 4);
    REQUIRE (sent[0][1].getString() == "stop");
    REQUIRE_FALSE (proc.learnActiveForTest());
}

TEST_CASE ("stopLearn is a no-op when the window was never opened")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setIdentity (4);
    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.stopLearn ("stop");
    REQUIRE (sent.empty());
}

TEST_CASE ("re-entrant startLearn re-emits started and stays a single window")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.setIdentity (4);
    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.startLearn();
    proc.startLearn();

    REQUIRE (sent.size() == 2);              // started emitted each time (idempotent for KM UI)
    REQUIRE (proc.learnActiveForTest());

    sent.clear();
    proc.stopLearn ("stop");
    REQUIRE (sent.size() == 1);              // single window -> one stopped
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/learn/stopped");
}
