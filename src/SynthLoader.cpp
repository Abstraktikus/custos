#include "SynthLoader.h"

namespace custos
{
std::unique_ptr<juce::AudioPluginInstance> SynthLoader::loadVST3 (
    const juce::String& path, double sampleRate, int blockSize, juce::String& errorMessage)
{
    if (path.isEmpty())        { errorMessage = "empty synth path"; return nullptr; }
    if (! juce::File (path).exists()) { errorMessage = "file not found: " + path; return nullptr; }

    juce::AudioPluginFormatManager formatManager;
    formatManager.addFormat (new juce::VST3PluginFormat());   // JUCE 8: member addDefaultFormats() is deleted

    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3.findAllTypesForFile (descriptions, path);
    if (descriptions.isEmpty()) { errorMessage = "no VST3 types in: " + path; return nullptr; }

    return formatManager.createPluginInstance (*descriptions.getFirst(),
                                               sampleRate, blockSize, errorMessage);
}
}
