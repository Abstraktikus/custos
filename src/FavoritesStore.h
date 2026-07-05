#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace custos
{
struct Favorite { juce::String name, path; int favOrder = 0; float gainDb = 0.0f; juce::String brand; };

juce::String favoritesToJson (const std::vector<Favorite>& favs);
std::vector<Favorite> favoritesFromJson (const juce::String& json);

juce::File favoritesConfigFile();                                             // %APPDATA%/Custos/favorites.json
void writeFavorites (const juce::File& file, const std::vector<Favorite>& favs);
std::vector<Favorite> readFavorites (const juce::File& file);                 // missing file -> empty
}
