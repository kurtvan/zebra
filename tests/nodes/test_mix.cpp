// The Mix node: model/nodes/MixNode.cpp, app/nodes/MixNode.cpp. Its shader: tests/gl/test_effect_nodes.cpp.

#include "nodes/NodeTestKit.hpp"

#include "engine/RenderPlan.hpp"

using namespace gv;
using model::NodeType;

namespace {

auto Opacity(const model::Node& node) -> float
{
    return model::ParamsOf<model::MixParams>(node).opacity;
}

} // namespace

TEST_CASE("mix: an effect at full opacity by default", "[node:mix]")
{
    CHECK(model::Def(NodeType::Mix).effect);
    CHECK(std::string(model::Def(NodeType::Mix).jsonName) == "mix");
    test::NodeScene scene;
    const auto& node = scene.Node(scene.Add(NodeType::Mix));
    CHECK(node.id == "mix-1");
    CHECK(Opacity(node) == 1.0f);
}

TEST_CASE("mix: opacity 0 to 1", "[node:mix]")
{
    auto set = [](float value) { return [value](model::Node& node) { model::ParamsOf<model::MixParams>(node).opacity = value; }; };
    CHECK(test::ParamErrors(NodeType::Mix, set(0.f)).empty());
    CHECK(test::ParamErrors(NodeType::Mix, set(1.f)).empty());
    CHECK(test::ParamErrors(NodeType::Mix, set(1.2f)) == std::vector<std::string>{"Mix: opacity must be between 0 and 1."});
}

TEST_CASE("mix: saved as \"opacity\" and read back", "[node:mix]")
{
    test::NodeScene scene;
    auto& node = scene.Node(scene.Add(NodeType::Mix, [](model::Node& n) { model::ParamsOf<model::MixParams>(n).opacity = 0.35f; }));
    CHECK(test::RoundTrip(node) == node);
    CHECK(Opacity(test::ReadNode(NodeType::Mix, {{"opacity", 0.5}}).first) == 0.5f);
}

TEST_CASE("mix: compiles to a mix pass; through a Video switcher too", "[node:mix]")
{
    test::NodeScene scene;
    const std::string preset = scene.Add(NodeType::Preset);
    const std::string video = scene.Add(NodeType::VideoSource);
    const std::string switcher = scene.Add(NodeType::Switcher, [](model::Node& n) { model::ParamsOf<model::SwitcherParams>(n).kind = model::PortKind::Video; });
    const std::string mix = scene.Add(NodeType::Mix, [](model::Node& n) { model::ParamsOf<model::MixParams>(n).opacity = 0.5f; });
    model::Connect(scene.Graph(), preset, "viewport", 0);
    model::Connect(scene.Graph(), video, switcher, 0);
    model::Connect(scene.Graph(), switcher, mix, 0);
    model::Connect(scene.Graph(), mix, preset, 0);
    REQUIRE(model::ValidateGraph(scene.Graph()).empty());
    const auto plan = engine::CompilePlan(scene.Graph());
    REQUIRE(plan.branches.size() == 1);
    CHECK(plan.sourceNode == video);
    CHECK(plan.branches[0].passes == std::vector<model::EffectPass>{{NodeType::Mix, 0.5f, 0.f, false}});
}

TEST_CASE("mix: on the canvas, an opacity slider", "[node:mix]")
{
    const auto& slider = test::ControlById(NodeType::Mix, "##opacity");
    CHECK(slider.min == 0.0);
    CHECK(slider.max == 1.0);
    CHECK(std::string(slider.format) == "opacity %.2f");
    CHECK(std::string(slider.undo) == "Change Mix");

    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::Mix);
    test::FakeNodeContext context(scene);
    test::HeadlessUi ui;
    ui.Frame(context, id);
    const auto rect = context.itemRects.at("(slider)");
    ui.Click(context, id, ImVec2(rect.first.x + (rect.second.x - rect.first.x) * 0.25f, (rect.first.y + rect.second.y) * 0.5f));
    CHECK(Opacity(scene.Node(id)) < 0.5f);
    CHECK(context.LastEdit() == "Change Mix");
}
