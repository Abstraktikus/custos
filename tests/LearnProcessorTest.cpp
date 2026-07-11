#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"
#include <vector>

using namespace custos;
using Catch::Approx;

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

namespace {
// Build a loaded, learning processor. Caller must have a live juce::ScopedJuceInitialiser_GUI in scope.
// boundCount = 5 (FakeInnerProcessor with 5 params). Sink is set AFTER attach so the /custos/loaded
// emit is not captured; the started ack is cleared before returning.
std::unique_ptr<CustosProcessor> makeLoadedLearning (std::vector<juce::OSCMessage>& sent)
{
    auto proc = std::make_unique<CustosProcessor>();
    proc->prepareToPlay (48000.0, 64);
    proc->setIdentity (4);
    proc->attachInner (std::make_unique<custos::test::FakeInnerProcessor> (5));
    proc->outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };
    proc->startLearn();
    sent.clear();
    return proc;
}
} // namespace

TEST_CASE ("learnRecord is ignored while the window is closed")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.prepareToPlay (48000.0, 64);
    proc.setIdentity (4);
    proc.attachInner (std::make_unique<custos::test::FakeInnerProcessor> (5));
    std::vector<juce::OSCMessage> sent;
    proc.outboundSink = [&sent] (const juce::OSCMessage& m) { sent.push_back (m); };

    proc.learnRecord (2, 0.8f);   // not learning -> dropped
    proc.drainLearn();
    REQUIRE (sent.empty());
}

TEST_CASE ("drainLearn coalesces to the latest value per index and names it")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::vector<juce::OSCMessage> sent;
    auto proc = makeLoadedLearning (sent);

    proc->learnRecord (2, 0.1f);
    proc->learnRecord (2, 0.5f);
    proc->learnRecord (2, 0.9f);
    proc->drainLearn();

    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/learn/moved");
    REQUIRE (sent[0][0].getInt32() == 4);
    REQUIRE (sent[0][1].getInt32() == 2);
    REQUIRE (sent[0][2].getFloat32() == Approx (0.9f));
    REQUIRE (sent[0][3].getString() == "Fake 2");

    proc->stopLearn ("stop");
}

TEST_CASE ("drainLearn drops sub-deadband changes after the first emit")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::vector<juce::OSCMessage> sent;
    auto proc = makeLoadedLearning (sent);

    proc->learnRecord (2, 0.5f);
    proc->drainLearn();
    REQUIRE (sent.size() == 1);          // first move always emits
    sent.clear();

    proc->learnRecord (2, 0.5005f);      // < 0.001 from last emitted -> dropped
    proc->drainLearn();
    REQUIRE (sent.empty());

    proc->learnRecord (2, 0.7f);         // > deadband -> emits
    proc->drainLearn();
    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0][2].getFloat32() == Approx (0.7f));

    proc->stopLearn ("stop");
}

TEST_CASE ("drainLearn ignores indices at or beyond boundCount")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::vector<juce::OSCMessage> sent;
    auto proc = makeLoadedLearning (sent);

    proc->learnRecord (5, 0.9f);         // == boundCount (unbound tail)
    proc->learnRecord (99, 0.9f);
    proc->drainLearn();
    REQUIRE (sent.empty());

    proc->learnRecord (4, 0.9f);         // last bound index
    proc->drainLearn();
    REQUIRE (sent.size() == 1);
    REQUIRE (sent[0][1].getInt32() == 4);

    proc->stopLearn ("stop");
}

TEST_CASE ("stopLearn flushes queued moves before the stopped ack")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::vector<juce::OSCMessage> sent;
    auto proc = makeLoadedLearning (sent);

    proc->learnRecord (3, 0.6f);         // queued, not yet drained
    proc->stopLearn ("stop");

    REQUIRE (sent.size() == 2);
    REQUIRE (sent[0].getAddressPattern().toString() == "/custos/learn/moved");
    REQUIRE (sent[0][1].getInt32() == 3);
    REQUIRE (sent[1].getAddressPattern().toString() == "/custos/learn/stopped");
    REQUIRE (sent[1][1].getString() == "stop");
}
