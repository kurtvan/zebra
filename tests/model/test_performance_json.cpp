#include "model/PerformanceJson.hpp"

#include "model/Document.hpp"
#include "TestScenes.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace gv::model;
using gv::test::MakeScene;
using gv::test::WithVideo;

namespace {

auto HasIssueAt(const LoadResult& result, const std::string& path, Issue::Severity severity = Issue::Severity::Error) -> bool
{
    for (const auto& issue : result.issues)
    {
        if (issue.path == path && issue.severity == severity)
            return true;
    }
    return false;
}

auto CountIssuesAt(const std::vector<Issue>& issues, const std::string& path) -> int
{
    int count = 0;
    for (const auto& issue : issues)
        count += issue.path == path ? 1 : 0;
    return count;
}

auto HasIssueAt(const std::vector<Issue>& issues, const std::string& path) -> bool
{
    return CountIssuesAt(issues, path) > 0;
}

/// The first node of `type` in a scene's graph.
auto NodeOf(Scene& scene, NodeType type) -> Node&
{
    for (auto& node : scene.graph.nodes)
    {
        if (node.type == type)
            return node;
    }
    throw std::runtime_error("no such node");
}

} // namespace

TEST_CASE("a performance survives a save/load round trip")
{
    Performance performance;
    performance.name = "Show";
    performance.videos = {PoolVideo{"intro", "Intro loop", "/videos/intro.mp4", 12.5, 0.5, VideoFit::Fit}};
    auto classic = WithVideo("/presets/intro.milk", "intro", VideoLook{0.5f, 0.25f, 80.0f, 0.0f, true, 0.25f}); // high contrast is allowed
    classic.video.restartOnEnter = false;
    classic.video.loop = false;
    classic.video.invertLuma = true;
    auto a = MakeScene("intro", 0, classic);
    a.name = "Intro";
    a.beatHit = {true, BeatHitEvery::Bar, BeatHitSound::Snare, 0.8f};
    ClassicScene syphon;
    syphon.presetPath = "/presets/cave.milk";
    syphon.video.enabled = true;
    syphon.video.source = {VideoSourceType::Syphon, {}, {}, "Resolume", "Composition"};
    auto b = MakeScene("cave", 7, syphon);
    a.graph.nodes[0].x = 12.5f;
    a.graph.nodes[0].expanded = false;
    performance.scenes = {a, b};

    const auto result = ParsePerformance(SerializePerformance(performance));
    REQUIRE(result.ok);
    REQUIRE(result.issues.empty());
    const auto& loaded = result.performance;
    CHECK(loaded.version == 3);
    CHECK(loaded.name == "Show");
    REQUIRE(loaded.scenes.size() == 2);
    CHECK(loaded.scenes[0].graph == a.graph); // node positions and expanded state included
    CHECK(loaded.scenes[1].graph == b.graph);
    CHECK(loaded.scenes[0].id == "intro");
    CHECK(loaded.scenes[0].name == "Intro");
    CHECK(loaded.scenes[0].classic == NormalizeClassic(classic));
    CHECK(loaded.scenes[0].FirstPreset() == "/presets/intro.milk");
    REQUIRE(loaded.videos.size() == 1);
    CHECK(loaded.videos[0].id == "intro");
    CHECK(loaded.videos[0].name == "Intro loop");
    CHECK(loaded.videos[0].path == "/videos/intro.mp4");
    CHECK(loaded.videos[0].startSeconds == 12.5);
    CHECK(loaded.videos[0].speed == 0.5);
    CHECK(loaded.videos[0].fit == VideoFit::Fit);
    CHECK(loaded.scenes[0].beatHit == BeatHit{true, BeatHitEvery::Bar, BeatHitSound::Snare, 0.8f});
    CHECK(loaded.scenes[1].beatHit == BeatHit{}); // off by default
    CHECK(loaded.scenes[1].cc20Value == 7);
    CHECK(loaded.scenes[1].classic.video.source.type == VideoSourceType::Syphon);
    CHECK(loaded.scenes[1].classic.video.source.appName == "Resolume");
    CHECK(loaded.scenes[1].classic.video.source.serverName == "Composition");
    CHECK(loaded.scenes[1].classic.video.loop);
}

TEST_CASE("format 3 files name preset inputs and keep switcher inputs stable")
{
    Performance performance;
    ClassicScene classic;
    classic.playlist.enabled = true;
    classic.playlist.presets = {"/a.milk", "/b.milk"};
    classic.playlist.looks = {VideoLook{}, VideoLook{0.5f, 0.0f, 1.0f, 1.0f, true}};
    classic.video.enabled = true;
    classic.video.source.type = VideoSourceType::Syphon;
    performance.scenes = {MakeScene("s", 0, classic)};
    const std::string text = SerializePerformance(performance);
    CHECK(text.find("\"input\": \"feedback\"") != std::string::npos);
    CHECK(text.find("\"input\": \"onTop\"") != std::string::npos);
    CHECK(text.find("\"kind\": \"frame\"") != std::string::npos);
    CHECK(text.find("\"type\": \"lumaGate\"") != std::string::npos);
    CHECK(text.find("\"preset\"") != std::string::npos);
    CHECK(text.find("\"playlist\"") == std::string::npos); // no format 2 fields
    CHECK(SerializePerformance(ParsePerformance(text).performance) == text); // stable
}

TEST_CASE("damaged graphs load unfinished, with a warning per problem")
{
    const auto result = ParsePerformance(R"({"version": 3, "scenes": [{"id": "a", "midi": {"cc20Value": 0}, "graph": {
        "nodes": [{"id": "p", "type": "preset", "path": "/x.milk"}, {"id": "v", "type": "viewport"}, {"id": "c", "type": "contrast"},
                  {"id": "w", "type": "wormhole"}],
        "wires": [{"from": "p", "to": "v", "input": 0}, {"from": "p", "to": "c", "input": 0}, {"from": "c", "to": "c", "input": 0},
                  {"from": "c", "to": "p", "input": "sideways"}]}}]})");
    REQUIRE(result.ok); // only warnings
    const auto& scene = result.performance.scenes[0];
    CHECK(scene.graph.nodes.size() == 3);           // the unknown node is left out
    CHECK(scene.graph.wires.empty());               // nothing wired: unfinished
    CHECK(ValidateGraph(scene.graph).empty());
    CHECK(HasIssueAt(result, "/scenes/0/graph/nodes/3/type", Issue::Severity::Warning));
    CHECK(HasIssueAt(result, "/scenes/0/graph/wires/3/input", Issue::Severity::Warning));
    CHECK(CountIssuesAt(result.issues, "/scenes/0/graph") >= 3); // each problem, then the note that wires were removed
    CHECK(scene.FirstPreset().empty());

    // Bad values are still errors, as in format 2.
    const auto bad = ParsePerformance(R"({"version": 3, "scenes": [{"id": "a", "midi": {"cc20Value": 0}, "graph": {
        "nodes": [{"id": "p", "type": "preset", "path": "/x.milk"}, {"id": "v", "type": "viewport"}, {"id": "m", "type": "mix", "opacity": 3}],
        "wires": [{"from": "p", "to": "v", "input": 0}]}}]})");
    CHECK_FALSE(bad.ok);
    CHECK(HasIssueAt(bad, "/scenes/0/graph"));
}

TEST_CASE("unparseable or structurally wrong files are rejected with a message")
{
    CHECK_FALSE(ParsePerformance("{not json").ok);
    CHECK_FALSE(ParsePerformance("[]").ok);
    CHECK(HasIssueAt(ParsePerformance(R"({"scenes": []})"), "/version"));
    CHECK(HasIssueAt(ParsePerformance(R"({"version": 99, "scenes": []})"), "/version"));
    CHECK(HasIssueAt(ParsePerformance(R"({"version": 1})"), "/scenes"));
    CHECK(HasIssueAt(ParsePerformance(R"({"version": 1, "scenes": [{"id": 3}]})"), "/scenes/0/id"));
    CHECK(HasIssueAt(ParsePerformance(R"({"version": 2, "scenes": [], "videos": [{"id": "a", "path": "/a.mov", "fit": "zoom"}]})"), "/videos/0/fit"));
}

TEST_CASE("an empty performance is valid")
{
    const auto result = ParsePerformance(R"({"version": 1, "scenes": []})");
    CHECK(result.ok);
    CHECK(result.performance.canvas.matchScreen);
}

TEST_CASE("the canvas round-trips, and Match screen isn't written")
{
    Performance performance;
    CHECK(SerializePerformance(performance).find("canvas") == std::string::npos);
    CHECK(ParsePerformance(SerializePerformance(performance)).performance.canvas == Canvas{});

    performance.canvas = {false, 3840, 1080};
    const auto text = SerializePerformance(performance);
    CHECK(text.find("\"canvas\"") != std::string::npos);
    const auto result = ParsePerformance(text);
    REQUIRE(result.ok);
    CHECK(result.performance.canvas == Canvas{false, 3840, 1080});

    // Files without a canvas (every older one) match the screen.
    CHECK(ParsePerformance(R"({"version": 2, "scenes": []})").performance.canvas.matchScreen);
}

TEST_CASE("canvas sizes must be even and within range")
{
    auto check = [](int width, int height) {
        Performance performance;
        performance.canvas = {false, width, height};
        return Validate(performance);
    };
    CHECK(check(1920, 1080).empty());
    CHECK(check(kMinCanvasSize, kMaxCanvasSize).empty());
    CHECK(CountIssuesAt(check(318, 1080), "/canvas/width") == 1);
    CHECK(CountIssuesAt(check(7682, 1080), "/canvas/width") == 1);
    CHECK(CountIssuesAt(check(1921, 1080), "/canvas/width") == 1);
    const auto both = check(0, 1081);
    CHECK(CountIssuesAt(both, "/canvas/width") == 1);
    CHECK(CountIssuesAt(both, "/canvas/height") == 1);

    // Match screen ignores the stored size.
    Performance performance;
    performance.canvas = {true, 1, 1};
    CHECK(Validate(performance).empty());

    CHECK(HasIssueAt(ParsePerformance(R"({"version": 3, "canvas": {"width": 1921, "height": 1080}, "scenes": []})"), "/canvas/width"));
    CHECK(HasIssueAt(ParsePerformance(R"({"version": 3, "canvas": {"width": "wide", "height": 1080}, "scenes": []})"), "/canvas/width"));
    CHECK(HasIssueAt(ParsePerformance(R"({"version": 3, "canvas": 1080, "scenes": []})"), "/canvas"));
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
        ParamsOf<PresetParams>(NodeOf(scene, NodeType::Preset)).path.clear();
        performance.scenes = {scene};
        const auto issues = Validate(performance);
        CHECK(HasIssueAt(issues, "/scenes/0/id"));
        CHECK(HasIssueAt(issues, "/scenes/0/graph"));
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
        performance.videos = {PoolVideo{"v", "", "/v.mp4"}};
        auto scene = MakeScene("a", 0, WithVideo("/a.milk", "v"));
        ParamsOf<MixParams>(NodeOf(scene, NodeType::Mix)).opacity = 1.5f;
        ParamsOf<LumaGateParams>(NodeOf(scene, NodeType::LumaGate)).threshold = -0.1f;
        ParamsOf<LumaGateParams>(NodeOf(scene, NodeType::LumaGate)).softness = 2.f;
        ParamsOf<ContrastParams>(NodeOf(scene, NodeType::Contrast)).contrast = 150.f;
        ParamsOf<SaturationParams>(NodeOf(scene, NodeType::Saturation)).saturation = -1.f;
        scene.beatHit.strength = 1.5f;
        performance.scenes = {scene};
        const auto issues = Validate(performance);
        CHECK(CountIssuesAt(issues, "/scenes/0/graph") == 5);
        CHECK(HasIssueAt(issues, "/scenes/0/beatHit/strength"));
    }

    SECTION("files without contrast/saturation load with defaults")
    {
        const auto result = ParsePerformance(R"({"version": 1, "scenes": [{"id": "a", "preset": {"path": "/x.milk"}, "midi": {"cc20Value": 0},
            "video": {"enabled": true, "source": {"type": "file", "path": "/v.mp4"}, "mix": 1, "lumaMin": 0}}]})");
        REQUIRE(result.ok);
        const auto& look = result.performance.scenes[0].classic.video.look;
        CHECK(look.contrast == 1.0f);
        CHECK(look.saturation == 1.0f);
        CHECK_FALSE(look.overlay);
        CHECK(look.softness == 0.0f);
    }
    SECTION("enabled file video needs a pool video; Syphon without names means any server")
    {
        auto file = MakeScene("a", 0, WithVideo("/a.milk", ""));
        ClassicScene syphonScene;
        syphonScene.presetPath = "/b.milk";
        syphonScene.video.enabled = true;
        syphonScene.video.source.type = VideoSourceType::Syphon;
        performance.scenes = {file, MakeScene("b", 1, syphonScene)};
        const auto issues = Validate(performance);
        REQUIRE(CountIssuesAt(issues, "/scenes/0/graph") == 1);
        CHECK(issues[0].severity == Issue::Severity::Warning); // none chosen yet: unfinished
        CHECK_FALSE(HasIssueAt(issues, "/scenes/1/graph"));
        performance.scenes[0] = MakeScene("a", 0, WithVideo("/a.milk", "gone"));
        const auto gone = Validate(performance);
        REQUIRE(CountIssuesAt(gone, "/scenes/0/graph") == 1);
        CHECK(gone[0].severity == Issue::Severity::Error); // a video the pool doesn't have: broken
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

TEST_CASE("nodes with nothing chosen yet save and open again, with a warning each")
{
    // As the canvas's Add menu leaves them: a preset with no file and a file video with no video.
    Performance performance;
    performance.scenes = {MakeScene("a", 0)};
    Graph& graph = performance.scenes[0].graph;
    const std::string preset = AddNode(graph, NodeType::Preset, 0.f, 0.f);
    const std::string video = AddNode(graph, NodeType::VideoSource, 0.f, 200.f);
    REQUIRE(Validate(performance).size() == 2);

    const auto loaded = ParsePerformance(SerializePerformance(performance));
    CHECK(loaded.ok); // so saving is allowed, and a crash recovery file opens
    CHECK(HasIssueAt(loaded, "/scenes/0/graph", Issue::Severity::Warning));
    CHECK_FALSE(HasIssueAt(loaded, "/scenes/0/graph", Issue::Severity::Error));
    REQUIRE(loaded.performance.scenes.size() == 1);
    CHECK(loaded.performance.scenes[0].graph == graph);
    CHECK(loaded.performance.scenes[0].graph.Find(preset) != nullptr);
    CHECK(loaded.performance.scenes[0].graph.Find(video) != nullptr);
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
    ClassicScene present;
    present.presetPath = preset;
    const std::string video = AddPoolVideo(performance, dir / "nope.mp4");
    performance.scenes = {MakeScene("present", 0, present), MakeScene("missing", 1, WithVideo((dir / "nope.milk").string(), video))};

    const auto file = dir / "performance.json";
    REQUIRE(SavePerformance(performance, file).empty());

    const auto result = LoadPerformance(file);
    CHECK(result.ok);
    CHECK_FALSE(result.performance.scenes[0].presetMissing);
    CHECK(result.performance.scenes[1].presetMissing);
    CHECK(result.performance.scenes[1].videoMissing);
    CHECK(HasIssueAt(result, "/scenes/1/graph", Issue::Severity::Warning));
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
    ClassicScene classic;
    classic.playlist.enabled = true;
    classic.playlist.presets = {dir / "a.milk", dir / "missing.milk", dir / "b.milk"};
    classic.playlist.intervalSeconds = 12.5;
    classic.playlist.shuffle = true;
    performance.scenes = {MakeScene("p", 0, classic), MakeScene("plain", 1)};
    CHECK(Validate(performance).empty());

    const auto file = dir / "performance.json";
    REQUIRE(SavePerformance(performance, file).empty());
    const auto result = LoadPerformance(file);
    REQUIRE(result.ok);
    const auto& loaded = result.performance.scenes[0];
    CHECK(loaded.IsPlaylist());
    CHECK(loaded.classic.playlist.presets.size() == 3);
    CHECK(loaded.classic.playlist.intervalSeconds == 12.5);
    CHECK(loaded.classic.playlist.shuffle);
    CHECK(loaded.FirstPreset() == dir / "a.milk");
    CHECK_FALSE(loaded.presetMissing);
    CHECK(loaded.playlistMissing == std::vector<bool>{false, true, false});
    CHECK(HasIssueAt(result, "/scenes/0/graph", Issue::Severity::Warning));
    CHECK_FALSE(result.performance.scenes[1].IsPlaylist());
    CHECK(result.performance.scenes[1].Presets().size() == 1);

    SECTION("intervals are validated")
    {
        auto bad = performance;
        ParamsOf<SwitcherParams>(NodeOf(bad.scenes[0], NodeType::Switcher)).intervalSeconds = 0.2;
        CHECK(HasIssueAt(Validate(bad), "/scenes/0/graph"));
    }
    SECTION("beat and bar intervals round-trip and validate")
    {
        auto bars = performance;
        auto& params = ParamsOf<SwitcherParams>(NodeOf(bars.scenes[0], NodeType::Switcher));
        params.unit = IntervalUnit::Bars;
        params.intervalCount = 8;
        params.intervalSeconds = 0.0; // ignored for bars
        CHECK(Validate(bars).empty());
        const auto reloaded = ParsePerformance(SerializePerformance(bars));
        REQUIRE(reloaded.ok);
        CHECK(reloaded.performance.scenes[0].classic.playlist.unit == IntervalUnit::Bars);
        CHECK(reloaded.performance.scenes[0].classic.playlist.intervalCount == 8);

        params.intervalCount = 0;
        CHECK(HasIssueAt(Validate(bars), "/scenes/0/graph"));
        CHECK(HasIssueAt(ParsePerformance(R"({"version": 1, "scenes": [{"id": "a", "midi": {"cc20Value": 0},
            "playlist": {"enabled": true, "presets": [{"path": "/a.milk"}], "intervalUnit": "fortnights"}}]})"), "/scenes/0/playlist/intervalUnit"));
    }
    SECTION("older playlists without a unit are in seconds")
    {
        const auto older = ParsePerformance(R"({"version": 1, "scenes": [{"id": "a", "midi": {"cc20Value": 0},
            "playlist": {"enabled": true, "presets": [{"path": "/a.milk"}], "intervalSeconds": 20}}]})");
        REQUIRE(older.ok);
        CHECK(older.performance.scenes[0].classic.playlist.unit == IntervalUnit::Seconds);
    }
    SECTION("a format 2 playlist that's turned off loads as the single preset")
    {
        const auto off = ParsePerformance(R"({"version": 2, "scenes": [{"id": "a", "midi": {"cc20Value": 0}, "preset": {"path": "/b.milk"},
            "playlist": {"enabled": false, "presets": [{"path": "/a.milk"}]}}]})");
        REQUIRE(off.ok);
        CHECK_FALSE(off.performance.scenes[0].IsPlaylist());
        CHECK(off.performance.scenes[0].FirstPreset() == "/b.milk");
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
    CHECK(performance.videos[0].fit == VideoFit::Fill); // no "fit" in the file
    CHECK(performance.videos[1].id == "hearts-intro-2");
    CHECK(performance.scenes[0].classic.video.source.videoId == "hearts-intro");
    CHECK(performance.scenes[1].classic.video.source.videoId == "hearts-intro");
    CHECK_FALSE(performance.scenes[2].classic.video.enabled); // switched-off video isn't converted
    CHECK(performance.scenes[3].classic.video.source.videoId.empty());
    CHECK(performance.scenes[0].classic.video.source.path.empty());

    // Saved in the new format, without per-scene paths.
    const std::string saved = SerializePerformance(performance);
    CHECK(saved.find("\"version\": 3") != std::string::npos);
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

    performance.scenes = {MakeScene("a", 0, WithVideo("/a.milk", id))};
    CHECK(VideoUsage(performance, id) == 1);
    CHECK(performance.VideoFor(performance.scenes[0]) == &performance.videos[0]);
    CHECK_FALSE(RemovePoolVideo(performance, id));
    CHECK(RemovePoolVideo(performance, "clip-2"));
    CHECK_FALSE(RemovePoolVideo(performance, "nope"));
    CHECK(performance.videos.size() == 1);
}

TEST_CASE("video playlists round-trip, validate and count as using their videos")
{
    Performance performance;
    AddPoolVideo(performance, "/v/a.mov");
    AddPoolVideo(performance, "/v/b.mov");
    auto classic = WithVideo("/p.milk", "a");
    classic.video.playlist = {true, {"b", "a"}, IntervalUnit::Bars, 30.0, 8, true};
    performance.scenes = {MakeScene("s", 0, classic)};
    CHECK(performance.scenes[0].IsVideoPlaylist());
    CHECK(performance.VideoFor(performance.scenes[0])->id == "b"); // starts on the playlist's first video
    CHECK(VideoUsage(performance, "b") == 1);
    CHECK_FALSE(RemovePoolVideo(performance, "b"));

    const auto result = ParsePerformance(SerializePerformance(performance));
    REQUIRE(result.ok);
    CHECK(result.issues.empty());
    const auto& playlist = result.performance.scenes[0].classic.video.playlist;
    CHECK(playlist.enabled);
    CHECK(playlist.videos == std::vector<std::string>{"b", "a"});
    CHECK(playlist.unit == IntervalUnit::Bars);
    CHECK(playlist.intervalCount == 8);
    CHECK(playlist.shuffle);

    // A turned-off playlist: the scene shows its single video.
    classic.video.playlist.enabled = false;
    performance.scenes = {MakeScene("s", 0, classic)};
    CHECK_FALSE(performance.scenes[0].IsVideoPlaylist());
    CHECK(performance.VideoFor(performance.scenes[0])->id == "a");

    // Validation: unknown ids, interval range.
    classic.video.playlist = {true, {"a", "nope"}, IntervalUnit::Bars, 30.0, 8, false};
    performance.scenes = {MakeScene("s", 0, classic)};
    CHECK(HasIssueAt(Validate(performance), "/scenes/0/graph"));
}

TEST_CASE("a Syphon scene's video playlist is left out, with a warning")
{
    const auto result = ParsePerformance(R"({"version": 2, "scenes": [{"id": "a", "preset": {"path": "/x.milk"}, "midi": {"cc20Value": 0},
        "video": {"enabled": true, "source": {"type": "syphon"}, "playlist": {"enabled": true, "videos": ["v"]}}}],
        "videos": [{"id": "v", "path": "/v.mp4"}]})");
    REQUIRE(result.ok);
    CHECK(HasIssueAt(result, "/scenes/0/video/playlist", Issue::Severity::Warning));
    CHECK_FALSE(result.performance.scenes[0].IsVideoPlaylist());
    CHECK(result.performance.scenes[0].classic.video.source.type == VideoSourceType::Syphon);
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
        REQUIRE(scene.classic.playlist.looks.size() == 2);
        CHECK(scene.LookFor(1) == scene.LookFor(0));
        CHECK(scene.LookFor(1).contrast == 50.f);
    }
    SECTION("round trip")
    {
        Performance performance;
        ClassicScene classic;
        classic.playlist.enabled = true;
        classic.playlist.presets = {"/a.milk", "/b.milk"};
        classic.playlist.looks = {VideoLook{0.5f, 0.1f, 1.f, 1.f}, VideoLook{1.f, 0.f, 100.f, 0.f, true, 0.5f}};
        classic.video.enabled = true;
        classic.video.source.type = VideoSourceType::Syphon;
        performance.scenes = {MakeScene("a", 0, classic)};
        const auto loaded = ParsePerformance(SerializePerformance(performance));
        REQUIRE(loaded.ok);
        const auto& back = loaded.performance.scenes[0];
        CHECK(back.LookFor(0) == VideoLook{0.5f, 0.1f, 1.f, 1.f});
        CHECK(back.LookFor(1) == VideoLook{1.f, 0.f, 100.f, 0.f, true, 0.5f});
    }
}

TEST_CASE("saving over an older format keeps one backup of it")
{
    const auto dir = std::filesystem::temp_directory_path() / "gv_test_backup";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto file = dir / "show.json";
    const std::string old = R"({"version": 2, "scenes": []})";
    std::ofstream(file) << old;

    const auto backup = BackupOlderFormat(file);
    CHECK(backup == dir / "show.v2-backup.json");
    std::stringstream copied;
    copied << std::ifstream(backup).rdbuf();
    CHECK(copied.str() == old);
    CHECK(BackupOlderFormat(file).empty()); // only once

    REQUIRE(SavePerformance(Performance{}, file).empty());
    CHECK(BackupOlderFormat(file).empty()); // already the current format
    CHECK(BackupOlderFormat(dir / "missing.json").empty());
    std::filesystem::remove_all(dir);
}
