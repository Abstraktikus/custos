#include "FavoritesStore.h"

namespace custos
{
juce::String favoritesToJson (const std::vector<Favorite>& favs)
{
    juce::Array<juce::var> arr;
    for (const auto& f : favs)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", f.name);
        o->setProperty ("path", f.path);
        o->setProperty ("favOrder", f.favOrder);
        o->setProperty ("gainDb", f.gainDb);
        o->setProperty ("brand", f.brand);
        o->setProperty ("slots", f.slots);
        o->setProperty ("controlType", f.controlType);
        o->setProperty ("paramDown", f.paramDown);
        o->setProperty ("paramUp", f.paramUp);
        o->setProperty ("classId", f.classId);   // v4: stable inner-synth key (durable record of KM's push)
        arr.add (juce::var (o));
    }
    return juce::JSON::toString (juce::var (arr));
}

std::vector<Favorite> favoritesFromJson (const juce::String& json)
{
    std::vector<Favorite> out;
    const auto parsed = juce::JSON::parse (json);
    if (auto* arr = parsed.getArray())
        for (const auto& v : *arr)
        {
            Favorite f;
            f.name     = v.getProperty ("name", "").toString();
            f.path     = v.getProperty ("path", "").toString();
            f.favOrder = (int) v.getProperty ("favOrder", 0);
            f.gainDb   = (float) (double) v.getProperty ("gainDb", 0.0);
            f.brand    = v.getProperty ("brand", "").toString();
            f.slots    = (int) v.getProperty ("slots", 0);
            f.controlType = v.getProperty ("controlType", "PRESET").toString();
            f.paramDown   = (int) v.getProperty ("paramDown", 0);
            f.paramUp     = (int) v.getProperty ("paramUp", 0);
            f.classId     = v.getProperty ("classId", "").toString();   // v4: absent -> empty (back-compat)
            out.push_back (f);
        }
    return out;
}

juce::File favoritesConfigFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
              .getChildFile ("Custos").getChildFile ("favorites.json");
}

bool writeFavorites (const juce::File& file, const std::vector<Favorite>& favs)
{
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (favoritesToJson (favs));
}

std::vector<Favorite> readFavorites (const juce::File& file)
{
    if (! file.existsAsFile()) return {};
    return favoritesFromJson (file.loadFileAsString());
}

juce::File instrumentsConfigFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
              .getChildFile ("Custos").getChildFile ("instruments.json");
}

juce::File instrumentsFileIn (const juce::File& root)
{
    return root.getChildFile ("instruments.json");
}

juce::File instrumentsTargetFor (const juce::File& root)
{
    return root.getFullPathName().isNotEmpty() ? instrumentsFileIn (root)
                                               : instrumentsConfigFile();
}

bool writeInstruments (const juce::File& root, const std::vector<Favorite>& favs)
{
    return writeFavorites (instrumentsTargetFor (root), favs);
}

std::vector<Favorite> readInstruments (const juce::File& newFile, const juce::File& legacyFile)
{
    if (newFile.existsAsFile())    return readFavorites (newFile);
    if (legacyFile.existsAsFile()) return readFavorites (legacyFile);   // one-time migration read
    return {};
}

InstrumentsSource resolveInstrumentsSource (const juce::File& root,
                                            const juce::File& legacyCanonical,
                                            const juce::File& legacyOld)
{
    const bool rootValid = root.getFullPathName().isNotEmpty();
    if (rootValid)
    {
        auto tier1 = instrumentsFileIn (root);
        if (tier1.existsAsFile()) return { tier1, false, true };
    }
    if (legacyCanonical.existsAsFile()) return { legacyCanonical, true, true };
    if (legacyOld.existsAsFile())       return { legacyOld, true, true };
    return { rootValid ? instrumentsFileIn (root) : legacyCanonical, false, false };
}

std::vector<Favorite> loadInstrumentsWithSelfHeal (const juce::File& root,
                                                   const juce::File& legacyCanonical,
                                                   const juce::File& legacyOld)
{
    return readInstrumentsChecked (root, legacyCanonical, legacyOld).favs;
}

InstrumentsRead readInstrumentsChecked (const juce::File& root,
                                        const juce::File& legacyCanonical,
                                        const juce::File& legacyOld)
{
    const auto src = resolveInstrumentsSource (root, legacyCanonical, legacyOld);
    InstrumentsRead r;
    r.source = src.file;
    if (! src.found) return r;              // nothing configured yet -> empty, not suspicious
    r.favs = readFavorites (src.file);
    r.suspiciousEmpty = r.favs.empty();     // the file is there but yields nothing -> retry-worthy
    if (src.fromLegacy && root.getFullPathName().isNotEmpty() && ! r.favs.empty())
        writeInstruments (root, r.favs);    // self-heal seed into the backup-friendly root
    return r;
}
}
