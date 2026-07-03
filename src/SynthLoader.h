#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

namespace custos
{
class SynthLoader
{
public:
    // Loads a VST3 from an absolute path, synchronously. Returns nullptr on failure
    // with errorMessage set. Requires a JUCE message manager to exist (present inside a host;
    // tests use juce::ScopedJuceInitialiser_GUI).
    static std::unique_ptr<juce::AudioPluginInstance> loadVST3 (
        const juce::String& path, double sampleRate, int blockSize, juce::String& errorMessage);
};
}
