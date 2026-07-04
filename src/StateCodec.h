#pragma once
#include <juce_core/juce_core.h>

namespace custos
{
struct PersistedState { juce::String path; juce::MemoryBlock innerState; };

// Format: "CUS1" + version(1) + int32 pathLen + UTF-8 path + int32 innerLen + inner state bytes.
juce::MemoryBlock serializeState (const juce::String& path, const juce::MemoryBlock& innerState);

// Returns false (leaving `out` untouched) on wrong magic/version or truncation.
bool parseState (const void* data, int size, PersistedState& out);
}
