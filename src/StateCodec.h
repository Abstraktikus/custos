#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>

namespace custos
{
struct PersistedState
{
    juce::String path; juce::MemoryBlock innerState; int identityN = 0;
    std::array<std::uint8_t, 16> route { {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16} };
};

// Format: "CUS1" + version byte + int32 pathLen + UTF-8 path + int32 innerLen + inner state bytes,
// and (version >= 2) + int32 identityN, and (version >= 3) + 16 route bytes.
// Version 1 blobs parse with identityN = 0. Version 1/2 blobs parse with an identity route.
juce::MemoryBlock serializeState (const juce::String& path, const juce::MemoryBlock& innerState,
                                  int identityN, const std::array<std::uint8_t, 16>& route);

// Returns false (leaving `out` untouched) on wrong magic/version or truncation.
bool parseState (const void* data, int size, PersistedState& out);
}
