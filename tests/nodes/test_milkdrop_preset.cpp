// The Milkdrop preset node ("preset" in older show files): model/nodes/MilkdropPresetNode.cpp, app/nodes/MilkdropPresetNode.cpp.

#include "nodes/NodeTestKit.hpp"

#include "engine/RenderPlan.hpp"
#include "model/PerformanceJson.hpp"

#include <fstream>
#include <unistd.h>

using namespace gv;
using model::NodeType;
using model::PresetLayer;

namespace {

auto Layer(PresetLayer layer) -> int
{
    return static_cast<int>(layer);
}

auto WithPath(std::string path)
{
    return [path](model::Node& n) { model::ParamsOf<model::MilkdropPresetParams>(n).path = path; };
}

auto PathOf(const model::Node& node) -> std::filesystem::path
{
    return model::ParamsOf<model::MilkdropPresetParams>(node).path;
}

} // namespace

TEST_CASE("milkdrop preset: video into its three layers and its textures, its picture out", "[node:milkdrop-preset]")
{
    const auto& def = model::Def(NodeType::MilkdropPreset);
    CHECK(def.textureInput == Layer(PresetLayer::Texture));
    CHECK(def.inputNames.size() == static_cast<size_t>(model::kPresetInputs));
    test::NodeScene scene;
    const auto& node = scene.Node(scene.Add(NodeType::MilkdropPreset));
    CHECK(node.id == "milkdrop-1");
    CHECK(PathOf(node).empty());
    CHECK(model::OutputKind(node) == model::PortKind::Frame);
    CHECK(model::InputCount(node) == model::kPresetInputs);
    for (int input = 0; input < model::kPresetInputs; ++input)
        CHECK(model::InputKind(node, input) == model::PortKind::Video);
}

TEST_CASE("milkdrop preset: texture inputs need a texture name, and only they have one", "[node:milkdrop-preset]")
{
    test::NodeScene scene;
    const std::string preset = scene.Add(NodeType::MilkdropPreset);
    const std::string video = scene.Add(NodeType::VideoSource);
    CHECK(model::ConnectErrors(scene.Graph(), video, preset, Layer(PresetLayer::Texture), "noise_lq").empty());
    CHECK(test::Messages(model::ConnectErrors(scene.Graph(), video, preset, Layer(PresetLayer::Texture))) ==
          std::vector<std::string>{"A texture input needs the name of the texture it replaces."});
    CHECK(test::Messages(model::ConnectErrors(scene.Graph(), video, preset, Layer(PresetLayer::OnTop), "noise_lq")) ==
          std::vector<std::string>{"Only texture inputs have a texture name."});
}

TEST_CASE("milkdrop preset: shows one video at a time", "[node:milkdrop-preset]")
{
    test::NodeScene scene;
    const std::string preset = scene.Add(NodeType::MilkdropPreset);
    const std::string a = scene.Add(NodeType::VideoSource);
    const std::string b = scene.Add(NodeType::VideoSource);
    const std::string contrast = scene.Add(NodeType::Contrast);
    model::Connect(scene.Graph(), a, preset, Layer(PresetLayer::Feedback));
    model::Connect(scene.Graph(), a, contrast, 0);
    // The same source through an effect chain: fine.
    CHECK(model::ConnectErrors(scene.Graph(), contrast, preset, Layer(PresetLayer::OnTop)).empty());
    // Another source: refused.
    CHECK(test::Messages(model::ConnectErrors(scene.Graph(), b, preset, Layer(PresetLayer::OnTop))) ==
          std::vector<std::string>{"A preset can show only one video at a time, but its inputs come from 2 different sources."});
}

TEST_CASE("milkdrop preset: saved with its path; wires into it by layer name", "[node:milkdrop-preset]")
{
    test::NodeScene scene;
    auto& node = scene.Node(scene.Add(NodeType::MilkdropPreset, WithPath("/presets/a b.milk")));
    CHECK(test::RoundTrip(node) == node);

    const std::string preset = node.id;
    scene.performance.videos.push_back({"clip", "", "/v/clip.mov"});
    const std::string video = scene.Add(NodeType::VideoSource, [](model::Node& n) { model::ParamsOf<model::VideoSourceParams>(n).source.videoId = "clip"; });
    model::Connect(scene.Graph(), preset, "viewport", 0);
    model::Connect(scene.Graph(), video, preset, Layer(PresetLayer::Feedback));
    model::Connect(scene.Graph(), video, preset, Layer(PresetLayer::BeforeComposite));
    model::Connect(scene.Graph(), video, preset, Layer(PresetLayer::OnTop));
    model::Connect(scene.Graph(), video, preset, Layer(PresetLayer::Texture), "noise_lq");
    scene.performance.scenes[0].cc20Value = 1;
    const std::string text = model::SerializePerformance(scene.performance);
    for (const char* name : {"\"feedback\"", "\"beforeComposite\"", "\"onTop\"", "\"texture\""})
        CHECK(text.find(name) != std::string::npos);
    const auto loaded = model::ParsePerformance(text);
    REQUIRE(loaded.ok);
    CHECK(loaded.performance.scenes[0].graph == scene.Graph());
}

TEST_CASE("milkdrop preset: a show file needs its file chosen", "[node:milkdrop-preset]")
{
    test::NodeScene scene;
    std::vector<model::ShowProblem> problems;
    model::Def(NodeType::MilkdropPreset).checkInShow(scene.performance, scene.Node(scene.Add(NodeType::MilkdropPreset)), problems);
    CHECK(problems == std::vector<model::ShowProblem>{{false, "no preset file chosen"}});
    problems.clear();
    model::Def(NodeType::MilkdropPreset).checkInShow(scene.performance, scene.Node(scene.Add(NodeType::MilkdropPreset, WithPath("/p.milk"))), problems);
    CHECK(problems.empty());
}

TEST_CASE("milkdrop preset: the plan has one branch per wired input, in input order", "[node:milkdrop-preset]")
{
    test::NodeScene scene;
    const std::string preset = scene.Add(NodeType::MilkdropPreset, WithPath("/p.milk"));
    const std::string video = scene.Add(NodeType::VideoSource);
    model::Connect(scene.Graph(), preset, "viewport", 0);
    model::Connect(scene.Graph(), video, preset, Layer(PresetLayer::Texture), "b");
    model::Connect(scene.Graph(), video, preset, Layer(PresetLayer::OnTop));
    model::Connect(scene.Graph(), video, preset, Layer(PresetLayer::Texture), "a");
    const auto plan = engine::CompilePlan(scene.Graph());
    CHECK(plan.presetNode == preset);
    CHECK(plan.presetPath == "/p.milk");
    REQUIRE(plan.branches.size() == 3);
    CHECK(plan.branches[0].layer == PresetLayer::OnTop);
    CHECK(plan.branches[1].textureName == "a");
    CHECK(plan.branches[2].textureName == "b");
}

TEST_CASE("milkdrop preset: titled by its file", "[node:milkdrop-preset]")
{
    test::NodeScene scene;
    test::FakeNodeContext context(scene);
    CHECK(app::NodeTitle(scene.Node(scene.Add(NodeType::MilkdropPreset)), context) == "Milkdrop preset (none chosen)");
    CHECK(app::NodeTitle(scene.Node(scene.Add(NodeType::MilkdropPreset, WithPath("/x/Martin - Liquid.milk"))), context) == "Martin - Liquid");
}

TEST_CASE("milkdrop preset: its dot says chosen, found, ready, or on screen", "[node:milkdrop-preset]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::MilkdropPreset, WithPath("/p.milk"));
    test::FakeNodeContext context(scene);
    auto status = [&] { return app::NodeStatusOf(scene.Node(id), context); };
    CHECK(app::NodeStatusOf(scene.Node(scene.Add(NodeType::MilkdropPreset)), context).state == app::NodeState::Empty);
    CHECK(status() == app::NodeStatus{app::NodeState::Missing, "File not found: /p.milk"});
    context.files = {"/p.milk"};
    CHECK(status().state == app::NodeState::Idle);
    context.ready = true;
    CHECK(status().state == app::NodeState::Ready);
    context.livePreset = id;
    CHECK(status().state == app::NodeState::Live);
}

TEST_CASE("milkdrop preset: an input row per layer and per texture, keeping wired textures it no longer has", "[node:milkdrop-preset]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::MilkdropPreset, WithPath("/p.milk"));
    const std::string video = scene.Add(NodeType::VideoSource);
    model::Connect(scene.Graph(), video, id, Layer(PresetLayer::Texture), "gone");
    test::FakeNodeContext context(scene);
    context.textures["/p.milk"] = {"noise_lq", "clouds"};
    const auto inputs = app::NodeInputs(scene.Node(id), context);
    std::vector<std::string> labels;
    for (const auto& input : inputs)
        labels.push_back(input.label);
    CHECK(labels == std::vector<std::string>{"Feedback", "Before composite", "On top", "Texture: noise_lq", "Texture: clouds", "Texture: gone"});
    CHECK(inputs[3] == app::InputSpec{Layer(PresetLayer::Texture), "noise_lq", model::PortKind::Video, "Texture: noise_lq"});

    test::HeadlessUi ui;
    ui.Frame(context, id);
    CHECK(context.inputRows == inputs);
}

TEST_CASE("milkdrop preset: Choose... changes the file in one step; cancelling changes nothing", "[node:milkdrop-preset]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::MilkdropPreset, WithPath("/p.milk"));
    test::FakeNodeContext context(scene);
    test::HeadlessUi ui;
    ui.Frame(context, id);
    const ImVec2 choose = test::HeadlessUi::Centre(context, "Or drop a preset from the Presets panel on this node.");

    ui.Click(context, id, choose);
    CHECK(context.fileDialogs == std::vector<std::string>{"Choose a preset"});
    CHECK(context.edits.empty());

    context.chosenFile = std::filesystem::path("/q.milk");
    ui.Click(context, id, choose);
    CHECK(PathOf(scene.Node(id)) == "/q.milk");
    CHECK(context.LastEdit() == "Change Milkdrop Preset");
}

TEST_CASE("milkdrop preset: Reveal shows the file, but not when it's missing", "[node:milkdrop-preset]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::MilkdropPreset, WithPath("/p.milk"));
    test::FakeNodeContext context(scene);
    test::HeadlessUi ui;
    ui.Frame(context, id);
    ui.Click(context, id, test::HeadlessUi::Centre(context, "Show the file in Finder."));
    CHECK(context.revealed.empty());
    context.files = {"/p.milk"};
    ui.Frame(context, id);
    ui.Click(context, id, test::HeadlessUi::Centre(context, "Show the file in Finder."));
    CHECK(context.revealed == std::vector<std::filesystem::path>{"/p.milk"});
}

TEST_CASE("milkdrop preset: the star shows for library presets and toggles the favourite", "[node:milkdrop-preset]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::MilkdropPreset, WithPath("/p.milk"));
    test::FakeNodeContext context(scene);
    context.files = {"/p.milk"};
    test::HeadlessUi ui;
    ui.Frame(context, id);
    CHECK_FALSE(context.itemRects.contains("Favourite"));
    context.favourites["/p.milk"] = false;
    ui.Frame(context, id);
    ui.Click(context, id, test::HeadlessUi::Centre(context, "Favourite"));
    CHECK(context.toggledFavourites == std::vector<std::string>{"/p.milk"});
}

TEST_CASE("milkdrop preset: asks for its thumbnail with where it's drawn, so a tiny one can be paused", "[node:milkdrop-preset]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::MilkdropPreset, WithPath("/p.milk"));
    test::FakeNodeContext context(scene);
    test::HeadlessUi ui;
    ui.Frame(context, id);
    REQUIRE(context.thumbnailRects.size() == 1);
    const auto [min, max] = context.thumbnailRects[0];
    CHECK(max.x - min.x == context.width);
    CHECK(max.y - min.y == context.width * 9.f / 16.f);
    const auto [itemMin, itemMax] = context.itemRects.at("/p.milk"); // the picture's own rectangle
    CHECK(min.x == itemMin.x);
    CHECK(min.y == itemMin.y);
    CHECK(max.x == itemMax.x);
    CHECK(max.y == itemMax.y);
}

TEST_CASE("milkdrop preset: the frame time shows only on the live preset, and the missing note only when missing", "[node:milkdrop-preset]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::MilkdropPreset, WithPath("/p.milk"));
    test::FakeNodeContext context(scene);
    const auto texts = test::ControlsOf(NodeType::MilkdropPreset, app::Control::Kind::Text);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0]->warning);
    CHECK(texts[0]->text(scene.Node(id), context) == "File not found.");
    context.files = {"/p.milk"};
    CHECK(texts[0]->text(scene.Node(id), context).empty());

    context.frameTime = std::pair<double, bool>{21.5, true};
    context.livePreset = id;
    test::HeadlessUi ui;
    ui.Frame(context, id); // draws "21.5 ms per frame: too heavy" without asserting
    CHECK(context.edits.empty());
}

TEST_CASE("milkdrop preset: \"Milkdrop preset...\" in the Add menu asks for a file", "[node:milkdrop-preset]")
{
    const auto& items = app::Ui(NodeType::MilkdropPreset).addItems;
    REQUIRE(items.size() == 1);
    CHECK(std::string(items[0].label) == "Milkdrop preset...");
    CHECK(items[0].chooseFile);
}

TEST_CASE("milkdrop preset: older show files call it \"preset\"; they open, and save it as \"milkdropPreset\"", "[node:milkdrop-preset]")
{
    const auto loaded = model::ParsePerformance(R"({"version": 3, "scenes": [{"id": "a", "midi": {"cc20Value": 0}, "graph": {
        "nodes": [{"id": "p", "type": "preset", "path": "/x.milk"}, {"id": "v", "type": "viewport"}, {"id": "m", "type": "mix", "opacity": 0.4}],
        "wires": [{"from": "p", "to": "v", "input": 0}, {"from": "m", "to": "p", "input": "feedback"}]}}]})");
    REQUIRE(loaded.ok);
    const auto& graph = loaded.performance.scenes[0].graph;
    REQUIRE(graph.Find("p") != nullptr);
    CHECK(graph.Find("p")->type == NodeType::MilkdropPreset);
    CHECK(PathOf(*graph.Find("p")) == "/x.milk");
    const std::string saved = model::SerializePerformance(loaded.performance);
    CHECK(saved.find("\"milkdropPreset\"") != std::string::npos);
    CHECK(saved.find("\"type\": \"preset\"") == std::string::npos);
}

namespace {

/// A library in a temp folder: A/1, A/2 (broken), A/3 and B/4.
auto StepLibrary() -> library::Library
{
    // One folder per process: ctest runs these tests in parallel, and each one rebuilds and deletes it.
    const auto dir = std::filesystem::temp_directory_path() / ("gv_test_node_stepping_" + std::to_string(getpid()));
    std::filesystem::remove_all(dir);
    for (const char* file : {"A/1.milk", "A/2.milk", "A/3.milk", "B/4.milk"})
    {
        std::filesystem::create_directories((dir / file).parent_path());
        std::ofstream(dir / file) << "[preset00]\n";
    }
    auto presets = library::Library::Scan(dir, library::Library());
    presets.Record(dir / "A/2.milk", 0.0, "Could not parse");
    std::filesystem::remove_all(dir);
    return presets;
}

} // namespace

TEST_CASE("milkdrop preset: ◀ ▶ step through its folder, one undo step each", "[node:milkdrop-preset]")
{
    const auto presets = StepLibrary();
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::MilkdropPreset, WithPath(presets.PathForId("A/1.milk").string()));
    test::FakeNodeContext context(scene);
    context.presetLibrary = &presets;
    test::HeadlessUi ui;
    ui.Frame(context, id);
    const ImVec2 next = test::HeadlessUi::Centre(context, "The next preset in this folder.");
    const ImVec2 previous = test::HeadlessUi::Centre(context, "The previous preset in this folder.");

    ui.Click(context, id, next);
    REQUIRE(context.edits.size() == 1);
    CHECK(context.LastEdit() == "Next preset");
    CHECK(PathOf(scene.Node(id)) == presets.PathForId("A/3.milk")); // A/2 is broken
    ui.Click(context, id, next);
    CHECK(PathOf(scene.Node(id)) == presets.PathForId("A/1.milk")); // wraps, staying in A
    ui.Click(context, id, previous);
    CHECK(context.LastEdit() == "Previous preset");
    CHECK(PathOf(scene.Node(id)) == presets.PathForId("A/3.milk"));
    CHECK(context.edits.size() == 3);
}

TEST_CASE("milkdrop preset: Random picks another working library preset, one undo step each", "[node:milkdrop-preset]")
{
    const auto presets = StepLibrary();
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::MilkdropPreset, WithPath(presets.PathForId("A/1.milk").string()));
    test::FakeNodeContext context(scene);
    test::HeadlessUi ui;
    ui.Frame(context, id);
    const ImVec2 random = test::HeadlessUi::Centre(context, "A random preset from the whole library.");
    const ImVec2 next = test::HeadlessUi::Centre(context, "The next preset in this folder.");
    ui.Click(context, id, random); // no library yet: disabled
    ui.Click(context, id, next);
    CHECK(context.edits.empty());

    context.presetLibrary = &presets;
    for (int i = 0; i < 5; ++i)
    {
        const auto before = PathOf(scene.Node(id));
        ui.Click(context, id, random);
        CHECK(context.LastEdit() == "Random preset");
        CHECK(PathOf(scene.Node(id)) != before);
        CHECK(PathOf(scene.Node(id)) != presets.PathForId("A/2.milk"));
    }
    CHECK(context.edits.size() == 5);
}

TEST_CASE("milkdrop preset: a preset outside the library can't step through a folder, but can go random", "[node:milkdrop-preset]")
{
    const auto presets = StepLibrary();
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::MilkdropPreset, WithPath("/elsewhere/x.milk"));
    test::FakeNodeContext context(scene);
    context.presetLibrary = &presets;
    test::HeadlessUi ui;
    ui.Frame(context, id);
    ui.Click(context, id, test::HeadlessUi::Centre(context, "The next preset in this folder."));
    ui.Click(context, id, test::HeadlessUi::Centre(context, "The previous preset in this folder."));
    CHECK(context.edits.empty());
    ui.Click(context, id, test::HeadlessUi::Centre(context, "A random preset from the whole library."));
    CHECK(context.LastEdit() == "Random preset");
}
