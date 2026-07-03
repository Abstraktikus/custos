#pragma once
#include <juce_core/juce_core.h>

namespace custos
{
    // Facade parameter count reported to the host. GP queries the count at boot, so it
    // MUST be fixed at construction. Start high (covers large synths); measure in GP, tune later.
    inline constexpr int kFacadeParamCount = 5000;

    inline constexpr const char* kVendor  = "Kapellmeister";
    inline constexpr const char* kProduct = "Custos";

    // M1: hard-coded inner synth path. Overridable via CMake -DCUSTOS_HARDCODED_SYNTH_PATH="...".
    // Empty => no inner synth (silent passthrough).
    inline juce::String hardcodedSynthPath()
    {
       #if defined(CUSTOS_HARDCODED_SYNTH_PATH)
        return juce::String (CUSTOS_HARDCODED_SYNTH_PATH);
       #else
        return {};
       #endif
    }
}
