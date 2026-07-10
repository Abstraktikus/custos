#include <catch2/catch_test_macros.hpp>
#include "InstrumentBrowser.h"

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
