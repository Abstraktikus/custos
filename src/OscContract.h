#pragma once
#include <juce_osc/juce_osc.h>

#ifndef CUSTOS_OSC_PORT
 #define CUSTOS_OSC_PORT 9100
#endif

namespace custos
{
constexpr int kProtoVersion = 1;

// Deterministic 1-based mapping. N in 1..15 -> BASE+N; anything else -> 0 (invalid / unassigned).
inline int oscPortForIdentity (int n)
{
    return (n >= 1 && n <= 15) ? (CUSTOS_OSC_PORT + n) : 0;
}

inline juce::OSCMessage buildHere (int n, const juce::String& mode, const juce::String& inner,
                                   int boundCount, int port)
{
    return juce::OSCMessage ("/custos/here", n, kProtoVersion, mode, inner, boundCount, port);
}

inline juce::OSCMessage buildAck (int n, const juce::String& text)
{
    return juce::OSCMessage ("/custos/ack", n, text);
}

inline juce::OSCMessage buildLoaded (int n, const juce::String& path, int boundCount)
{
    return juce::OSCMessage ("/custos/loaded", n, path, boundCount);
}

inline juce::OSCMessage buildParam (int n, int idx, float val, const juce::String& name)
{
    return juce::OSCMessage ("/custos/param", n, idx, val, name);
}

inline juce::OSCMessage buildParamsDone (int n, int start, int count)
{
    return juce::OSCMessage ("/custos/params/done", n, start, count);
}
}
