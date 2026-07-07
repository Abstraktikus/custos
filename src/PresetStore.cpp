#include "PresetStore.h"

namespace custos
{
juce::File presetFolderFor (const juce::File& root, const juce::String& classId)
{
    return root.getChildFile (juce::String::toHexString (classId.toRawUTF8(),
                                                         classId.getNumBytesAsUTF8(), 0));
}

juce::String sanitizePresetName (const juce::String& name)
{
    juce::String s;
    for (auto c : name)
        if (juce::CharacterFunctions::isPrintable (c)
            && juce::String ("\\/:*?\"<>|").indexOfChar (c) < 0)
            s += c;
    return s.trim();
}

static juce::File fileFor (const juce::File& root, const juce::String& classId, const juce::String& name)
{
    return presetFolderFor (root, classId).getChildFile (sanitizePresetName (name) + ".cuspreset");
}

bool savePreset (const juce::File& root, const PresetData& p)
{
    const auto stem = sanitizePresetName (p.presetName);
    if (stem.isEmpty()) return false;
    auto folder = presetFolderFor (root, p.classId);
    folder.createDirectory();
    auto file = folder.getChildFile (stem + ".cuspreset");
    const auto blob = serializePreset (p);
    return file.replaceWithData (blob.getData(), blob.getSize());
}

std::vector<juce::String> listPresets (const juce::File& root, const juce::String& classId)
{
    std::vector<juce::String> names;
    auto folder = presetFolderFor (root, classId);
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.cuspreset"))
        names.push_back (f.getFileNameWithoutExtension());
    std::sort (names.begin(), names.end(),
               [] (const juce::String& a, const juce::String& b)
               { return a.compareIgnoreCase (b) < 0; });
    return names;
}

bool loadPreset (const juce::File& root, const juce::String& classId,
                 const juce::String& name, PresetData& out)
{
    auto file = fileFor (root, classId, name);
    if (! file.existsAsFile()) return false;
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb)) return false;
    return parsePreset (mb.getData(), (int) mb.getSize(), out);
}

bool deletePreset (const juce::File& root, const juce::String& classId, const juce::String& name)
{
    auto file = fileFor (root, classId, name);
    return file.existsAsFile() && file.deleteFile();
}

bool renamePreset (const juce::File& root, const juce::String& classId,
                   const juce::String& oldName, const juce::String& newName)
{
    const auto stem = sanitizePresetName (newName);
    if (stem.isEmpty()) return false;
    auto from = fileFor (root, classId, oldName);
    if (! from.existsAsFile()) return false;
    return from.moveFileTo (presetFolderFor (root, classId).getChildFile (stem + ".cuspreset"));
}
}
