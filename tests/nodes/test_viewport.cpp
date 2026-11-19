// The Viewport node: model/nodes/ViewportNode.cpp, app/nodes/ViewportNode.cpp.

#include "nodes/NodeTestKit.hpp"

#include "engine/RenderPlan.hpp"

using namespace gv;
using model::NodeType;

TEST_CASE("viewport: one picture input, no output, always called \"viewport\"", "[node:viewport]")
{
    test::NodeScene scene; // has one
    const model::Node* viewport = scene.Graph().Viewport();
    REQUIRE(viewport != nullptr);
    CHECK(viewport->id == "viewport");
    CHECK_FALSE(model::OutputKind(*viewport));
    CHECK(model::InputCount(*viewport) == 1);
    CHECK(model::InputKind(*viewport, 0) == model::PortKind::Frame);
    CHECK_FALSE(model::InputKind(*viewport, 1));
}

TEST_CASE("viewport: exactly one per graph, and it can't be removed", "[node:viewport]")
{
    test::NodeScene scene;
    CHECK(model::ValidateGraph(scene.Graph()).empty());
    CHECK_FALSE(model::RemoveNode(scene.Graph(), "viewport"));

    model::AddNode(scene.Graph(), NodeType::Viewport, 0.f, 0.f);
    CHECK(test::Messages(model::ValidateGraph(scene.Graph())) == std::vector<std::string>{"There can only be one viewport."});

    model::Graph empty;
    CHECK(test::Messages(model::ValidateGraph(empty)) == std::vector<std::string>{"There's no viewport."});
}

TEST_CASE("viewport: takes a preset's picture, not video", "[node:viewport]")
{
    test::NodeScene scene;
    const std::string preset = scene.Add(NodeType::Preset);
    const std::string video = scene.Add(NodeType::VideoSource);
    CHECK(model::ConnectErrors(scene.Graph(), preset, "viewport", 0).empty());
    CHECK(test::Messages(model::ConnectErrors(scene.Graph(), video, "viewport", 0)) ==
          std::vector<std::string>{"This input of the Viewport takes a preset's picture, not video."});
}

TEST_CASE("viewport: what's wired into it is what the scene draws; nothing wired draws black", "[node:viewport]")
{
    test::NodeScene scene;
    CHECK(engine::CompilePlan(scene.Graph()).presetNode.empty());
    const std::string preset = scene.Add(NodeType::Preset, [](model::Node& n) { model::ParamsOf<model::PresetParams>(n).path = "/a.milk"; });
    model::Connect(scene.Graph(), preset, "viewport", 0);
    const auto plan = engine::CompilePlan(scene.Graph());
    CHECK(plan.presetNode == preset);
    CHECK(plan.presetPath == "/a.milk");
}

TEST_CASE("viewport: saved with no settings of its own", "[node:viewport]")
{
    test::NodeScene scene;
    CHECK(test::RoundTrip(*scene.Graph().Viewport()) == *scene.Graph().Viewport());
}

TEST_CASE("viewport: on the canvas, a Picture input and the output size", "[node:viewport]")
{
    test::NodeScene scene;
    test::FakeNodeContext context(scene);
    const model::Node& viewport = scene.Node("viewport");
    CHECK(app::NodeTitle(viewport, context) == "Viewport");
    CHECK(app::NodeInputs(viewport, context) == std::vector<app::InputSpec>{{0, {}, model::PortKind::Frame, "Picture"}});

    const auto texts = test::ControlsOf(NodeType::Viewport, app::Control::Kind::Text);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0]->text(viewport, context) == "Output: the screen's size");
    scene.performance.canvas = {false, 1280, 720};
    CHECK(texts[0]->text(viewport, context) == "Output: 1280 x 720");

    test::HeadlessUi ui;
    ui.Frame(context, "viewport");
    CHECK(context.inputRows.size() == 1);
    CHECK(context.edits.empty());
}

TEST_CASE("viewport: offered in the Add menu only when the graph has none", "[node:viewport]")
{
    const auto& items = app::Ui(NodeType::Viewport).addItems;
    REQUIRE(items.size() == 1);
    test::NodeScene scene;
    CHECK_FALSE(items[0].available(scene.Graph()));
    CHECK(items[0].available(model::Graph{}));
}
