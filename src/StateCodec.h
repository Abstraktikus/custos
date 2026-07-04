#pragma once
#include <juce_core/juce_core.h>

namespace custos
{
struct PersistedState { juce::String path; juce::MemoryBlock innerState; int identityN = 0; };

// Format: "CUS1" + version byte + int32 pathLen + UTF-8 path + int32 innerLen + inner state bytes,
// and (version >= 2) + int32 identityN. Version 1 blobs parse with identityN = 0.
juce::MemoryBlock serializeState (const juce::String& path, const juce::MemoryBlock& innerState, int identityN);

// Returns false (leaving `out` untouched) on wrong magic/version or truncation.
bool parseState (const void* data, int size, PersistedState& out);
}
