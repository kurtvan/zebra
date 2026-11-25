#include "engine/RenderPlan.hpp"
#include "model/PerformanceJson.hpp"

#include "model/ClassicView.hpp"

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

/// What format 2 drew for playlist entry `item`, as branches, computed straight from a format 2 scene's JSON
/// the way the lanes did before graphs: a feedback blend (contrast, saturation, then a luma gate with the
/// patch's fixed 0.05 edge on each side, then mix) and, with the overlay on, the same look on top with the
/// overlay's own softness.
auto FormatTwoBranches(const nlohmann::json& scene, int item) -> std::vector<VideoBranch>
{
    const auto video = scene.value("video", nlohmann::json::object());
    if (!video.value("enabled", false))
        return {};
    nlohmann::json look = video;
    const auto playlist = scene.value("playlist", nlohmann::json::object());
    if (playlist.value("enabled", false) && !playlist.value("presets", nlohmann::json::array()).empty())
    {
        const auto& entry = playlist["presets"][static_cast<size_t>(item)];
        if (entry.contains("look"))
            look = entry["look"];
    }
    const bool invert = video.value("invertLuma", false);
    auto passes = [&](float softness) -> std::vector<EffectPass> {
        return {{NodeType::Contrast, look.value("contrast", 1.0f), 0.0f, false},
                {NodeType::Saturation, look.value("saturation", 1.0f), 0.0f, false},
                {NodeType::LumaGate, look.value("lumaMin", 0.0f), softness, invert},
                {NodeType::Opacity, look.value("mix", 1.0f), 0.0f, false}};
    };
    std::vector<VideoBranch> branches{{{{passes(0.1f)}}, PresetLayer::Feedback, {}}};
    if (look.value("overlay", false))
        branches.push_back({{{passes(look.value("softness", 0.0f))}}, PresetLayer::OnTop, {}});
    return branches;
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
        REQUIRE(gv::test::ClassicFromGraph(scene.graph));

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
            CHECK(plan.branches == FormatTwoBranches(source, item));
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
    const std::vector<EffectPass> onTop = {{NodeType::Contrast, 3.0f, 0, false},
                                           {NodeType::Saturation, 0.0f, 0, false},
                                           {NodeType::LumaGate, 0.2f, 0.4f, true},
                                           {NodeType::Opacity, 0.8f, 0, false}};
    CHECK(second.branches[1].steps == std::vector<VideoStep>{{onTop}});

    // The Video switcher's selection picks the source.
    std::string videoSwitcher;
    for (const auto& node : graph.nodes)
    {
        if (SwitchesVideos(graph, node))
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
    const auto preset = AddNode(graph, NodeType::MilkdropPreset, 0, 0);
    Connect(graph, preset, "viewport", 0);
    const auto mix = AddNode(graph, NodeType::Opacity, 0, 0);
    Connect(graph, mix, preset, 0);
    const auto plan = CompilePlan(graph);
    CHECK(plan.presetNode == preset);
    CHECK(plan.branches.empty());
}

TEST_CASE("every preset input compiles to a branch, texture inputs by name")
{
    Graph graph;
    AddNode(graph, NodeType::Viewport, 0, 0);
    const auto preset = AddNode(graph, NodeType::MilkdropPreset, 0, 0);
    REQUIRE(Connect(graph, preset, "viewport", 0));
    const auto source = AddNode(graph, NodeType::VideoSource, 0, 0);
    // Fan-out: one source into the feedback, before-composite and on-top inputs and two textures.
    for (int input : {static_cast<int>(PresetLayer::Feedback), static_cast<int>(PresetLayer::BeforeComposite), static_cast<int>(PresetLayer::OnTop)})
        REQUIRE(Connect(graph, source, preset, input));
    const auto contrast = AddNode(graph, NodeType::Contrast, 0, 0);
    REQUIRE(Connect(graph, source, contrast, 0));
    REQUIRE(Connect(graph, contrast, preset, static_cast<int>(PresetLayer::Texture), "worms"));
    REQUIRE(Connect(graph, source, preset, static_cast<int>(PresetLayer::Texture), "clouds"));
    CHECK(ValidateGraph(graph).empty());

    const auto plan = CompilePlan(graph);
    REQUIRE(plan.branches.size() == 5);
    CHECK(plan.branches[0].layer == PresetLayer::Feedback);
    CHECK(plan.branches[1].layer == PresetLayer::BeforeComposite);
    CHECK(plan.branches[2].layer == PresetLayer::OnTop);
    CHECK(plan.branches[3].layer == PresetLayer::Texture);
    CHECK(plan.branches[3].textureName == "clouds");
    CHECK(plan.branches[3].steps.empty());
    CHECK(plan.branches[4].textureName == "worms");
    REQUIRE(plan.branches[4].steps.size() == 1);
    REQUIRE(plan.branches[4].steps[0].effects.size() == 1);
    CHECK(plan.branches[4].steps[0].effects[0].type == NodeType::Contrast);
}

TEST_CASE("an ISF shader is a step of its own between fused effects; one whose file is missing is left out")
{
    const std::string invert = std::string(GV_TEST_FIXTURES_DIR) + "/isf/invert.fs";
    Graph graph;
    AddNode(graph, NodeType::Viewport, 0, 0);
    const auto preset = AddNode(graph, NodeType::MilkdropPreset, 0, 0);
    REQUIRE(Connect(graph, preset, "viewport", 0));
    // video -> contrast -> saturation -> ISF -> missing ISF -> opacity -> preset
    std::string from = AddNode(graph, NodeType::VideoSource, 0, 0);
    std::vector<std::string> chain;
    for (NodeType type : {NodeType::Contrast, NodeType::Saturation, NodeType::IsfShader, NodeType::IsfShader, NodeType::Opacity})
    {
        chain.push_back(AddNode(graph, type, 0, 0));
        REQUIRE(Connect(graph, from, chain.back(), 0));
        from = chain.back();
    }
    REQUIRE(Connect(graph, from, preset, static_cast<int>(PresetLayer::Feedback)));
    std::get<IsfShaderParams>(graph.Find(chain[2])->params) = {invert, {{"amount", {0.5f}}}};
    std::get<IsfShaderParams>(graph.Find(chain[3])->params).path = "/nowhere/missing.fs";
    FollowWires(graph);
    CHECK(ValidateGraph(graph).empty());

    const auto plan = CompilePlan(graph);
    REQUIRE(plan.branches.size() == 1);
    const auto& steps = plan.branches[0].steps;
    REQUIRE(steps.size() == 3);
    CHECK(steps[0].effects == std::vector<EffectPass>{{NodeType::Contrast, 1.f, 0.f, false}, {NodeType::Saturation, 1.f, 0.f, false}});
    CHECK_FALSE(steps[0].isf);
    REQUIRE(steps[1].isf);
    CHECK(steps[1].isf->node == chain[2]);
    CHECK(steps[1].isf->path == invert);
    REQUIRE(steps[1].isf->shader != nullptr);
    CHECK(steps[1].isf->values.at("amount") == std::vector<float>{0.5f});
    CHECK(steps[2].effects == std::vector<EffectPass>{{NodeType::Opacity, 1.f, 0.f, false}});
    CHECK(CompilePlan(graph) == plan); // the same parsed file each time, so an unchanged scene isn't replanned
}
