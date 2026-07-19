#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"
#include "CustosEditor.h"
#include "FakeInnerProcessor.h"
#include <memory>

using namespace custos;

TEST_CASE ("Synth window starts hidden and is a no-op without an inner synth")
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // message manager for GUI-adjacent construction
    CustosProcessor proc;                        // empty hard-coded path -> no inner attached

    REQUIRE_FALSE (proc.hasInnerSynth());
    REQUIRE_FALSE (proc.isSynthWindowVisible());

    proc.showSynthWindow();                      // no inner -> no window
    REQUIRE_FALSE (proc.isSynthWindowVisible());

    proc.toggleSynthWindow();                    // still no inner -> no window
    REQUIRE_FALSE (proc.isSynthWindowVisible());
}

TEST_CASE ("Show is a graceful no-op when the inner synth has no editor")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.attachInner (std::make_unique<custos::test::FakeInnerProcessor> (2));  // FakeInner has no editor

    REQUIRE (proc.hasInnerSynth());
    proc.showSynthWindow();                      // inner has no editor -> no window created
    REQUIRE_FALSE (proc.isSynthWindowVisible());
}

TEST_CASE ("innerSynthName reflects the attached inner processor")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    REQUIRE (proc.innerSynthName() == "(none)");

    proc.attachInner (std::make_unique<custos::test::FakeInnerProcessor> (2));
    REQUIRE (proc.innerSynthName() == "FakeInner");
}

TEST_CASE ("CustosProcessor has an editor and creates a non-null CustosEditor")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    REQUIRE (proc.hasEditor());
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
}

TEST_CASE ("editor constructs and refreshes with presets present")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc (false, root.getChildFile ("presetRoot.txt"));
    proc.setPresetRoot (root.getFullPathName());
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());
    proc.savePreset ("Warm Pad");

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    // refresh() must not crash and must reflect the one saved preset.
    dynamic_cast<CustosEditor*> (ed.get())->refresh();
    REQUIRE (proc.listPresets().size() == 1);
    root.deleteRecursively();
}

TEST_CASE ("preset name field prefills with the loaded preset and preserves typing")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    CustosProcessor proc (false, root.getChildFile ("presetRoot.txt"));
    proc.setPresetRoot (root.getFullPathName());
    proc.attachInner (std::make_unique<test::FakeInnerProcessor>());
    proc.savePreset ("Warm Pad");

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<CustosEditor*> (ed.get());
    editor->refresh();
    REQUIRE (editor->presetNameText() == "Warm Pad");   // prefilled from the save

    editor->setPresetNameText ("My New Name");           // user types a save-as-new name
    editor->refresh();                                   // unrelated refresh must not clobber it
    REQUIRE (editor->presetNameText() == "My New Name");

    proc.loadPresetByName ("Warm Pad");                  // a real load re-syncs the field
    editor->refresh();
    REQUIRE (editor->presetNameText() == "Warm Pad");
    root.deleteRecursively();
}
