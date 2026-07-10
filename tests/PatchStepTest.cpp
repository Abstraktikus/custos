#include <catch2/catch_test_macros.hpp>
#include "InstrumentBrowser.h"
#include "CustosProcessor.h"
#include "FakeInnerProcessor.h"
using custos::test::FakeInnerProcessor;

using namespace custos;

TEST_CASE ("patchMethodFor: PARAM/PC native, everything else falls back to Preset")
{
    REQUIRE (patchMethodFor ("PARAM")  == PatchMethod::Param);
    REQUIRE (patchMethodFor ("PC")     == PatchMethod::Pc);
    REQUIRE (patchMethodFor ("PRESET") == PatchMethod::PresetFallback);
    REQUIRE (patchMethodFor ("NONE")   == PatchMethod::PresetFallback);
    REQUIRE (patchMethodFor ("")       == PatchMethod::PresetFallback);
    REQUIRE (patchMethodFor ("host")   == PatchMethod::PresetFallback);   // no such type any more
}

TEST_CASE ("patchStep on a PRESET/unknown synth falls back to the preset cursor")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    // No matching favourite for the loaded path -> PresetFallback. With no presets on disk the
    // preset cursor cannot advance, so this must not crash and must not touch inner params.
    proc.loadInner (std::make_unique<FakeInnerProcessor> (4), "C:/unlisted.vst3");
    REQUIRE_NOTHROW (proc.patchNext());
    REQUIRE_NOTHROW (proc.patchPrev());
}
