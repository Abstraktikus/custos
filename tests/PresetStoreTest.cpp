#include <catch2/catch_test_macros.hpp>
#include "PresetStore.h"

using namespace custos;

static PresetData makePreset (const juce::String& classId, const juce::String& name,
                              const juce::String& marker)
{
    PresetData p; p.classId = classId; p.synthName = "Synth"; p.presetName = name;
    p.innerState.append (marker.toRawUTF8(), marker.getNumBytesAsUTF8());
    return p;
}

TEST_CASE ("save then list returns names alphabetically case-insensitive")
{
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    const juce::String cls = "SynthA";
    REQUIRE (savePreset (root, makePreset (cls, "banana", "b")));
    REQUIRE (savePreset (root, makePreset (cls, "Apple",  "a")));
    const auto names = listPresets (root, cls);
    REQUIRE (names.size() == 2);
    REQUIRE (names[0] == "Apple");
    REQUIRE (names[1] == "banana");
    root.deleteRecursively();
}

TEST_CASE ("load returns the stored inner state and metadata")
{
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    REQUIRE (savePreset (root, makePreset ("SynthA", "Warm Pad", "hello")));
    PresetData out;
    REQUIRE (loadPreset (root, "SynthA", "Warm Pad", out));
    REQUIRE (out.classId == "SynthA");
    REQUIRE (out.innerState.matches ("hello", 5));
    REQUIRE_FALSE (loadPreset (root, "SynthA", "Missing", out));
    root.deleteRecursively();
}

TEST_CASE ("two synths keep separate folders keyed by classId")
{
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    REQUIRE (savePreset (root, makePreset ("SynthA", "P1", "a")));
    REQUIRE (savePreset (root, makePreset ("SynthB", "P1", "b")));
    REQUIRE (listPresets (root, "SynthA").size() == 1);
    REQUIRE (listPresets (root, "SynthB").size() == 1);
    root.deleteRecursively();
}

TEST_CASE ("rename and delete")
{
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    REQUIRE (savePreset (root, makePreset ("SynthA", "Old", "x")));
    REQUIRE (renamePreset (root, "SynthA", "Old", "New"));
    REQUIRE (listPresets (root, "SynthA") == std::vector<juce::String> { "New" });
    REQUIRE (deletePreset (root, "SynthA", "New"));
    REQUIRE (listPresets (root, "SynthA").empty());
    root.deleteRecursively();
}

TEST_CASE ("save with an all-illegal name is rejected, no file written")
{
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    REQUIRE_FALSE (savePreset (root, makePreset ("SynthA", "///", "x")));
    REQUIRE (listPresets (root, "SynthA").empty());
    root.deleteRecursively();
}
