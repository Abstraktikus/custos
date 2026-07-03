#pragma once
#include <juce_core/juce_core.h>

namespace custos
{
    // Appends a timestamped line to the host-trace log (temp dir). Compiled to a no-op
    // unless CUSTOS_HOST_TRACE is defined. Used to observe GP's boot call order
    // (construct / setState / first parameter query) for the count-timing experiment.
    void trace (const juce::String& message);

    juce::File traceLogFile();
}
