#include "model/PerformanceJson.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace gv::model;

namespace {

auto MakeScene(const std::string& id, int cc) -> Scene
{
    Scene scene;
    scene.id = id;
    scene.presetPath = "/presets/" + id + ".milk";
    scene.cc20Value = cc;
    return scene;
}

auto HasIssueAt(const LoadResult& result, const std::string& path, Issue::Severity severity = Issue::Severity::Error) -> bool
{
    for (const auto& issue : result.issues)
    {
        if (issue.path == path && issue.severity == severity)
            return true;
    }
    return false;
}

auto HasIssueAt(const std::vector<Issue>& issues, const std::string& path) -> bool
{
    for (const auto& issue : issues)
    {
        if (issue.path == path)
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("a performance survives a save/load round trip")
{
    Performance performance;
    performance.name = "Show";
    auto a = MakeScene("intro", 0);
    a.name = "Intro";
    a.video.enabled = true;
    a.video.source.type = VideoSourceType::File;
    a.video.source.path = "/videos/intro.mp4";
    a.video.mix = 0.5f;
    a.video.lumaMin = 0.25f;
    a.video.restartOnEnter = false;
    a.video.contrast = 1.4f;
    a.video.saturation = 0.0f;
    auto b = MakeScene("cave", 7);
    b.video.enabled = true;
    b.video.source.type = VideoSourceType::Syphon;
    b.video.source.appName = "Resolume";
    b.video.source.serverName = "Composition";
    performance.scenes = {a, b};

    const auto result = ParsePerformance(SerializePerformance(performance));
    REQUIRE(result.ok);
    REQUIRE(result.issues.empty());
    const auto& loaded = result.performance;
    CHECK(loaded.name == "Show");
    REQUIRE(loaded.scenes.size() == 2);
    CHECK(loaded.scenes[0].id == "intro");
    CHECK(loaded.scenes[0].name == "Intro");
    CHECK(loaded.scenes[0].presetPath == "/presets/intro.milk");
    CHECK(loaded.scenes[0].video.enabled);
    CHECK(loaded.scenes[0].video.source.path == "/videos/intro.mp4");
    CHECK(loaded.scenes[0].video.mix == 0.5f);
    CHECK(loaded.scenes[0].video.lumaMin == 0.25f);
    CHECK_FALSE(loaded.scenes[0].video.restartOnEnter);
    CHECK(loaded.scenes[0].video.contrast == 1.4f);
    CHECK(loaded.scenes[0].video.saturation == 0.0f);
    CHECK(loaded.scenes[1].video.contrast == 1.0f); // defaults
    CHECK(loaded.scenes[1].cc20Value == 7);
    CHECK(loaded.scenes[1].video.source.type == VideoSourceType::Syphon);
    CHECK(loaded.scenes[1].video.source.appName == "Resolume");
    CHECK(loaded.scenes[1].video.source.serverName == "Composition");
}

TEST_CASE("unparseable or structurally wrong files are rejected with a message")
{
    CHECK_FALSE(ParsePerformance("{not json").ok);
    CHECK_FALSE(ParsePerformance("[]").ok);
    CHECK(HasIssueAt(ParsePerformance(R"({"scenes": []})"), "/version"));
    CHECK(HasIssueAt(ParsePerformance(R"({"version": 99, "scenes": []})"), "/version"));
    CHECK(HasIssueAt(ParsePerformance(R"({"version": 1})"), "/scenes"));
    CHECK(HasIssueAt(ParsePerformance(R"({"version": 1, "scenes": [{"id": 3}]})"), "/scenes/0/id"));
}

TEST_CASE("an empty performance is valid")
{
    const auto result = ParsePerformance(R"({"version": 1, "scenes": []})");
    CHECK(result.ok);
}

TEST_CASE("validation rules")
{
    Performance performance;

    SECTION("duplicate ids")
    {
        performance.scenes = {MakeScene("a", 0), MakeScene("a", 1)};
        CHECK(HasIssueAt(Validate(performance), "/scenes/1/id"));
    }
    SECTION("empty id and preset path")
    {
        auto scene = MakeScene("", 0);
        scene.presetPath.clear();
        performance.scenes = {scene};
        const auto issues = Validate(performance);
        CHECK(HasIssueAt(issues, "/scenes/0/id"));
        CHECK(HasIssueAt(issues, "/scenes/0/preset/path"));
    }
    SECTION("CC 20 value out of range or duplicated")
    {
        performance.scenes = {MakeScene("a", 128), MakeScene("b", 5), MakeScene("c", 5), MakeScene("d", -1)};
        const auto issues = Validate(performance);
        CHECK(HasIssueAt(issues, "/scenes/0/midi/cc20Value"));
        CHECK_FALSE(HasIssueAt(issues, "/scenes/1/midi/cc20Value"));
        CHECK(HasIssueAt(issues, "/scenes/2/midi/cc20Value"));
        CHECK(HasIssueAt(issues, "/scenes/3/midi/cc20Value"));
    }
    SECTION("mix, luma, contrast and saturation ranges")
    {
        auto scene = MakeScene("a", 0);
        scene.video.mix = 1.5f;
        scene.video.lumaMin = -0.1f;
        scene.video.contrast = 2.5f;
        scene.video.saturation = -1.f;
        performance.scenes = {scene};
        const auto issues = Validate(performance);
        CHECK(HasIssueAt(issues, "/scenes/0/video/mix"));
        CHECK(HasIssueAt(issues, "/scenes/0/video/lumaMin"));
        CHECK(HasIssueAt(issues, "/scenes/0/video/contrast"));
        CHECK(HasIssueAt(issues, "/scenes/0/video/saturation"));
    }

    SECTION("files without contrast/saturation load with defaults")
    {
        const auto result = ParsePerformance(R"({"version": 1, "scenes": [{"id": "a", "preset": {"path": "/x.milk"}, "midi": {"cc20Value": 0},
            "video": {"enabled": true, "source": {"type": "file", "path": "/v.mp4"}, "mix": 1, "lumaMin": 0}}]})");
        REQUIRE(result.ok);
        CHECK(result.performance.scenes[0].video.contrast == 1.0f);
        CHECK(result.performance.scenes[0].video.saturation == 1.0f);
    }
    SECTION("enabled file video needs a path; Syphon without names means any server")
    {
        auto file = MakeScene("a", 0);
        file.video.enabled = true;
        auto syphon = MakeScene("b", 1);
        syphon.video.enabled = true;
        syphon.video.source.type = VideoSourceType::Syphon;
        performance.scenes = {file, syphon};
        const auto issues = Validate(performance);
        CHECK(HasIssueAt(issues, "/scenes/0/video/source/path"));
        CHECK_FALSE(HasIssueAt(issues, "/scenes/1/video/source"));
    }
    SECTION("more than 128 scenes")
    {
        for (int i = 0; i < 129; ++i)
            performance.scenes.push_back(MakeScene("s" + std::to_string(i), i % 128));
        CHECK(HasIssueAt(Validate(performance), "/scenes"));
    }
}

TEST_CASE("a scene without a midi block is reported")
{
    const auto result = ParsePerformance(R"({"version": 1, "scenes": [{"id": "a", "preset": {"path": "/x.milk"}}]})");
    CHECK_FALSE(result.ok);
    CHECK(HasIssueAt(result, "/scenes/0/midi/cc20Value"));
}

TEST_CASE("missing files are warnings and the performance still loads")
{
    const auto dir = std::filesystem::temp_directory_path() / "gv_test_missing_files";
    std::filesystem::create_directories(dir);
    const auto preset = dir / "exists.milk";
    std::ofstream(preset) << "[preset00]\n";

    Performance performance;
    auto present = MakeScene("present", 0);
    present.presetPath = preset;
    auto missing = MakeScene("missing", 1);
    missing.presetPath = dir / "nope.milk";
    missing.video.enabled = true;
    missing.video.source.path = dir / "nope.mp4";
    performance.scenes = {present, missing};

    const auto file = dir / "performance.json";
    REQUIRE(SavePerformance(performance, file).empty());

    const auto result = LoadPerformance(file);
    CHECK(result.ok);
    CHECK_FALSE(result.performance.scenes[0].presetMissing);
    CHECK(result.performance.scenes[1].presetMissing);
    CHECK(result.performance.scenes[1].videoMissing);
    CHECK(HasIssueAt(result, "/scenes/1/preset/path", Issue::Severity::Warning));
    CHECK(HasIssueAt(result, "/scenes/1/video/source/path", Issue::Severity::Warning));

    std::filesystem::remove_all(dir);
}

TEST_CASE("CC 20 lookup and free values")
{
    Performance performance;
    performance.scenes = {MakeScene("a", 0), MakeScene("b", 1), MakeScene("c", 3)};
    CHECK(performance.SceneForCc20(3) == 2u);
    CHECK_FALSE(performance.SceneForCc20(2).has_value());
    CHECK(performance.FreeCc20Value() == 2);
}

TEST_CASE("playlists round-trip, validate and report missing entries")
{
    const auto dir = std::filesystem::temp_directory_path() / "gv_test_playlist";
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "a.milk") << "[preset00]\n";
    std::ofstream(dir / "b.milk") << "[preset00]\n";

    Performance performance;
    auto scene = MakeScene("p", 0);
    scene.presetPath.clear();
    scene.playlist.enabled = true;
    scene.playlist.presets = {dir / "a.milk", dir / "missing.milk", dir / "b.milk"};
    scene.playlist.intervalSeconds = 12.5;
    scene.playlist.shuffle = true;
    performance.scenes = {scene, MakeScene("plain", 1)};
    CHECK(Validate(performance).empty());

    const auto file = dir / "performance.json";
    REQUIRE(SavePerformance(performance, file).empty());
    const auto result = LoadPerformance(file);
    REQUIRE(result.ok);
    const auto& loaded = result.performance.scenes[0];
    CHECK(loaded.IsPlaylist());
    CHECK(loaded.playlist.presets.size() == 3);
    CHECK(loaded.playlist.intervalSeconds == 12.5);
    CHECK(loaded.playlist.shuffle);
    CHECK(loaded.FirstPreset() == dir / "a.milk");
    CHECK_FALSE(loaded.presetMissing);
    CHECK(loaded.playlistMissing == std::vector<bool>{false, true, false});
    CHECK(HasIssueAt(result, "/scenes/0/playlist/presets/1/path", Issue::Severity::Warning));
    CHECK_FALSE(result.performance.scenes[1].IsPlaylist());
    CHECK(result.performance.scenes[1].Presets().size() == 1);

    SECTION("an enabled playlist needs presets and a sane interval")
    {
        auto bad = performance;
        bad.scenes[0].playlist.presets.clear();
        bad.scenes[0].playlist.intervalSeconds = 0.2;
        const auto issues = Validate(bad);
        CHECK(HasIssueAt(issues, "/scenes/0/playlist/presets"));
        CHECK(HasIssueAt(issues, "/scenes/0/playlist/intervalSeconds"));
    }
    SECTION("a disabled playlist falls back to the single preset")
    {
        auto plain = performance;
        plain.scenes[0].playlist.enabled = false;
        CHECK(HasIssueAt(Validate(plain), "/scenes/0/preset/path")); // presetPath is empty
        plain.scenes[0].presetPath = dir / "b.milk";
        CHECK(Validate(plain).empty());
        CHECK(plain.scenes[0].FirstPreset() == dir / "b.milk");
    }
    std::filesystem::remove_all(dir);
}
