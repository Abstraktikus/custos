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

TEST_CASE ("presetSet loads immediately by index and emits loaded")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc;
    proc.setIdentity (1);
    proc.setPresetRoot (root.getFullPathName());
    auto fake = std::make_unique<test::FakeInnerProcessor>();
    auto* raw = fake.get();
    proc.attachInner (std::move (fake));
    proc.savePreset ("Apple");   // idx 0
    proc.savePreset ("Banana");  // idx 1

    raw->stateMarker = "";       // savePreset captured "fake-state" for both; markers identical, so assert via emission
    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };

    proc.presetSet (1);
    REQUIRE (msgs.back().getAddressPattern().toString() == "/custos/preset/loaded");
    REQUIRE (msgs.back()[1].getString() == "Banana");
}

TEST_CASE ("presetNext/prev step the cursor with wrap and preview browsing")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc;
    proc.setIdentity (1);
    proc.setPresetRoot (root.getFullPathName());
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());
    proc.savePreset ("Apple");   // idx 0
    proc.savePreset ("Banana");  // idx 1

    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };

    proc.presetNext();   // from unset -> idx 0 preview
    REQUIRE (msgs.back().getAddressPattern().toString() == "/custos/preset/browsing");
    REQUIRE (msgs.back()[1].getString() == "Apple");
    proc.presetNext();   // -> idx 1
    REQUIRE (msgs.back()[1].getString() == "Banana");
    proc.presetNext();   // wrap -> idx 0
    REQUIRE (msgs.back()[1].getString() == "Apple");
    proc.presetPrev();   // wrap back -> idx 1
    REQUIRE (msgs.back()[1].getString() == "Banana");
}

TEST_CASE ("presetSet out of range leaves cursor uncorrupted")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc;
    proc.setIdentity (1);
    proc.setPresetRoot (root.getFullPathName());
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());
    proc.savePreset ("Apple");   // idx 0
    proc.savePreset ("Banana");  // idx 1

    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };

    proc.presetSet (99);   // out of range -> rejected, cursor must NOT become 99
    REQUIRE (msgs.back().getAddressPattern().toString() == "/custos/preset/error");

    proc.presetNext();     // cursor was never validly set -> previews idx 0, not 99+1
    REQUIRE (msgs.back().getAddressPattern().toString() == "/custos/preset/browsing");
    REQUIRE (msgs.back()[1].getString() == "Apple");
    root.deleteRecursively();
}
