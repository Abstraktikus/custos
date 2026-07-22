#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "FavoritesStore.h"

using namespace custos;
using Catch::Approx;

TEST_CASE ("favorites JSON round-trips")
{
    std::vector<Favorite> favs { { "Diva", "C:/x/Diva.vst3", 1, -3.0f, "u-he" },
                                 { "DX7",  "C:/x/DX7.vst3",  2,  0.0f, "Arturia" } };
    const auto back = favoritesFromJson (favoritesToJson (favs));
    REQUIRE (back.size() == 2);
    REQUIRE (back[0].name == "Diva");
    REQUIRE (back[0].path == "C:/x/Diva.vst3");
    REQUIRE (back[0].favOrder == 1);
    REQUIRE (back[0].gainDb == Approx (-3.0f));
    REQUIRE (back[0].brand == "u-he");
    REQUIRE (back[1].name == "DX7");
    REQUIRE (back[1].brand == "Arturia");
}

TEST_CASE ("writeFavorites/readFavorites round-trip via a temp file")
{
    auto tmp = juce::File::createTempFile (".json");
    writeFavorites (tmp, { { "Pigments", "C:/x/Pigments.vst3", 5, 1.5f } });
    const auto back = readFavorites (tmp);
    REQUIRE (back.size() == 1);
    REQUIRE (back[0].name == "Pigments");
    REQUIRE (back[0].gainDb == Approx (1.5f));
    tmp.deleteFile();
}

TEST_CASE ("readFavorites on a missing file returns empty")
{
    REQUIRE (readFavorites (juce::File ("C:/nonexistent/nope.json")).empty());
}

TEST_CASE ("Favorite round-trips controlType / paramDown / paramUp")
{
    std::vector<custos::Favorite> in;
    custos::Favorite f;
    f.name = "Analog Lab V"; f.path = "C:/AL.vst3"; f.favOrder = 3; f.gainDb = -3.0f;
    f.brand = "Arturia"; f.slots = 500;
    f.controlType = "PARAM"; f.paramDown = 498; f.paramUp = 499;
    in.push_back (f);

    const auto out = custos::favoritesFromJson (custos::favoritesToJson (in));
    REQUIRE (out.size() == 1);
    REQUIRE (out[0].controlType == "PARAM");
    REQUIRE (out[0].paramDown == 498);
    REQUIRE (out[0].paramUp  == 499);
}

TEST_CASE ("legacy Favorite JSON without controlType defaults to PRESET")
{
    const auto out = custos::favoritesFromJson (R"([{"name":"X","path":"C:/x.vst3","favOrder":1}])");
    REQUIRE (out.size() == 1);
    REQUIRE (out[0].controlType == "PRESET");
    REQUIRE (out[0].paramDown == 0);
    REQUIRE (out[0].paramUp  == 0);
}

TEST_CASE ("Favorite round-trips classId (v4)")
{
    std::vector<custos::Favorite> in;
    custos::Favorite f;
    f.name = "Diva"; f.path = "C:/x/Diva.vst3"; f.favOrder = 1; f.gainDb = -3.0f;
    f.brand = "u-he"; f.slots = 2797; f.classId = "u-he Diva|Diva.vst3|...";
    in.push_back (f);

    const auto out = custos::favoritesFromJson (custos::favoritesToJson (in));
    REQUIRE (out.size() == 1);
    REQUIRE (out[0].classId == "u-he Diva|Diva.vst3|...");
}

TEST_CASE ("legacy Favorite JSON without classId loads as empty (v4 back-compat)")
{
    const auto out = custos::favoritesFromJson (R"([{"name":"X","path":"C:/x.vst3","favOrder":1}])");
    REQUIRE (out.size() == 1);
    REQUIRE (out[0].classId.isEmpty());
}

TEST_CASE ("readInstruments prefers the new file, else migrates the legacy file")
{
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("custos_migtest");
    tmp.createDirectory();
    auto legacy = tmp.getChildFile ("favorites.json");
    auto neu    = tmp.getChildFile ("instruments.json");
    legacy.deleteFile(); neu.deleteFile();

    custos::writeFavorites (legacy, { { "L", "C:/l.vst3", 1, 0.0f } });
    auto migrated = custos::readInstruments (neu, legacy);   // new absent -> read legacy
    REQUIRE (migrated.size() == 1);
    REQUIRE (migrated[0].name == "L");

    custos::writeFavorites (neu, { { "N", "C:/n.vst3", 1, 0.0f } });
    auto fresh = custos::readInstruments (neu, legacy);      // new present -> ignore legacy
    REQUIRE (fresh.size() == 1);
    REQUIRE (fresh[0].name == "N");

    legacy.deleteFile(); neu.deleteFile();
    REQUIRE (custos::readInstruments (neu, legacy).empty());   // neither present -> empty

    legacy.deleteFile(); neu.deleteFile(); tmp.deleteRecursively();
}

TEST_CASE ("instrumentsFileIn / instrumentsTargetFor pick the right file")
{
    juce::File root ("C:/backup/Custos");
    REQUIRE (instrumentsFileIn (root).getFullPathName()
             == juce::File ("C:/backup/Custos/instruments.json").getFullPathName());

    // Non-empty root -> under the root.
    REQUIRE (instrumentsTargetFor (root) == instrumentsFileIn (root));

    // Empty/invalid root -> legacy %APPDATA% canonical file.
    REQUIRE (instrumentsTargetFor (juce::File()) == instrumentsConfigFile());
}

TEST_CASE ("writeInstruments lands at <root>/instruments.json")
{
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    REQUIRE (writeInstruments (root, { { "Zebra2", "C:/x/Zebra2.vst3", 1, 0.0f, "u-he" } }));
    auto target = root.getChildFile ("instruments.json");
    REQUIRE (target.existsAsFile());
    const auto back = readFavorites (target);
    REQUIRE (back.size() == 1);
    REQUIRE (back[0].name == "Zebra2");
    root.deleteRecursively();
}

TEST_CASE ("writeInstruments returns false when the target cannot be written")
{
    auto tmp = juce::File::createTempFile (""); tmp.deleteFile(); tmp.createDirectory();
    auto blocker = tmp.getChildFile ("blocker"); blocker.replaceWithText ("x");  // a FILE
    auto root = blocker.getChildFile ("sub");   // parent 'blocker' is a file -> mkdir + write must fail
    REQUIRE_FALSE (writeInstruments (root, { { "X", "C:/x.vst3", 1, 0.0f } }));
    tmp.deleteRecursively();
}

TEST_CASE ("writeInstruments does not report false success over a pre-existing read-only file")
{
    auto root = juce::File::createTempFile (""); root.deleteFile(); root.createDirectory();
    auto target = instrumentsFileIn (root);

    REQUIRE (writeInstruments (root, { { "Stale", "C:/old.vst3", 1, 0.0f } }));
    target.setReadOnly (true);

    REQUIRE_FALSE (writeInstruments (root, { { "New", "C:/new.vst3", 1, 0.0f } }));

    // The stale content must survive the failed overwrite untouched.
    const auto back = readFavorites (target);
    REQUIRE (back.size() == 1);
    REQUIRE (back[0].name == "Stale");

    target.setReadOnly (false);
    root.deleteRecursively();
}

TEST_CASE ("resolveInstrumentsSource honours tier order root > canonical > old")
{
    auto dir = juce::File::createTempFile (""); dir.deleteFile(); dir.createDirectory();
    auto root  = dir.getChildFile ("root");  root.createDirectory();
    auto rootF = root.getChildFile ("instruments.json");
    auto canon = dir.getChildFile ("instruments.json");   // legacy canonical
    auto old   = dir.getChildFile ("favorites.json");     // legacy old

    // None present -> not found, file points at tier 1 target.
    auto r0 = resolveInstrumentsSource (root, canon, old);
    REQUIRE_FALSE (r0.found);
    REQUIRE (r0.file == rootF);

    // Only old present -> tier 3, fromLegacy.
    writeFavorites (old, { { "O", "C:/o.vst3", 1, 0.0f } });
    auto r3 = resolveInstrumentsSource (root, canon, old);
    REQUIRE (r3.found); REQUIRE (r3.fromLegacy); REQUIRE (r3.file == old);

    // Canonical present -> tier 2 wins over old.
    writeFavorites (canon, { { "C", "C:/c.vst3", 1, 0.0f } });
    auto r2 = resolveInstrumentsSource (root, canon, old);
    REQUIRE (r2.found); REQUIRE (r2.fromLegacy); REQUIRE (r2.file == canon);

    // Root present -> tier 1 wins, not legacy.
    writeFavorites (rootF, { { "R", "C:/r.vst3", 1, 0.0f } });
    auto r1 = resolveInstrumentsSource (root, canon, old);
    REQUIRE (r1.found); REQUIRE_FALSE (r1.fromLegacy); REQUIRE (r1.file == rootF);

    // Empty root -> tier 1 skipped, resolves to canonical.
    auto re = resolveInstrumentsSource (juce::File(), canon, old);
    REQUIRE (re.found); REQUIRE (re.fromLegacy); REQUIRE (re.file == canon);

    dir.deleteRecursively();
}

TEST_CASE ("loadInstrumentsWithSelfHeal seeds the root from a legacy file, once")
{
    auto dir = juce::File::createTempFile (""); dir.deleteFile(); dir.createDirectory();
    auto root  = dir.getChildFile ("root");  root.createDirectory();
    auto rootF = root.getChildFile ("instruments.json");
    auto canon = dir.getChildFile ("instruments.json");
    auto old   = dir.getChildFile ("favorites.json");

    writeFavorites (canon, { { "Legacy", "C:/l.vst3", 1, 0.0f } });
    REQUIRE_FALSE (rootF.existsAsFile());

    // Legacy read -> data returned AND seeded into the root.
    auto favs = loadInstrumentsWithSelfHeal (root, canon, old);
    REQUIRE (favs.size() == 1);
    REQUIRE (favs[0].name == "Legacy");
    REQUIRE (rootF.existsAsFile());                 // self-heal wrote it

    // Second call reads tier 1 and does not touch legacy; still correct.
    auto again = loadInstrumentsWithSelfHeal (root, canon, old);
    REQUIRE (again.size() == 1);
    REQUIRE (again[0].name == "Legacy");

    dir.deleteRecursively();
}

TEST_CASE ("loadInstrumentsWithSelfHeal returns empty and seeds nothing when no source exists")
{
    auto dir = juce::File::createTempFile (""); dir.deleteFile(); dir.createDirectory();
    auto root  = dir.getChildFile ("root");  root.createDirectory();
    auto favs = loadInstrumentsWithSelfHeal (root, dir.getChildFile ("instruments.json"),
                                                   dir.getChildFile ("favorites.json"));
    REQUIRE (favs.empty());
    REQUIRE_FALSE (root.getChildFile ("instruments.json").existsAsFile());   // no seed from nothing
    dir.deleteRecursively();
}

TEST_CASE ("readInstrumentsChecked flags a source that exists but reads empty (OneDrive hydration)")
{
    auto dir = juce::File::createTempFile (""); dir.deleteFile(); dir.createDirectory();
    auto root  = dir.getChildFile ("root");  root.createDirectory();
    auto rootF = root.getChildFile ("instruments.json");
    auto canon = dir.getChildFile ("instruments.json");
    auto old   = dir.getChildFile ("favorites.json");

    SECTION ("valid root file: data, not suspicious")
    {
        writeFavorites (rootF, { { "Good", "C:/g.vst3", 1, 0.0f } });
        auto r = readInstrumentsChecked (root, canon, old);
        REQUIRE (r.favs.size() == 1);
        REQUIRE_FALSE (r.suspiciousEmpty);
        REQUIRE (r.source == rootF);
    }
    SECTION ("root file exists but is empty: suspicious")
    {
        rootF.replaceWithText ("");
        auto r = readInstrumentsChecked (root, canon, old);
        REQUIRE (r.favs.empty());
        REQUIRE (r.suspiciousEmpty);
        REQUIRE (r.source == rootF);
    }
    SECTION ("root file exists but is garbage: suspicious")
    {
        rootF.replaceWithText ("not json at all");
        auto r = readInstrumentsChecked (root, canon, old);
        REQUIRE (r.favs.empty());
        REQUIRE (r.suspiciousEmpty);
    }
    SECTION ("no source anywhere: empty but NOT suspicious (first configuration)")
    {
        auto r = readInstrumentsChecked (root, canon, old);
        REQUIRE (r.favs.empty());
        REQUIRE_FALSE (r.suspiciousEmpty);
    }
    SECTION ("legacy-only valid: data via migration read, not suspicious, self-heal seeds root")
    {
        writeFavorites (canon, { { "Legacy", "C:/l.vst3", 1, 0.0f } });
        auto r = readInstrumentsChecked (root, canon, old);
        REQUIRE (r.favs.size() == 1);
        REQUIRE_FALSE (r.suspiciousEmpty);
        REQUIRE (rootF.existsAsFile());   // same self-heal as loadInstrumentsWithSelfHeal
    }
    dir.deleteRecursively();
}
