// The Opacity node ("mix" in older show files): model/nodes/OpacityNode.cpp, app/nodes/OpacityNode.cpp. Its shader: tests/gl/test_effect_nodes.cpp.

#include "nodes/NodeTestKit.hpp"

#include "engine/RenderPlan.hpp"

using namespace gv;
using model::NodeType;

namespace {

auto Opacity(const model::Node& node) -> float
{
    return model::ParamsOf<model::OpacityParams>(node).opacity;
}

} // namespace

TEST_CASE("opacity: an effect at full opacity by default", "[node:opacity]")
{
    CHECK(model::Def(NodeType::Opacity).effect);
    CHECK(std::string(model::Def(NodeType::Opacity).jsonName) == "opacity");
    test::NodeScene scene;
    const auto& node = scene.Node(scene.Add(NodeType::Opacity));
    CHECK(node.id == "opacity-1");
    CHECK(Opacity(node) == 1.0f);
}

TEST_CASE("opacity: opacity 0 to 1", "[node:opacity]")
{
    auto set = [](float value) { return [value](model::Node& node) { model::ParamsOf<model::OpacityParams>(node).opacity = value; }; };
    CHECK(test::ParamErrors(NodeType::Opacity, set(0.f)).empty());
    CHECK(test::ParamErrors(NodeType::Opacity, set(1.f)).empty());
    CHECK(test::ParamErrors(NodeType::Opacity, set(1.2f)) == std::vector<std::string>{"Opacity: opacity must be between 0 and 1."});
}

TEST_CASE("opacity: saved as \"opacity\" and read back", "[node:opacity]")
{
    test::NodeScene scene;
    auto& node = scene.Node(scene.Add(NodeType::Opacity, [](model::Node& n) { model::ParamsOf<model::OpacityParams>(n).opacity = 0.35f; }));
    CHECK(test::RoundTrip(node) == node);
    CHECK(Opacity(test::ReadNode(NodeType::Opacity, {{"opacity", 0.5}}).first) == 0.5f);
}

TEST_CASE("opacity: compiles to an opacity pass; through a Video switcher too", "[node:opacity]")
{
    test::NodeScene scene;
    const std::string preset = scene.Add(NodeType::MilkdropPreset);
    const std::string video = scene.Add(NodeType::VideoSource);
    const std::string switcher = scene.Add(NodeType::Switcher);
    const std::string opacity = scene.Add(NodeType::Opacity, [](model::Node& n) { model::ParamsOf<model::OpacityParams>(n).opacity = 0.5f; });
    model::Connect(scene.Graph(), preset, "viewport", 0);
    model::Connect(scene.Graph(), video, switcher, 0);
    model::Connect(scene.Graph(), switcher, opacity, 0);
    model::Connect(scene.Graph(), opacity, preset, 0);
    REQUIRE(model::ValidateGraph(scene.Graph()).empty());
    const auto plan = engine::CompilePlan(scene.Graph());
    REQUIRE(plan.branches.size() == 1);
    CHECK(plan.sourceNode == video);
    CHECK(plan.branches[0].steps.at(0).effects == std::vector<model::EffectPass>{{NodeType::Opacity, 0.5f, 0.f, false}});
}

TEST_CASE("opacity: on the canvas, an opacity slider", "[node:opacity]")
{
    const auto& slider = test::ControlById(NodeType::Opacity, "##opacity");
    CHECK(slider.min == 0.0);
    CHECK(slider.max == 1.0);
    CHECK(std::string(slider.format) == "opacity %.2f");
    CHECK(std::string(slider.undo) == "Change Opacity");

    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::Opacity);
    test::FakeNodeContext context(scene);
    test::HeadlessUi ui;
    ui.Frame(context, id);
    const auto rect = context.itemRects.at("(slider)");
    ui.Click(context, id, ImVec2(rect.first.x + (rect.second.x - rect.first.x) * 0.25f, (rect.first.y + rect.second.y) * 0.5f));
    CHECK(Opacity(scene.Node(id)) < 0.5f);
    CHECK(context.LastEdit() == "Change Opacity");
}

TEST_CASE("opacity: older show files call it \"mix\"; they open, and save it as \"opacity\"", "[node:opacity]")
{
    const auto loaded = model::ParsePerformance(R"({"version": 3, "scenes": [{"id": "a", "midi": {"cc20Value": 0}, "graph": {
        "nodes": [{"id": "p", "type": "preset", "path": "/x.milk"}, {"id": "v", "type": "viewport"}, {"id": "m", "type": "mix", "opacity": 0.4}],
        "wires": [{"from": "p", "to": "v", "input": 0}, {"from": "m", "to": "p", "input": "feedback"}]}}]})");
    REQUIRE(loaded.ok);
    const auto& graph = loaded.performance.scenes[0].graph;
    REQUIRE(graph.Find("m") != nullptr);
    CHECK(graph.Find("m")->type == NodeType::Opacity);
    CHECK(Opacity(*graph.Find("m")) == 0.4f);
    const std::string saved = model::SerializePerformance(loaded.performance);
    CHECK(saved.find("\"opacity\"") != std::string::npos);
    CHECK(saved.find("\"type\": \"mix\"") == std::string::npos);
}
