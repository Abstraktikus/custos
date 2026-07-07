#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"

using namespace custos;

TEST_CASE ("innerSynthKey is empty with no synth and non-empty once loaded")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    REQUIRE (proc.innerSynthKey().isEmpty());
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());
    REQUIRE (proc.innerSynthKey().isNotEmpty());   // falls back to the synth name for a non-plugin inner
}

TEST_CASE ("capture and restore inner state round-trips through the fake")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    auto fake = std::make_unique<test::FakeInnerProcessor>();
    fake->stateMarker = "abc";
    auto* raw = fake.get();
    proc.attachInner (std::move (fake));

    const auto captured = proc.captureInnerState();
    REQUIRE (captured.matches ("abc", 3));
    REQUIRE (proc.restoreInnerState (captured));
    REQUIRE (raw->restoredMarker == "abc");
}

TEST_CASE ("save writes a file and emits saved with identity first")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc;
    proc.setIdentity (7);
    proc.setPresetRoot (root.getFullPathName());
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());

    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };

    const int idx = proc.savePreset ("Warm Pad");
    REQUIRE (idx == 0);
    REQUIRE (proc.listPresets() == std::vector<juce::String> { "Warm Pad" });
    REQUIRE (msgs.size() == 1);
    REQUIRE (msgs[0].getAddressPattern().toString() == "/custos/preset/saved");
    REQUIRE ((int) msgs[0][0].getInt32() == 7);
    root.deleteRecursively();
}

TEST_CASE ("save with no synth loaded emits error, writes nothing")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc;
    proc.setIdentity (3);
    proc.setPresetRoot (root.getFullPathName());
    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };

    REQUIRE (proc.savePreset ("X") == -1);
    REQUIRE (msgs.size() == 1);
    REQUIRE (msgs[0].getAddressPattern().toString() == "/custos/preset/error");
    root.deleteRecursively();
}

TEST_CASE ("load restores inner state and emits loaded")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc;
    proc.setIdentity (2);
    proc.setPresetRoot (root.getFullPathName());
    auto fake = std::make_unique<test::FakeInnerProcessor>();
    fake->stateMarker = "saved-sound";
    auto* raw = fake.get();
    proc.attachInner (std::move (fake));
    proc.savePreset ("Lead");

    raw->restoredMarker = "";           // clear
    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };
    REQUIRE (proc.loadPresetByName ("Lead"));
    REQUIRE (raw->restoredMarker == "saved-sound");
    REQUIRE (msgs.back().getAddressPattern().toString() == "/custos/preset/loaded");

    REQUIRE_FALSE (proc.loadPresetByName ("Nope"));   // missing -> error
    REQUIRE (msgs.back().getAddressPattern().toString() == "/custos/preset/error");
    root.deleteRecursively();
}
