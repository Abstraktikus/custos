#include "HostTrace.h"
#include <atomic>

namespace custos
{
namespace { std::atomic<bool> g_traceEnabled { false }; }   // runtime gate; default OFF (quiet in production)

void setTraceEnabled (bool on) { g_traceEnabled.store (on); }
bool isTraceEnabled()          { return g_traceEnabled.load(); }

juce::File traceLogFile()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
              .getChildFile ("custos-hosttrace.log");
}

void trace (const juce::String& message)
{
   #if defined(CUSTOS_HOST_TRACE)
    if (! g_traceEnabled.load()) return;
    static const juce::int64 start = juce::Time::getHighResolutionTicks();
    const double ms = juce::Time::highResolutionTicksToSeconds (
        juce::Time::getHighResolutionTicks() - start) * 1000.0;
    traceLogFile().appendText (juce::String (ms, 3) + "  " + message + "\n");
   #else
    juce::ignoreUnused (message);
   #endif
}
}
