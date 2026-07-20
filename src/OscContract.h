#pragma once
#include <array>
#include <juce_osc/juce_osc.h>

#ifndef CUSTOS_OSC_PORT
 #define CUSTOS_OSC_PORT 9100
#endif

namespace custos
{
constexpr int kProtoVersion = 3;

// Deterministic 1-based mapping. N in 1..15 -> BASE+N; anything else -> 0 (invalid / unassigned).
inline int oscPortForIdentity (int n)
{
    return (n >= 1 && n <= 15) ? (CUSTOS_OSC_PORT + n) : 0;
}

inline juce::OSCMessage buildHere (int n, const juce::String& mode, const juce::String& inner,
                                   int boundCount, int port, int facadeCap)
{
    return juce::OSCMessage ("/custos/here", n, kProtoVersion, mode, inner, boundCount, port, facadeCap);
}

inline juce::OSCMessage buildAck (int n, const juce::String& text)
{
    return juce::OSCMessage ("/custos/ack", n, text);
}

inline juce::OSCMessage buildLoaded (int n, const juce::String& path, int boundCount, int innerTotal)
{
    return juce::OSCMessage ("/custos/loaded", n, path, boundCount, innerTotal);
}

inline juce::OSCMessage buildParam (int n, int idx, float val, const juce::String& name,
                                    float defaultVal, int numSteps, const juce::String& label)
{
    return juce::OSCMessage ("/custos/param", n, idx, val, name, defaultVal, numSteps, label);
}

inline juce::OSCMessage buildParamsDone (int n, int start, int count)
{
    return juce::OSCMessage ("/custos/params/done", n, start, count);
}

// Window-position feedback (Custos -> KM). N first, then the PHYSICAL-pixel rect and the movable flag.
// Emitted when the operator drags the synth window or when a rect is (re)applied, so KM can capture the
// operator-chosen geometry for its settings. Same address as the inbound verb; the leading N marks it a report.
// contentW/contentH (added 2026-07-19) = the hosted editor's ACHIEVED size in DIPs: when it exceeds w/h the
// GUI is centre-cropped in the dock (plugin minimum larger than the fit area) — KM can hint the shortfall.
inline juce::OSCMessage buildWindowRect (int n, int x, int y, int w, int h, bool movable, int contentW, int contentH)
{
    return juce::OSCMessage ("/custos/window/rect", n, x, y, w, h, movable ? 1 : 0, contentW, contentH);
}

// Instrument-browse feedback (Custos -> KM/GP). Reports the cursor's favourite NAME while flipping (no
// load yet). wrapped=1 on the step that wrapped past an end. The actual load is signalled later by
// /custos/loaded (= ready/playable).
inline juce::OSCMessage buildBrowsing (int n, int index, const juce::String& name, bool wrapped)
{
    return juce::OSCMessage ("/custos/browsing", n, index, name, wrapped ? 1 : 0);
}

// arg2 of the EXIT SIGNAL: the /custos/browsing that reports "nothing to browse" (empty list, or no
// entry passing the scope+fit filter). Deliberately NOT an instrument name — GP renders arg2 as an
// overlay ("BROWSE <name>"), and a non-matching name there reads as loadable. See emitBrowseExit().
inline const char* const kBrowseNoneName = "(none)";

// MIDI route feedback (Custos -> KM). N first, then 16 targets (input i -> value; 0 = dropped).
// Same address as the inbound verb; the 17-arg form with leading N marks it a report.
inline juce::OSCMessage buildMidiRoute (int n, const std::array<int, 16>& route)
{
    juce::OSCMessage m ("/custos/midi/route");
    m.addInt32 (n);
    for (int t : route) m.addInt32 (t);
    return m;
}

// Main L/R fold feedback (Custos -> KM). N first, then the fold flag (1 = all inner outputs summed
// onto stereo Out 1; 0 = inner pairs mapped across the 5 stereo out buses).
inline juce::OSCMessage buildMainLR (int n, bool on)
{
    return juce::OSCMessage ("/custos/mainlr", n, on ? 1 : 0);
}

// Patch-step feedback (Custos -> KM/GP). controlType = the native method that ran, PARAM or PC only
// (the PRESET fallback reports via /custos/preset/* instead, never this).
// detail is best-effort ("+"/"-" for PARAM, program number for PC).
inline juce::OSCMessage buildPatchStepped (int n, const juce::String& controlType, const juce::String& detail)
{
    return juce::OSCMessage ("/custos/patch/stepped", n, controlType, detail);
}

// Learn feedback (Custos -> KM hub, hub-only). N first. `started`/`stopped` bracket a KM-opened
// capture window; `moved` reports one facade parameter the operator moved (idx into the facade,
// normalised value, current name) so KM can bind a macro by wiggling the knob. reason = "stop"|"timeout".
inline juce::OSCMessage buildLearnStarted (int n)
{
    return juce::OSCMessage ("/custos/learn/started", n);
}

inline juce::OSCMessage buildLearnMoved (int n, int facadeIdx, float value, const juce::String& name)
{
    return juce::OSCMessage ("/custos/learn/moved", n, facadeIdx, value, name);
}

inline juce::OSCMessage buildLearnStopped (int n, const juce::String& reason)
{
    return juce::OSCMessage ("/custos/learn/stopped", n, reason);
}

}
