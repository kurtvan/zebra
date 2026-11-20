// The Preset node: model/nodes/PresetNode.cpp, app/nodes/PresetNode.cpp.

#include "nodes/NodeTestKit.hpp"

#include "engine/RenderPlan.hpp"
#include "model/PerformanceJson.hpp"

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
    return [path](model::Node& n) { model::ParamsOf<model::PresetParams>(n).path = path; };
}

auto PathOf(const model::Node& node) -> std::filesystem::path
{
    return model::ParamsOf<model::PresetParams>(node).path;
}

} // namespace

TEST_CASE("preset: video into its three layers and its textures, its picture out", "[node:preset]")
{
    const auto& def = model::Def(NodeType::Preset);
    CHECK(def.textureInput == Layer(PresetLayer::Texture));
    CHECK(def.inputNames.size() == static_cast<size_t>(model::kPresetInputs));
    test::NodeScene scene;
    const auto& node = scene.Node(scene.Add(NodeType::Preset));
    CHECK(node.id == "preset-1");
    CHECK(PathOf(node).empty());
    CHECK(model::OutputKind(node) == model::PortKind::Frame);
    CHECK(model::InputCount(node) == model::kPresetInputs);
    for (int input = 0; input < model::kPresetInputs; ++input)
        CHECK(model::InputKind(node, input) == model::PortKind::Video);
}

TEST_CASE("preset: texture inputs need a texture name, and only they have one", "[node:preset]")
{
    test::NodeScene scene;
    const std::string preset = scene.Add(NodeType::Preset);
    const std::string video = scene.Add(NodeType::VideoSource);
    CHECK(model::ConnectErrors(scene.Graph(), video, preset, Layer(PresetLayer::Texture), "noise_lq").empty());
    CHECK(test::Messages(model::ConnectErrors(scene.Graph(), video, preset, Layer(PresetLayer::Texture))) ==
          std::vector<std::string>{"A texture input needs the name of the texture it replaces."});
    CHECK(test::Messages(model::ConnectErrors(scene.Graph(), video, preset, Layer(PresetLayer::OnTop), "noise_lq")) ==
          std::vector<std::string>{"Only texture inputs have a texture name."});
}

TEST_CASE("preset: shows one video at a time", "[node:preset]")
{
    test::NodeScene scene;
    const std::string preset = scene.Add(NodeType::Preset);
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

TEST_CASE("preset: saved with its path; wires into it by layer name", "[node:preset]")
{
    test::NodeScene scene;
    auto& node = scene.Node(scene.Add(NodeType::Preset, WithPath("/presets/a b.milk")));
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

TEST_CASE("preset: a show file needs its file chosen", "[node:preset]")
{
    test::NodeScene scene;
    std::vector<model::ShowProblem> problems;
    model::Def(NodeType::Preset).checkInShow(scene.performance, scene.Node(scene.Add(NodeType::Preset)), problems);
    CHECK(problems == std::vector<model::ShowProblem>{{false, "no preset file chosen"}});
    problems.clear();
    model::Def(NodeType::Preset).checkInShow(scene.performance, scene.Node(scene.Add(NodeType::Preset, WithPath("/p.milk"))), problems);
    CHECK(problems.empty());
}

TEST_CASE("preset: the plan has one branch per wired input, in input order", "[node:preset]")
{
    test::NodeScene scene;
    const std::string preset = scene.Add(NodeType::Preset, WithPath("/p.milk"));
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

TEST_CASE("preset: titled by its file", "[node:preset]")
{
    test::NodeScene scene;
    test::FakeNodeContext context(scene);
    CHECK(app::NodeTitle(scene.Node(scene.Add(NodeType::Preset)), context) == "Preset (none chosen)");
    CHECK(app::NodeTitle(scene.Node(scene.Add(NodeType::Preset, WithPath("/x/Martin - Liquid.milk"))), context) == "Martin - Liquid");
}

TEST_CASE("preset: its dot says chosen, found, ready, or on screen", "[node:preset]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::Preset, WithPath("/p.milk"));
    test::FakeNodeContext context(scene);
    auto status = [&] { return app::NodeStatusOf(scene.Node(id), context); };
    CHECK(app::NodeStatusOf(scene.Node(scene.Add(NodeType::Preset)), context).state == app::NodeState::Empty);
    CHECK(status() == app::NodeStatus{app::NodeState::Missing, "File not found: /p.milk"});
    context.files = {"/p.milk"};
    CHECK(status().state == app::NodeState::Idle);
    context.ready = true;
    CHECK(status().state == app::NodeState::Ready);
    context.livePreset = id;
    CHECK(status().state == app::NodeState::Live);
}

TEST_CASE("preset: an input row per layer and per texture, keeping wired textures it no longer has", "[node:preset]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::Preset, WithPath("/p.milk"));
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

TEST_CASE("preset: Choose... changes the file in one step; cancelling changes nothing", "[node:preset]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::Preset, WithPath("/p.milk"));
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
    CHECK(context.LastEdit() == "Change Preset");
}

TEST_CASE("preset: Reveal shows the file, but not when it's missing", "[node:preset]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::Preset, WithPath("/p.milk"));
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

TEST_CASE("preset: the star shows for library presets and toggles the favourite", "[node:preset]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::Preset, WithPath("/p.milk"));
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

TEST_CASE("preset: the frame time shows only on the live preset, and the missing note only when missing", "[node:preset]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::Preset, WithPath("/p.milk"));
    test::FakeNodeContext context(scene);
    const auto texts = test::ControlsOf(NodeType::Preset, app::Control::Kind::Text);
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

TEST_CASE("preset: \"Preset...\" in the Add menu asks for a file", "[node:preset]")
{
    const auto& items = app::Ui(NodeType::Preset).addItems;
    REQUIRE(items.size() == 1);
    CHECK(std::string(items[0].label) == "Preset...");
    CHECK(items[0].chooseFile);
}
