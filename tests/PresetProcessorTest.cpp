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
    CustosProcessor proc (false, root.getChildFile ("presetRoot.txt"));
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
    CustosProcessor proc (false, root.getChildFile ("presetRoot.txt"));
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
    CustosProcessor proc (false, root.getChildFile ("presetRoot.txt"));
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
    CustosProcessor proc (false, root.getChildFile ("presetRoot.txt"));
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
    CustosProcessor proc (false, root.getChildFile ("presetRoot.txt"));
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
    CustosProcessor proc (false, root.getChildFile ("presetRoot.txt"));
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

TEST_CASE ("preset names preserve German umlauts")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc (false, root.getChildFile ("presetRoot.txt"));
    proc.setIdentity (1);
    proc.setPresetRoot (root.getFullPathName());
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());

    REQUIRE (proc.savePreset (juce::CharPointer_UTF8 ("Gr\xC3\xBCner Bass")) == 0);
    const auto names = proc.listPresets();
    REQUIRE (names.size() == 1);
    REQUIRE (names[0] == juce::String (juce::CharPointer_UTF8 ("Gr\xC3\xBCner Bass")));
    root.deleteRecursively();
}

TEST_CASE ("presetCursor re-seeds to the new synth on attachInner swap")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc (false, root.getChildFile ("presetRoot.txt"));
    proc.setIdentity (1);
    proc.setPresetRoot (root.getFullPathName());

    // Synth A: two presets, step the cursor to index 1 ("Banana").
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());
    proc.savePreset ("Apple");    // idx 0
    proc.savePreset ("Banana");   // idx 1

    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };
    proc.presetNext();   // unset -> idx 0 ("Apple")
    proc.presetNext();   // idx 0 -> idx 1 ("Banana")
    REQUIRE (msgs.back()[1].getString() == "Banana");

    // Swap to synth B (same fallback key "FakeInner", so folder is shared) and save a third preset.
    // List becomes alphabetical: "Apple", "Banana", "Cherry" (idx 0, 1, 2).
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());
    proc.savePreset ("Cherry");   // idx 2

    // If the swap reset presetCursor to -1, the next presetNext() previews idx 0 ("Apple").
    // If the swap left the stale cursor (1) in place, it would advance to idx 2 ("Cherry").
    proc.presetNext();
    REQUIRE (msgs.back().getAddressPattern().toString() == "/custos/preset/browsing");
    REQUIRE (msgs.back()[1].getString() == "Apple");
    root.deleteRecursively();
}

TEST_CASE ("rename and delete presets emit feedback")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc (false, root.getChildFile ("presetRoot.txt"));
    proc.setIdentity (4);
    proc.setPresetRoot (root.getFullPathName());
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());
    proc.savePreset ("Old");

    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };

    REQUIRE (proc.renamePreset ("Old", "New"));
    REQUIRE (msgs.back().getAddressPattern().toString() == "/custos/preset/renamed");
    REQUIRE (proc.listPresets() == std::vector<juce::String> { "New" });

    REQUIRE (proc.deletePreset ("New"));
    REQUIRE (msgs.back().getAddressPattern().toString() == "/custos/preset/deleted");
    REQUIRE (proc.listPresets().empty());

    REQUIRE_FALSE (proc.deletePreset ("Ghost"));
    REQUIRE (msgs.back().getAddressPattern().toString() == "/custos/preset/error");
    root.deleteRecursively();
}

TEST_CASE ("recall during an in-flight synth load is buffered then applied after the load")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc (false, root.getChildFile ("presetRoot.txt"));
    proc.setIdentity (5);
    proc.setPresetRoot (root.getFullPathName());
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());
    proc.savePreset ("Apple");    // idx 0
    proc.savePreset ("Banana");   // idx 1

    // Arm an in-flight synth load (browse debounce). browseInstrument needs a favourite to arm;
    // startTimer sets isTimerRunning() true immediately, no message loop required.
    proc.setFavorites ({ { "Synth2", "C:/x/Synth2.vst3", 1, 0.0f } });
    proc.browseInstrument (+1);   // arms browseDebounce -> load in-flight

    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };

    proc.presetSet (1);   // arrives during the in-flight load -> buffered, must NOT execute yet
    const bool loadedWhilePending = std::any_of (msgs.begin(), msgs.end(),
        [] (const juce::OSCMessage& m) { return m.getAddressPattern().toString() == "/custos/preset/loaded"; });
    REQUIRE_FALSE (loadedWhilePending);

    // Complete a synth load through the choke point -> drains the buffered recall onto the new synth
    // (same fake key -> same folder -> Apple/Banana exist).
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());

    const auto lastLoaded = std::find_if (msgs.rbegin(), msgs.rend(),
        [] (const juce::OSCMessage& m) { return m.getAddressPattern().toString() == "/custos/preset/loaded"; });
    REQUIRE (lastLoaded != msgs.rend());
    REQUIRE ((*lastLoaded)[1].getString() == "Banana");   // buffered set(1) applied post-load

    root.deleteRecursively();
}

TEST_CASE ("emitPresetRoot reports identity + current root")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc (false, root.getChildFile ("presetRoot.txt"));
    proc.setIdentity (6);
    proc.setPresetRoot (root.getFullPathName());

    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };

    proc.emitPresetRoot();
    REQUIRE (msgs.size() == 1);
    REQUIRE (msgs[0].getAddressPattern().toString() == "/custos/preset/root");
    REQUIRE ((int) msgs[0][0].getInt32() == 6);
    REQUIRE (msgs[0][1].getString() == root.getFullPathName());
    root.deleteRecursively();
}

TEST_CASE ("setPresetRoot adopts an existing instruments.json at the new root")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    custos::writeFavorites (root.getChildFile ("instruments.json"),
                            { { "Adopted", "C:/a.vst3", 1, 0.0f } });
    CustosProcessor proc (false, root.getChildFile ("presetRoot.txt"));
    proc.setIdentity (1);
    proc.setPresetRoot (root.getFullPathName());     // should adopt the file's favourites
    REQUIRE (proc.getFavorites().size() == 1);
    REQUIRE (proc.getFavorites()[0].name == "Adopted");
    root.deleteRecursively();
}

TEST_CASE ("setPresetRoot carries current favourites into an empty new root")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc (false, root.getChildFile ("presetRoot.txt"));
    proc.setIdentity (1);
    proc.setFavorites ({ { "Carried", "C:/c.vst3", 1, 0.0f } });
    proc.setPresetRoot (root.getFullPathName());     // no file at root -> carry memory there
    auto seeded = custos::readFavorites (root.getChildFile ("instruments.json"));
    REQUIRE (seeded.size() == 1);
    REQUIRE (seeded[0].name == "Carried");
    root.deleteRecursively();
}

TEST_CASE ("setPresetRoot does not blank in-memory favourites when the target parses empty")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    // Target exists but is corrupt / parses to empty (favoritesFromJson yields {} for garbage).
    root.getChildFile ("instruments.json").replaceWithText ("not valid json");

    CustosProcessor proc (false, root.getChildFile ("presetRoot.txt"));
    proc.setIdentity (1);
    proc.setFavorites ({ { "Good", "C:/good.vst3", 1, 0.0f } });

    proc.setPresetRoot (root.getFullPathName());     // target parses empty -> must carry memory, not blank

    REQUIRE (proc.getFavorites().size() == 1);
    REQUIRE (proc.getFavorites()[0].name == "Good");

    // And the carried favourites must actually land at the new root, not be orphaned at the old one.
    auto onDisk = custos::readFavorites (root.getChildFile ("instruments.json"));
    REQUIRE (onDisk.size() == 1);
    REQUIRE (onDisk[0].name == "Good");

    root.deleteRecursively();
}

TEST_CASE ("FavEnd-equivalent persistFavorites surfaces a failed write via preset/error")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto tmp = juce::File::createTempFile (""); tmp.deleteFile(); tmp.createDirectory();
    auto blocker = tmp.getChildFile ("blocker"); blocker.replaceWithText ("x");  // a FILE
    auto root = blocker.getChildFile ("sub");    // parent 'blocker' is a file -> mkdir + write must fail

    CustosProcessor proc (false, tmp.getChildFile ("presetRoot.txt"));
    proc.setIdentity (9);
    proc.setPresetRoot (root.getFullPathName());   // no favourites yet -> adopt branch is a no-op
    proc.setFavorites ({ { "X", "C:/x.vst3", 1, 0.0f } });

    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };

    REQUIRE_FALSE (proc.persistFavorites());
    REQUIRE (msgs.size() == 1);
    REQUIRE (msgs[0].getAddressPattern().toString() == "/custos/preset/error");
    REQUIRE ((int) msgs[0][0].getInt32() == 9);

    tmp.deleteRecursively();
}

TEST_CASE ("setPresetRoot carry write surfaces a failure via preset/error instead of swallowing it")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto tmp = juce::File::createTempFile (""); tmp.deleteFile(); tmp.createDirectory();
    auto blocker = tmp.getChildFile ("blocker"); blocker.replaceWithText ("x");  // a FILE
    auto root = blocker.getChildFile ("sub");    // parent 'blocker' is a file -> mkdir + write must fail

    CustosProcessor proc (false, tmp.getChildFile ("presetRoot.txt"));
    proc.setIdentity (8);
    proc.setFavorites ({ { "Carried", "C:/c.vst3", 1, 0.0f } });   // non-empty in-memory favourites BEFORE the switch

    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };

    proc.setPresetRoot (root.getFullPathName());   // no file at unwritable root -> carry write, must not go silent

    REQUIRE (std::any_of (msgs.begin(), msgs.end(),
        [] (const juce::OSCMessage& m) { return m.getAddressPattern().toString() == "/custos/preset/error"; }));

    tmp.deleteRecursively();
}

TEST_CASE ("setPresetRoot surfaces a failed config-file persist")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    auto blocker = root.getChildFile ("blocker"); blocker.replaceWithText ("x");   // a FILE
    auto badCfg = blocker.getChildFile ("sub/presetRoot.txt");   // parent 'blocker' is a file -> write must fail

    CustosProcessor proc (false, badCfg);
    proc.setIdentity (1);
    std::vector<juce::OSCMessage> msgs;
    proc.outboundSink = [&] (const juce::OSCMessage& m) { msgs.push_back (m); };
    proc.setPresetRoot (root.getFullPathName());   // writable root, unwritable config
    const bool sawError = std::any_of (msgs.begin(), msgs.end(),
        [] (const juce::OSCMessage& m){ return m.getAddressPattern().toString() == "/custos/preset/error"; });
    REQUIRE (sawError);
    root.deleteRecursively();
}

TEST_CASE ("loadedPresetName tracks save/load/rename/delete and clears on synth swap")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc (false, root.getChildFile ("presetRoot.txt"));
    proc.setPresetRoot (root.getFullPathName());
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());

    REQUIRE (proc.loadedPresetName().isEmpty());

    proc.savePreset ("A");
    REQUIRE (proc.loadedPresetName() == "A");
    proc.savePreset ("B");                       // save-as-new makes the new preset current
    REQUIRE (proc.loadedPresetName() == "B");
    proc.savePreset ("///");                     // sanitizes to "" -> error, current unchanged
    REQUIRE (proc.loadedPresetName() == "B");

    REQUIRE (proc.loadPresetByName ("A"));
    REQUIRE (proc.loadedPresetName() == "A");
    REQUIRE_FALSE (proc.loadPresetByName ("nope"));   // failed load leaves current alone
    REQUIRE (proc.loadedPresetName() == "A");

    REQUIRE (proc.renamePreset ("A", "C"));      // rename of the current preset follows
    REQUIRE (proc.loadedPresetName() == "C");
    REQUIRE (proc.renamePreset ("B", "D"));      // rename of another preset doesn't
    REQUIRE (proc.loadedPresetName() == "C");

    REQUIRE (proc.deletePreset ("D"));           // delete of another preset doesn't clear
    REQUIRE (proc.loadedPresetName() == "C");
    REQUIRE (proc.deletePreset ("C"));           // delete of the current preset clears
    REQUIRE (proc.loadedPresetName().isEmpty());

    proc.savePreset ("E");
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());   // synth swap
    REQUIRE (proc.loadedPresetName().isEmpty());
    root.deleteRecursively();
}

TEST_CASE ("presetNameRevision bumps on every assignment, including a same-name reload")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc (false, root.getChildFile ("presetRoot.txt"));
    proc.setPresetRoot (root.getFullPathName());
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());

    proc.savePreset ("A");
    const int afterSave = proc.presetNameRevision();
    REQUIRE (proc.loadPresetByName ("A"));       // same name, but a new event
    REQUIRE (proc.presetNameRevision() > afterSave);
    root.deleteRecursively();
}
