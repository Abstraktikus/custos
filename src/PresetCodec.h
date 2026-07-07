#pragma once
#include <juce_core/juce_core.h>

namespace custos
{
struct PresetData
{
    juce::String classId;      // stable per-synth key (also the folder scope)
    juce::String synthName;    // human-readable, for display
    juce::String presetName;   // user-given
    juce::MemoryBlock innerState;
};

// Format: "CUSP" + version byte (1) + int32 classIdLen + UTF-8 classId
//         + int32 synthNameLen + UTF-8 synthName + int32 presetNameLen + UTF-8 presetName
//         + int32 innerLen + inner state bytes.
juce::MemoryBlock serializePreset (const PresetData&);

// Returns false (leaving `out` untouched) on wrong magic/version or truncation.
bool parsePreset (const void* data, int size, PresetData& out);
}
