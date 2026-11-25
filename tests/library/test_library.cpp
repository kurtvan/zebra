#include "library/Library.hpp"
#include "library/PresetParsing.hpp"
#include "library/PresetStepping.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace gv::library;

TEST_CASE("preset paths give category, subcategory, author and name")
{
    auto info = ParsePresetPath("Dancer/Glowsticks Mirror/shifter - mandala.milk");
    CHECK(info.category == "Dancer");
    CHECK(info.subcategory == "Glowsticks Mirror");
    CHECK(info.author == "shifter");
    CHECK(info.name == "mandala");

    info = ParsePresetPath("Reaction/Liquid Simmering/fed + flexi + geiss - tokamak repellor tech twist mix.milk");
    CHECK(info.author == "fed + flexi + geiss");
    CHECK(info.name == "tokamak repellor tech twist mix");

    info = ParsePresetPath("Geometric/Squares/Martin - QBikal - Surface Turbulence.milk");
    CHECK(info.author == "Martin");
    CHECK(info.name == "QBikal - Surface Turbulence"); // only the first " - " separates the author

    info = ParsePresetPath("Drawing/Trails/xtramartin (894).milk");
    CHECK(info.author.empty());
    CHECK(info.name == "xtramartin (894)");

    info = ParsePresetPath("! Transition/Fast transition to black - levels effect === Goody's Lightning.milk");
    CHECK(info.author.empty()); // a description, not an author
    CHECK(info.name == "Fast transition to black - levels effect === Goody's Lightning");

    info = ParsePresetPath("Drawing/Explosions/Stahlregen & Boz & Nitorami - Machine Code.milk");
    CHECK(info.author == "Stahlregen & Boz & Nitorami");

    info = ParsePresetPath("loose.milk"); // one level shallower
    CHECK(info.category.empty());
    CHECK(info.subcategory.empty());
    CHECK(info.name == "loose");

    info = ParsePresetPath("A/B/C/deep - one.MILK"); // deeper: first two folders
    CHECK(info.category == "A");
    CHECK(info.subcategory == "B");
    CHECK(info.name == "one");
}

TEST_CASE("texture names come from sampler declarations, minus prefixes and built-ins")
{
    const std::string preset = R"(
comp_1=`shader_body {
comp_2=`  ret = tex2D(sampler_worms, uv) + tex2D(sampler_fw_Clouds, uv) * tex2D(sampler_pc_lichen, uv);
comp_3=`  ret += tex2D(sampler_main, uv) + GetBlur1(uv) + tex2D(sampler_blur2, uv);
comp_4=`  ret += tex2D(sampler_noise_lq, uv) + tex3D(sampler_noisevol_hq, uv.xyy) + tex2D(sampler_rand05_smalltiled, uv);
comp_5=`  ret += tex2D(sampler_pw_main, uv); float x = texsampler_fake; }
)";
    const auto names = ExtractTextureNames(preset);
    CHECK(names == std::set<std::string>{"worms", "clouds", "lichen"});
}

TEST_CASE("texture names follow projectM: any two-letter prefix, built-ins and random images left out")
{
    const std::string preset = R"(
warp_1=`shader_body { ret = tex2D(sampler_WF_Rock, uv).xyz + tex2D(sampler_cp_Bark, uv).xyz + tex2D(sampler_xy_odd, uv).xyz;
warp_2=`  ret += tex2D(sampler_noise_lq_lite, uv).xyz + tex2D(sampler_noise_mq, uv).xyz + tex2D(sampler_idlem, uv).xyz;
warp_3=`  ret += tex2D(sampler_fc_idleheadphones, uv).xyz + tex2D(sampler_rand12, uv).xyz + tex2D(sampler_noise_custom, uv).xyz;
warp_4=`  ret += tex2D(sampler_ab, uv).xyz + tex2D(sampler_fw_main, uv).xyz + tex2D(sampler_blur3, uv).xyz; }
)";
    // "noise_custom" is a file, not projectM's noise; "ab" is too short to have a prefix.
    CHECK(ExtractTextureNames(preset) == std::set<std::string>{"rock", "bark", "odd", "noise_custom", "ab"});
}

TEST_CASE("PresetTextures reads a preset file")
{
    const auto file = std::filesystem::temp_directory_path() / "gv_test_preset_textures.milk";
    std::ofstream(file) << "comp_1=`ret = tex2D(sampler_fw_Worms, uv) + tex2D(sampler_clouds, uv);\n";
    CHECK(PresetTextures(file) == std::vector<std::string>{"clouds", "worms"});
    CHECK(PresetTextures(file.string() + ".missing").empty());
    std::filesystem::remove(file);
}

TEST_CASE("search matches all words in name, author and tags, and filters")
{
    const auto dir = std::filesystem::temp_directory_path() / "gv_test_library_search";
    std::filesystem::remove_all(dir);
    for (const char* file : {"Dancer/Petals/shifter - pink petals.milk", "Dancer/Whirl/ORB - Smoke and Fire.milk",
                             "Reaction/Cloudy/ORB - Ethereal Plain.milk", "Reaction/Cloudy/martin - foggy notion.milk"})
    {
        std::filesystem::create_directories((dir / file).parent_path());
        std::ofstream(dir / file) << "[preset00]\n";
    }
    const auto library = Library::Scan(dir, Library());
    REQUIRE(library.Presets().size() == 4);

    auto names = [&](const std::vector<int>& indices) {
        std::set<std::string> result;
        for (int i : indices)
            result.insert(library.Presets()[static_cast<size_t>(i)].name);
        return result;
    };
    const std::set<std::string> none;
    CHECK(names(Search(library, {"orb", "", "", false, false}, none)) == std::set<std::string>{"Smoke and Fire", "Ethereal Plain"});
    CHECK(names(Search(library, {"ORB cloudy", "", "", false, false}, none)) == std::set<std::string>{"Ethereal Plain"});
    CHECK(names(Search(library, {"", "Dancer", "", false, false}, none)).size() == 2);
    CHECK(names(Search(library, {"", "Reaction", "Cloudy", false, false}, none)).size() == 2);
    CHECK(names(Search(library, {"", "", "", true, false}, {"Dancer/Petals/shifter - pink petals.milk"})) == std::set<std::string>{"pink petals"});

    const auto categories = library.Categories();
    REQUIRE(categories.size() == 2);
    CHECK(categories[0].name == "Dancer");
    CHECK(categories[0].count == 2);
    CHECK(categories[1].subcategories.at("Cloudy") == 2);
    std::filesystem::remove_all(dir);
}

TEST_CASE("rescanning reuses unchanged files and keeps what was learned; the index round-trips")
{
    const auto dir = std::filesystem::temp_directory_path() / "gv_test_library_scan";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "A" / "B");
    std::ofstream(dir / "A/B/x - one.milk") << "comp=`ret = tex2D(sampler_worms, uv);\n";
    std::ofstream(dir / "A/B/x - two.milk") << "[preset00]\n";

    auto library = Library::Scan(dir, Library());
    REQUIRE(library.Presets().size() == 2);
    CHECK(library.Find("A/B/x - one.milk")->textures == std::vector<std::string>{"worms"});
    CHECK(library.IdForPath(dir / "A/B/x - one.milk") == "A/B/x - one.milk");
    CHECK(library.IdForPath("/somewhere/else.milk").empty());

    CHECK(library.Record(dir / "A/B/x - one.milk", 42.0, ""));
    CHECK(library.Record(dir / "A/B/x - two.milk", 0.0, "Could not parse"));
    CHECK_FALSE(library.Record(dir / "A/B/x - one.milk", 42.1, "")); // no meaningful change

    const auto indexFile = dir / "library.json";
    REQUIRE(library.Save(indexFile).empty());
    Library loaded;
    REQUIRE(loaded.Load(indexFile));
    CHECK(loaded.Find("A/B/x - one.milk")->frameMs == 42.0);
    CHECK(loaded.Find("A/B/x - two.milk")->broken);

    const auto rescanned = Library::Scan(dir, loaded);
    CHECK(rescanned.Find("A/B/x - one.milk")->frameMs == 42.0); // unchanged file: measurement kept
    CHECK(rescanned.Find("A/B/x - two.milk")->broken);
    std::filesystem::remove_all(dir);
}

TEST_CASE("missing textures are those not found in the texture folders")
{
    const auto dir = std::filesystem::temp_directory_path() / "gv_test_textures";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "sub");
    std::ofstream(dir / "Worms.JPG") << "x";
    std::ofstream(dir / "sub" / "clouds.png") << "x";
    const auto available = AvailableTextures({dir});
    CHECK(available == std::set<std::string>{"worms", "clouds"});

    PresetInfo preset;
    preset.textures = {"worms", "rose", "clouds", "pic"};
    CHECK(MissingTextures(preset, available) == std::vector<std::string>{"rose", "pic"});
    std::filesystem::remove_all(dir);
}

TEST_CASE("favourites toggle and persist")
{
    const auto file = std::filesystem::temp_directory_path() / "gv_test_favourites.json";
    Favourites favourites;
    favourites.Toggle("A/b.milk");
    favourites.Toggle("C/d.milk");
    favourites.Toggle("C/d.milk");
    REQUIRE(favourites.Save(file).empty());
    Favourites loaded;
    REQUIRE(loaded.Load(file));
    CHECK(loaded.Ids() == std::set<std::string>{"A/b.milk"});
    std::filesystem::remove(file);
}

namespace {

/// A scanned library of `files` (ids) in a fresh temp folder `name`; `broken` ones are recorded as broken.
auto SteppingLibrary(const char* name, const std::vector<std::string>& files, const std::vector<std::string>& broken = {}) -> Library
{
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    for (const auto& file : files)
    {
        std::filesystem::create_directories((dir / file).parent_path());
        std::ofstream(dir / file) << "[preset00]\n";
    }
    auto library = Library::Scan(dir, Library());
    for (const auto& file : broken)
        library.Record(dir / file, 0.0, "Could not parse");
    std::filesystem::remove_all(dir);
    return library;
}

} // namespace

TEST_CASE("stepping goes through the preset's own folder, wrapping at both ends and skipping broken presets")
{
    const auto library = SteppingLibrary("gv_test_stepping", {"A/1.milk", "A/2.milk", "A/3.milk", "A/4.milk", "A/sub/5.milk", "B/6.milk"}, {"A/3.milk"});
    const auto at = [&](const char* id) { return library.PathForId(id); };
    CHECK(StepPreset(library, at("A/1.milk"), 1) == at("A/2.milk"));
    CHECK(StepPreset(library, at("A/2.milk"), 1) == at("A/4.milk")); // 3 is broken
    CHECK(StepPreset(library, at("A/4.milk"), 1) == at("A/1.milk")); // wraps; not into A/sub
    CHECK(StepPreset(library, at("A/1.milk"), -1) == at("A/4.milk")); // wraps back
    CHECK(StepPreset(library, at("A/4.milk"), -1) == at("A/2.milk"));
    CHECK(StepPreset(library, at("A/3.milk"), 1) == at("A/4.milk")); // a broken current preset steps on from its place
    CHECK(StepPreset(library, at("A/3.milk"), -1) == at("A/2.milk"));
    CHECK(FolderOf(library, at("A/sub/5.milk")) == "A/sub");
    CHECK(FolderOf(library, "/elsewhere/x.milk") == std::nullopt);

    // Not in the library: nothing to step through. No preset chosen: the whole library.
    CHECK(StepPreset(library, "/elsewhere/x.milk", 1) == std::nullopt);
    CHECK(StepPreset(library, std::filesystem::path(), 1) == at("A/1.milk"));
    CHECK(StepPreset(library, std::filesystem::path(), -1) == at("B/6.milk"));

    // A given folder (for a folder picker): a preset outside it starts at its first or last.
    CHECK(StepPreset(library, "A", at("B/6.milk"), 1) == at("A/1.milk"));
    CHECK(StepPreset(library, "A", at("B/6.milk"), -1) == at("A/4.milk"));
    CHECK(StepPreset(library, "nowhere", at("A/1.milk"), 1) == std::nullopt);
}

TEST_CASE("stepping in a folder holding only the current preset gives nothing")
{
    const auto library = SteppingLibrary("gv_test_stepping_one", {"A/only.milk", "A/broken.milk", "B/other.milk"}, {"A/broken.milk"});
    CHECK(StepPreset(library, library.PathForId("A/only.milk"), 1) == std::nullopt);
    CHECK(StepPreset(library, library.PathForId("A/only.milk"), -1) == std::nullopt);
    CHECK(StepPreset(library, library.PathForId("B/other.milk"), 1) == std::nullopt);
}

TEST_CASE("random picks from the whole library, never the current or a broken preset, and repeats with the same seed")
{
    const auto library = SteppingLibrary("gv_test_random", {"A/1.milk", "A/2.milk", "B/3.milk", "C/x/4.milk"}, {"A/2.milk"});
    const auto current = library.PathForId("A/1.milk");
    std::mt19937 random(7);
    std::set<std::filesystem::path> seen;
    std::vector<std::filesystem::path> picks;
    for (int i = 0; i < 50; ++i)
    {
        const auto pick = RandomPreset(library, current, random);
        REQUIRE(pick);
        picks.push_back(*pick);
        seen.insert(*pick);
    }
    CHECK(seen == std::set<std::filesystem::path>{library.PathForId("B/3.milk"), library.PathForId("C/x/4.milk")});

    std::mt19937 again(7);
    for (const auto& pick : picks)
        CHECK(RandomPreset(library, current, again) == pick);

    const auto alone = SteppingLibrary("gv_test_random_one", {"A/1.milk", "A/2.milk"}, {"A/2.milk"});
    CHECK(RandomPreset(alone, alone.PathForId("A/1.milk"), random) == std::nullopt);
    CHECK(RandomPreset(alone, std::filesystem::path(), random) == alone.PathForId("A/1.milk"));
}

TEST_CASE("the real preset library indexes and searches fast", "[.][benchmark]")
{
    // Run explicitly: gv_tests "[benchmark]" (uses the author's Cream of the Crop folder).
    const std::filesystem::path root = std::string(std::getenv("HOME")) + "/projects/visualizers/libprojectM-4.1.4/presets/milk";
    if (!std::filesystem::exists(root))
        SKIP("library not found");
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now();
    const auto library = Library::Scan(root, Library());
    const double scanMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    start = Clock::now();
    const auto rescanned = Library::Scan(root, library);
    const double rescanMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    start = Clock::now();
    const auto hits = Search(library, {"orb fire", "", "", false, false}, {});
    const double searchMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    WARN("presets " << library.Presets().size() << ", first scan " << scanMs << " ms, rescan " << rescanMs << " ms, search " << searchMs
                    << " ms (" << hits.size() << " hits)");
    CHECK(library.Presets().size() > 9000);
    CHECK(searchMs < 16.0);
}

TEST_CASE("stepping under a chosen folder takes in its subfolders; \"\" is the whole library")
{
    const auto library = SteppingLibrary("gv_test_stepping_under", {"A/1.milk", "A/sub/2.milk", "A/sub/3.milk", "AB/4.milk", "B/5.milk"}, {"A/sub/3.milk"});
    const auto at = [&](const char* id) { return library.PathForId(id); };
    CHECK(StepPresetUnder(library, "A", at("A/1.milk"), 1) == at("A/sub/2.milk"));
    CHECK(StepPresetUnder(library, "A", at("A/sub/2.milk"), 1) == at("A/1.milk")); // 3 is broken; AB isn't under A
    CHECK(StepPresetUnder(library, "A/sub", at("B/5.milk"), 1) == at("A/sub/2.milk"));
    CHECK(StepPresetUnder(library, "", at("AB/4.milk"), 1) == at("B/5.milk"));
    CHECK(StepPresetUnder(library, "", "/elsewhere/x.milk", -1) == at("B/5.milk"));
    CHECK(StepPresetUnder(library, "nowhere", at("A/1.milk"), 1) == std::nullopt);
}

TEST_CASE("the folder tree lists the categories, with the open one's subcategories, and maps to queries")
{
    const auto library = SteppingLibrary("gv_test_folder_rows", {"top.milk", "A/1.milk", "A/x/2.milk", "A/y/3.milk", "A/y/deep/4.milk", "B/5.milk"});
    CHECK(FolderRows(library, "") == std::vector<FolderRow>{{"", "", 0, 6}, {"A", "", 1, 4}, {"B", "", 1, 1}});
    CHECK(FolderRows(library, "A") ==
          std::vector<FolderRow>{{"", "", 0, 6}, {"A", "", 1, 4}, {"A", "x", 2, 1}, {"A", "y", 2, 2}, {"B", "", 1, 1}});

    CHECK(FolderName("A", "y") == "A/y");
    CHECK(FolderName("A", "") == "A");
    CHECK(FolderName("", "").empty());
    const auto query = FolderQuery("A/y/deep");
    CHECK(query.category == "A");
    CHECK(query.subcategory == "y");
    CHECK(FolderQuery("").category.empty());
    // A folder's query shows what stepping under it goes through.
    CHECK(Search(library, FolderQuery("A/y"), {}).size() == 2);
    CHECK(Search(library, FolderQuery("A"), {}).size() == 4);
}
