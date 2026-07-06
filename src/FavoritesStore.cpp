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
            out.push_back (f);
        }
    return out;
}

juce::File favoritesConfigFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
              .getChildFile ("Custos").getChildFile ("favorites.json");
}

void writeFavorites (const juce::File& file, const std::vector<Favorite>& favs)
{
    file.getParentDirectory().createDirectory();
    file.replaceWithText (favoritesToJson (favs));
}

std::vector<Favorite> readFavorites (const juce::File& file)
{
    if (! file.existsAsFile()) return {};
    return favoritesFromJson (file.loadFileAsString());
}
}
