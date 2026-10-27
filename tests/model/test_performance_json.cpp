#include "model/PerformanceJson.hpp"

#include "model/Document.hpp"

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
    a.video.source.videoId = "intro";
    performance.videos = {PoolVideo{"intro", "Intro loop", "/videos/intro.mp4", 12.5, 0.5}};
    a.video.look.mix = 0.5f;
    a.video.look.lumaMin = 0.25f;
    a.video.restartOnEnter = false;
    a.video.look.contrast = 80.0f; // high contrast is allowed
    a.video.look.saturation = 0.0f;
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
    CHECK(loaded.scenes[0].video.source.videoId == "intro");
    REQUIRE(loaded.videos.size() == 1);
    CHECK(loaded.videos[0].id == "intro");
    CHECK(loaded.videos[0].name == "Intro loop");
    CHECK(loaded.videos[0].path == "/videos/intro.mp4");
    CHECK(loaded.videos[0].startSeconds == 12.5);
    CHECK(loaded.videos[0].speed == 0.5);
    CHECK(loaded.scenes[0].video.look.mix == 0.5f);
    CHECK(loaded.scenes[0].video.look.lumaMin == 0.25f);
    CHECK_FALSE(loaded.scenes[0].video.restartOnEnter);
    CHECK(loaded.scenes[0].video.look.contrast == 80.0f);
    CHECK(loaded.scenes[0].video.look.saturation == 0.0f);
    CHECK(loaded.scenes[1].video.look.contrast == 1.0f); // defaults
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
        scene.video.look.mix = 1.5f;
        scene.video.look.lumaMin = -0.1f;
        scene.video.look.contrast = 150.f;
        scene.video.look.saturation = -1.f;
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
        CHECK(result.performance.scenes[0].video.look.contrast == 1.0f);
        CHECK(result.performance.scenes[0].video.look.saturation == 1.0f);
    }
    SECTION("enabled file video needs a pool video; Syphon without names means any server")
    {
        auto file = MakeScene("a", 0);
        file.video.enabled = true;
        auto syphon = MakeScene("b", 1);
        syphon.video.enabled = true;
        syphon.video.source.type = VideoSourceType::Syphon;
        performance.scenes = {file, syphon};
        const auto issues = Validate(performance);
        CHECK(HasIssueAt(issues, "/scenes/0/video/source/video"));
        CHECK_FALSE(HasIssueAt(issues, "/scenes/1/video/source"));
        performance.scenes[0].video.source.videoId = "gone";
        CHECK(HasIssueAt(Validate(performance), "/scenes/0/video/source/video"));
    }
    SECTION("pool videos: unique ids, speed 0.1-4, start not negative")
    {
        performance.videos = {PoolVideo{"a", "", "/a.mp4", -1.0, 1.0}, PoolVideo{"a", "", "/b.mp4", 0.0, 5.0}, PoolVideo{"c", "", "/c.mp4", 0.0, 0.05}};
        const auto issues = Validate(performance);
        CHECK(HasIssueAt(issues, "/videos/0/startSeconds"));
        CHECK(HasIssueAt(issues, "/videos/1/id"));
        CHECK(HasIssueAt(issues, "/videos/1/speed"));
        CHECK(HasIssueAt(issues, "/videos/2/speed"));
        CHECK_FALSE(HasIssueAt(issues, "/videos/0/id"));
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
    missing.video.source.videoId = AddPoolVideo(performance, dir / "nope.mp4");
    performance.scenes = {present, missing};

    const auto file = dir / "performance.json";
    REQUIRE(SavePerformance(performance, file).empty());

    const auto result = LoadPerformance(file);
    CHECK(result.ok);
    CHECK_FALSE(result.performance.scenes[0].presetMissing);
    CHECK(result.performance.scenes[1].presetMissing);
    CHECK(result.performance.scenes[1].videoMissing);
    CHECK(HasIssueAt(result, "/scenes/1/preset/path", Issue::Severity::Warning));
    CHECK(result.performance.videos[0].missing);
    CHECK(HasIssueAt(result, "/videos/0/path", Issue::Severity::Warning));

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
    SECTION("beat and bar intervals round-trip and validate")
    {
        auto bars = performance;
        bars.scenes[0].playlist.unit = IntervalUnit::Bars;
        bars.scenes[0].playlist.intervalCount = 8;
        bars.scenes[0].playlist.intervalSeconds = 0.0; // ignored for bars
        CHECK(Validate(bars).empty());
        const auto reloaded = ParsePerformance(SerializePerformance(bars));
        REQUIRE(reloaded.ok);
        CHECK(reloaded.performance.scenes[0].playlist.unit == IntervalUnit::Bars);
        CHECK(reloaded.performance.scenes[0].playlist.intervalCount == 8);

        bars.scenes[0].playlist.intervalCount = 0;
        CHECK(HasIssueAt(Validate(bars), "/scenes/0/playlist/intervalCount"));
        CHECK(HasIssueAt(ParsePerformance(R"({"version": 1, "scenes": [{"id": "a", "midi": {"cc20Value": 0},
            "playlist": {"enabled": true, "presets": [{"path": "/a.milk"}], "intervalUnit": "fortnights"}}]})"), "/scenes/0/playlist/intervalUnit"));
    }
    SECTION("older playlists without a unit are in seconds")
    {
        const auto result = ParsePerformance(R"({"version": 1, "scenes": [{"id": "a", "midi": {"cc20Value": 0},
            "playlist": {"enabled": true, "presets": [{"path": "/a.milk"}], "intervalSeconds": 20}}]})");
        REQUIRE(result.ok);
        CHECK(result.performance.scenes[0].playlist.unit == IntervalUnit::Seconds);
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

TEST_CASE("format 1 file videos become pool videos, one per file")
{
    const auto result = ParsePerformance(R"({"version": 1, "scenes": [
        {"id": "a", "preset": {"path": "/x.milk"}, "midi": {"cc20Value": 0}, "video": {"enabled": true, "source": {"type": "file", "path": "/v/Hearts Intro.mp4"}}},
        {"id": "b", "preset": {"path": "/x.milk"}, "midi": {"cc20Value": 1}, "video": {"enabled": true, "source": {"type": "file", "path": "/v/Hearts Intro.mp4"}}},
        {"id": "c", "preset": {"path": "/x.milk"}, "midi": {"cc20Value": 2}, "video": {"enabled": false, "source": {"type": "file", "path": "/other/hearts intro.mp4"}}},
        {"id": "d", "preset": {"path": "/x.milk"}, "midi": {"cc20Value": 3}}]})");
    REQUIRE(result.ok);
    const auto& performance = result.performance;
    CHECK(performance.version == kCurrentFormatVersion);
    REQUIRE(performance.videos.size() == 2);
    CHECK(performance.videos[0].id == "hearts-intro");
    CHECK(performance.videos[0].path == "/v/Hearts Intro.mp4");
    CHECK(performance.videos[0].startSeconds == 0.0);
    CHECK(performance.videos[0].speed == 1.0);
    CHECK(performance.videos[1].id == "hearts-intro-2");
    CHECK(performance.scenes[0].video.source.videoId == "hearts-intro");
    CHECK(performance.scenes[1].video.source.videoId == "hearts-intro");
    CHECK(performance.scenes[2].video.source.videoId == "hearts-intro-2"); // kept for when it's switched on
    CHECK(performance.scenes[3].video.source.videoId.empty());
    CHECK(performance.scenes[0].video.source.path.empty());

    // Saved in the new format, without per-scene paths.
    const std::string saved = SerializePerformance(performance);
    CHECK(saved.find("\"version\": 2") != std::string::npos);
    CHECK(saved.find("\"video\": \"hearts-intro\"") != std::string::npos);
    const auto reloaded = ParsePerformance(saved);
    REQUIRE(reloaded.ok);
    CHECK(reloaded.performance.videos.size() == 2);
}

TEST_CASE("the video pool: adding dedupes by file, removing needs it unused")
{
    Performance performance;
    const std::string id = AddPoolVideo(performance, "/v/clip.mov");
    CHECK(id == "clip");
    CHECK(AddPoolVideo(performance, "/v/clip.mov") == "clip");
    CHECK(AddPoolVideo(performance, "/w/clip.mov") == "clip-2");
    REQUIRE(performance.videos.size() == 2);

    auto scene = MakeScene("a", 0);
    scene.video.source.videoId = id;
    performance.scenes = {scene};
    CHECK(VideoUsage(performance, id) == 1);
    CHECK(performance.VideoFor(performance.scenes[0]) == &performance.videos[0]);
    CHECK_FALSE(RemovePoolVideo(performance, id));
    CHECK(RemovePoolVideo(performance, "clip-2"));
    CHECK_FALSE(RemovePoolVideo(performance, "nope"));
    CHECK(performance.videos.size() == 1);
}

TEST_CASE("playlist entries have their own video look")
{
    SECTION("older files: every entry takes the scene's look")
    {
        const auto result = ParsePerformance(R"({"version": 2, "scenes": [{"id": "a", "preset": {"path": "/x.milk"}, "midi": {"cc20Value": 0},
            "playlist": {"enabled": true, "presets": [{"path": "/a.milk"}, {"path": "/b.milk"}]},
            "video": {"enabled": true, "source": {"type": "syphon"}, "mix": 0.4, "lumaMin": 0.2, "contrast": 50, "saturation": 0}}]})");
        REQUIRE(result.ok);
        const auto& scene = result.performance.scenes[0];
        REQUIRE(scene.playlist.looks.size() == 2);
        CHECK(scene.LookFor(1) == scene.video.look);
        CHECK(scene.LookFor(1).contrast == 50.f);
    }
    SECTION("round trip, and the scene's look is separate")
    {
        Performance performance;
        auto scene = MakeScene("a", 0);
        scene.playlist.enabled = true;
        scene.playlist.Add("/a.milk", VideoLook{0.5f, 0.1f, 1.f, 1.f});
        scene.playlist.Add("/b.milk", VideoLook{1.f, 0.f, 100.f, 0.f});
        scene.video.look.mix = 0.7f;
        performance.scenes = {scene};
        const auto loaded = ParsePerformance(SerializePerformance(performance));
        REQUIRE(loaded.ok);
        const auto& back = loaded.performance.scenes[0];
        CHECK(back.LookFor(0) == VideoLook{0.5f, 0.1f, 1.f, 1.f});
        CHECK(back.LookFor(1) == VideoLook{1.f, 0.f, 100.f, 0.f});
        CHECK(back.video.look.mix == 0.7f);
        CHECK(back.LookFor(-1).mix == 0.7f);

        // A disabled playlist keeps its entries' looks.
        performance.scenes[0].playlist.enabled = false;
        const auto disabled = ParsePerformance(SerializePerformance(performance));
        CHECK(disabled.performance.scenes[0].playlist.looks[1].contrast == 100.f);
        CHECK(disabled.performance.scenes[0].LookFor(1).mix == 0.7f); // not a playlist: the scene's look
    }
    SECTION("editing entries keeps the looks with their presets")
    {
        ScenePlaylist playlist;
        playlist.Add("/a.milk", VideoLook{0.1f});
        playlist.Add("/b.milk", VideoLook{0.2f});
        playlist.Add("/c.milk", VideoLook{0.3f});
        playlist.Swap(0, 2);
        CHECK(playlist.presets[0] == "/c.milk");
        CHECK(playlist.looks[0].mix == 0.3f);
        playlist.Remove(1);
        REQUIRE(playlist.looks.size() == 2);
        CHECK(playlist.looks[1].mix == 0.1f);
    }
    SECTION("entry looks are validated")
    {
        Performance performance;
        auto scene = MakeScene("a", 0);
        scene.playlist.enabled = true;
        scene.playlist.Add("/a.milk", VideoLook{2.f, 0.f, 1.f, 1.f});
        performance.scenes = {scene};
        CHECK(HasIssueAt(Validate(performance), "/scenes/0/playlist/presets/0/look/mix"));
    }
}
