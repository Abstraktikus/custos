#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace custos
{
struct Favorite { juce::String name, path; int favOrder = 0; float gainDb = 0.0f; juce::String brand;
                  int slots = 0;                                   // synth's param count (0 = unknown)
                  juce::String controlType = "PRESET";             // PARAM | PC | PRESET | NONE
                  int paramDown = 0, paramUp = 0; };               // inject indices (PARAM only)

juce::String favoritesToJson (const std::vector<Favorite>& favs);
std::vector<Favorite> favoritesFromJson (const juce::String& json);

juce::File favoritesConfigFile();                                             // %APPDATA%/Custos/favorites.json
bool writeFavorites (const juce::File& file, const std::vector<Favorite>& favs);
std::vector<Favorite> readFavorites (const juce::File& file);                 // missing file -> empty

juce::File instrumentsConfigFile();                                          // %APPDATA%/Custos/instruments.json
std::vector<Favorite> readInstruments (const juce::File& newFile, const juce::File& legacyFile); // new, else migrate legacy

// Root-aware instrument-DB location (unified data root).
juce::File instrumentsFileIn   (const juce::File& root);           // <root>/instruments.json
juce::File instrumentsTargetFor (const juce::File& root);          // <root>/... or legacy %APPDATA% if root empty
bool       writeInstruments    (const juce::File& root, const std::vector<Favorite>& favs);
}
