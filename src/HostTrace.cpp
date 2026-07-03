#include "HostTrace.h"

namespace custos
{
juce::File traceLogFile()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
              .getChildFile ("custos-hosttrace.log");
}

void trace (const juce::String& message)
{
   #if defined(CUSTOS_HOST_TRACE)
    static const juce::int64 start = juce::Time::getHighResolutionTicks();
    const double ms = juce::Time::highResolutionTicksToSeconds (
        juce::Time::getHighResolutionTicks() - start) * 1000.0;
    traceLogFile().appendText (juce::String (ms, 3) + "  " + message + "\n");
   #else
    juce::ignoreUnused (message);
   #endif
}
}
