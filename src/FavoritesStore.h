#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace custos
{
struct Favorite { juce::String name, path; int favOrder = 0; float gainDb = 0.0f; juce::String brand;
                  int slots = 0;                                   // synth's param count (0 = unknown)
                  juce::String controlType = "PRESET";             // PARAM | PC | PRESET | NONE
                  int paramDown = 0, paramUp = 0;                  // inject indices (PARAM only)
                  juce::String classId; };                         // v4: inner synth's stable VST3 key (empty = unknown)

juce::String favoritesToJson (const std::vector<Favorite>& favs);
std::vector<Favorite> favoritesFromJson (const juce::String& json);

juce::File favoritesConfigFile();                                             // %APPDATA%/Custos/favorites.json
bool writeFavorites (const juce::File& file, const std::vector<Favorite>& favs);   // returns true if the write succeeded
std::vector<Favorite> readFavorites (const juce::File& file);                 // missing file -> empty

juce::File instrumentsConfigFile();                                          // %APPDATA%/Custos/instruments.json
std::vector<Favorite> readInstruments (const juce::File& newFile, const juce::File& legacyFile); // new, else migrate legacy

// Root-aware instrument-DB location (unified data root).
juce::File instrumentsFileIn   (const juce::File& root);           // <root>/instruments.json
juce::File instrumentsTargetFor (const juce::File& root);          // <root>/... or legacy %APPDATA% if root empty
bool       writeInstruments    (const juce::File& root, const std::vector<Favorite>& favs);   // returns true if the write succeeded

struct InstrumentsSource { juce::File file; bool fromLegacy = false; bool found = false; };

// First existing of [<root>/instruments.json, legacyCanonical, legacyOld]. Non-empty root required
// for tier 1. When none exist, found=false and file = the tier-1 target (<root>/instruments.json,
// or legacyCanonical if root empty).
InstrumentsSource resolveInstrumentsSource (const juce::File& root,
                                            const juce::File& legacyCanonical,
                                            const juce::File& legacyOld);

// Load favourites for a data root: resolve (root > legacy canonical > legacy old), read, and if the
// data came from a legacy tier while root is non-empty, seed <root>/instruments.json once. Idempotent.
std::vector<Favorite> loadInstrumentsWithSelfHeal (const juce::File& root,
                                                   const juce::File& legacyCanonical,
                                                   const juce::File& legacyOld);

// Checked variant of the same read: also reports WHICH file was read and whether that file exists
// yet yielded NO entries (empty/garbage content) — the cloud-placeholder/hydration signature the
// boot path must retry and report instead of silently running with no favourites and no size guard.
// A missing source (first configuration) is empty but NOT suspicious.
struct InstrumentsRead { std::vector<Favorite> favs; bool suspiciousEmpty = false; juce::File source; };
InstrumentsRead readInstrumentsChecked (const juce::File& root,
                                        const juce::File& legacyCanonical,
                                        const juce::File& legacyOld);
}
