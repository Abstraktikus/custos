#pragma once
#include <juce_core/juce_core.h>

namespace custos
{
    // Appends a timestamped line to the host-trace log (temp dir). Compiled to a no-op unless
    // CUSTOS_HOST_TRACE is defined, AND gated at RUNTIME by setTraceEnabled (default OFF, so production
    // is quiet). The hidden editor "Trace" toggle flips it for E2E; call sites prefix "N<id>" so a
    // multi-instance log is greppable per instance.
    void trace (const juce::String& message);

    void setTraceEnabled (bool on);   // runtime on/off (global; any instance's toggle controls it)
    bool isTraceEnabled();

    juce::File traceLogFile();
}
