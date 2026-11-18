#include "engine/RenderPlan.hpp"
#include "model/PerformanceJson.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

using namespace gv::engine;
using namespace gv::model;

namespace {

auto Fixture(const char* name) -> std::string
{
    std::ifstream in(std::string(GV_TEST_FIXTURES_DIR) + "/" + name);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

/// Today's (format 2) lane settings for playlist entry `item`, computed straight from a format 2 scene's JSON,
/// the way Engine::VideoSettingsFor did before graphs.
auto FormatTwoSettings(const nlohmann::json& scene, int item) -> VideoSettings
{
    const auto video = scene.value("video", nlohmann::json::object());
    nlohmann::json look = video;
    const auto playlist = scene.value("playlist", nlohmann::json::object());
    if (playlist.value("enabled", false) && !playlist.value("presets", nlohmann::json::array()).empty())
    {
        const auto& entry = playlist["presets"][static_cast<size_t>(item)];
        if (entry.contains("look"))
            look = entry["look"];
    }
    VideoSettings settings;
    settings.enabled = video.value("enabled", false);
    if (!settings.enabled)
        return settings;
    settings.mix = look.value("mix", 1.0f);
    settings.lumaMin = look.value("lumaMin", 0.0f);
    settings.contrast = look.value("contrast", 1.0f);
    settings.saturation = look.value("saturation", 1.0f);
    settings.overlay = look.value("overlay", false);
    settings.softness = settings.overlay ? look.value("softness", 0.0f) : 0.0f; // only drawn with the overlay
    settings.lumaInvert = video.value("invertLuma", false);
    return settings;
}

} // namespace

TEST_CASE("the real show converts to valid graphs that draw exactly what format 2 drew")
{
    const std::string text = Fixture("govtech-nov-6th.json");
    const auto json = nlohmann::json::parse(text);
    REQUIRE(json["version"] == 2);
    const auto result = ParsePerformance(text);
    REQUIRE(result.ok);
    const auto& scenes = result.performance.scenes;
    REQUIRE(scenes.size() == json["scenes"].size());

    int checked = 0;
    for (size_t i = 0; i < scenes.size(); ++i)
    {
        INFO("scene " << scenes[i].id);
        const auto& scene = scenes[i];
        const auto& source = json["scenes"][i];
        CHECK(ValidateGraph(scene.graph).empty());
        REQUIRE(scene.classicShape);

        // Presets, entry by entry.
        const auto playlist = source.value("playlist", nlohmann::json::object());
        const bool isPlaylist = playlist.value("enabled", false) && !playlist.value("presets", nlohmann::json::array()).empty();
        std::vector<std::string> presets;
        if (isPlaylist)
        {
            for (const auto& entry : playlist["presets"])
                presets.push_back(entry["path"].get<std::string>());
        }
        else
        {
            presets.push_back(source["preset"]["path"].get<std::string>());
        }
        REQUIRE(scene.Presets().size() == presets.size());

        for (int item = 0; item < static_cast<int>(presets.size()); ++item)
        {
            INFO("entry " << item);
            const auto plan = CompilePlan(scene.graph, SelectionForItem(scene.graph, item));
            CHECK(plan.presetPath == presets[static_cast<size_t>(item)]);
            CHECK(VideoSettingsFromPlan(plan) == FormatTwoSettings(source, item));
            checked++;
        }

        // The video the scene starts with.
        const auto video = source.value("video", nlohmann::json::object());
        if (video.value("enabled", false))
        {
            const auto plan = CompilePlan(scene.graph);
            const std::string type = video["source"].value("type", "file");
            CHECK(plan.source.type == (type == "syphon" ? VideoSourceType::Syphon : VideoSourceType::File));
            const auto videoPlaylist = video.value("playlist", nlohmann::json::object());
            if (type == "file" && videoPlaylist.value("enabled", false) && !videoPlaylist.value("videos", nlohmann::json::array()).empty())
                CHECK(plan.source.videoId == videoPlaylist["videos"][0].get<std::string>());
            else if (type == "file")
                CHECK(plan.source.videoId == video["source"]["video"].get<std::string>());
        }
    }
    CHECK(checked >= 30); // every entry of every playlist
}

TEST_CASE("plans follow switcher selections and carry each branch's effects in order")
{
    ClassicScene classic;
    classic.playlist = {true, {"/a.milk", "/b.milk"}, {VideoLook{0.5f}, VideoLook{0.8f, 0.2f, 3.0f, 0.0f, true, 0.4f}}, IntervalUnit::Seconds, 10, 4, false};
    classic.video.enabled = true;
    classic.video.invertLuma = true;
    classic.video.playlist = {true, {"x", "y"}, IntervalUnit::Seconds, 10, 4, false};
    const Graph graph = GraphFromClassic(classic);

    const auto first = CompilePlan(graph);
    CHECK(first.presetPath == "/a.milk");
    CHECK(first.source.videoId == "x");
    REQUIRE(first.branches.size() == 1);

    const auto second = CompilePlan(graph, SelectionForItem(graph, 1));
    CHECK(second.presetPath == "/b.milk");
    REQUIRE(second.branches.size() == 2);
    CHECK(second.branches[0].layer == PresetLayer::Feedback);
    CHECK(second.branches[1].layer == PresetLayer::OnTop);
    const std::vector<EffectPass> onTop = {{EffectPass::Type::Contrast, 3.0f, 0, false},
                                           {EffectPass::Type::Saturation, 0.0f, 0, false},
                                           {EffectPass::Type::LumaGate, 0.2f, 0.4f, true},
                                           {EffectPass::Type::Mix, 0.8f, 0, false}};
    CHECK(second.branches[1].passes == onTop);

    // The Video switcher's selection picks the source.
    std::string videoSwitcher;
    for (const auto& node : graph.nodes)
    {
        if (node.type == NodeType::Switcher && ParamsOf<SwitcherParams>(node).kind == PortKind::Video)
            videoSwitcher = node.id;
    }
    auto selection = SelectionForItem(graph, 1);
    selection[videoSwitcher] = 1;
    CHECK(CompilePlan(graph, selection).source.videoId == "y");

    // A selection on an unwired input falls back to the first wired one.
    selection[videoSwitcher] = 5;
    CHECK(CompilePlan(graph, selection).source.videoId == "x");
}

TEST_CASE("unfinished graphs compile to nothing")
{
    Graph graph;
    AddNode(graph, NodeType::Viewport, 0, 0);
    CHECK(CompilePlan(graph) == RenderPlan{});

    // A chain without a source carries no video.
    const auto preset = AddNode(graph, NodeType::Preset, 0, 0);
    Connect(graph, preset, "viewport", 0);
    const auto mix = AddNode(graph, NodeType::Mix, 0, 0);
    Connect(graph, mix, preset, 0);
    const auto plan = CompilePlan(graph);
    CHECK(plan.presetNode == preset);
    CHECK(plan.branches.empty());
    CHECK_FALSE(VideoSettingsFromPlan(plan).enabled);
}
