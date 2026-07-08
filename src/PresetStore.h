#pragma once
#include <juce_core/juce_core.h>
#include "PresetCodec.h"
#include <vector>

namespace custos
{
// <root>/<hex(classId)> — class-ids are not guaranteed filesystem-safe, so the folder name
// is the hex encoding of the classId's UTF-8 bytes.
juce::File presetFolderFor (const juce::File& root, const juce::String& classId);

// Strip filesystem-illegal characters for the on-disk file stem. May return "" (all illegal).
juce::String sanitizePresetName (const juce::String& name);

// Write p under presetFolderFor(root, p.classId)/<sanitize(p.presetName)>.cuspreset (creates dirs,
// overwrites same name). Returns false if the sanitized name is empty (nothing written).
bool savePreset (const juce::File& root, const PresetData& p);

// File stems (no extension) in the synth's folder, alphabetical, case-insensitive.
std::vector<juce::String> listPresets (const juce::File& root, const juce::String& classId);

// Load by display name (sanitized to locate the file). false if missing or corrupt.
bool loadPreset (const juce::File& root, const juce::String& classId,
                 const juce::String& name, PresetData& out);

bool deletePreset (const juce::File& root, const juce::String& classId, const juce::String& name);
bool renamePreset (const juce::File& root, const juce::String& classId,
                   const juce::String& oldName, const juce::String& newName);

// Machine-global root persistence (favourites-style), %APPDATA%/Custos/presetRoot.txt.
juce::File   presetRootConfigFile();
juce::File   defaultPresetRoot();                    // ~/Documents/CustosPresets
void         writePresetRoot (const juce::File& cfg, const juce::String& path);
juce::String readPresetRoot (const juce::File& cfg); // missing/empty -> defaultPresetRoot() path
}
