#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;
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

TEST_CASE ("PARAM patchStep injects 1.0 into the paramUp/paramDown index")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    auto fake = std::make_unique<FakeInnerProcessor> (4);
    auto* fakePtr = fake.get();
    proc.loadInner (std::move (fake), "C:/AL.vst3");
    proc.setFavorites ({ [] { custos::Favorite f; f.name="AL"; f.path="C:/AL.vst3"; f.favOrder=1;
                              f.controlType="PARAM"; f.paramDown=2; f.paramUp=3; return f; }() });

    proc.patchNext();   // -> paramUp = index 3 set to 1.0
    REQUIRE (fakePtr->getParameters()[3]->getValue() == Approx (1.0f));

    proc.patchPrev();   // -> paramDown = index 2 set to 1.0
    REQUIRE (fakePtr->getParameters()[2]->getValue() == Approx (1.0f));
}

TEST_CASE ("PARAM patchStep with an out-of-range index is a no-op")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    CustosProcessor proc;
    proc.loadInner (std::make_unique<FakeInnerProcessor> (2), "C:/AL.vst3");
    proc.setFavorites ({ [] { custos::Favorite f; f.name="AL"; f.path="C:/AL.vst3"; f.favOrder=1;
                              f.controlType="PARAM"; f.paramDown=0; f.paramUp=9; return f; }() });
    REQUIRE_NOTHROW (proc.patchNext());   // index 9 >= 2 params -> no-op, no crash
}
