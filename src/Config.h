#pragma once
#include <juce_core/juce_core.h>

namespace custos
{
    // Facade parameter count reported to the host. GP queries the count at boot, so it
    // MUST be fixed at construction. Overridable per build target (facade-size ladder);
    // default 5000 when no override is supplied.
   #ifndef CUSTOS_FACADE_PARAM_COUNT
    #define CUSTOS_FACADE_PARAM_COUNT 5000
   #endif
    inline constexpr int kFacadeParamCount = CUSTOS_FACADE_PARAM_COUNT;

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
