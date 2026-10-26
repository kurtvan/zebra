#include "library/Library.hpp"
#include "library/PresetParsing.hpp"

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
