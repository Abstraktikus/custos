#include <catch2/catch_test_macros.hpp>
#include "PresetCodec.h"

using namespace custos;

TEST_CASE ("preset round-trips through serialize/parse")
{
    PresetData in;
    in.classId    = "VST3-Omnisphere-abc123";
    in.synthName  = "Omnisphere";
    in.presetName = "Warm Pad";
    in.innerState.append ("inner-bytes", 11);

    const auto blob = serializePreset (in);
    PresetData out;
    REQUIRE (parsePreset (blob.getData(), (int) blob.getSize(), out));
    REQUIRE (out.classId == "VST3-Omnisphere-abc123");
    REQUIRE (out.synthName == "Omnisphere");
    REQUIRE (out.presetName == "Warm Pad");
    REQUIRE (out.innerState.getSize() == 11);
    REQUIRE (out.innerState.matches ("inner-bytes", 11));
}

TEST_CASE ("parsePreset rejects wrong magic and truncation")
{
    PresetData out;
    REQUIRE_FALSE (parsePreset ("XXXX\1", 5, out));
    const char tiny[] = { 'C','U','S','P' };
    REQUIRE_FALSE (parsePreset (tiny, 4, out));   // no version byte
}
