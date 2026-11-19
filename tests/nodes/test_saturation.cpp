// The Saturation node: model/nodes/SaturationNode.cpp, app/nodes/SaturationNode.cpp. Its shader: tests/gl/test_effect_nodes.cpp.

#include "nodes/NodeTestKit.hpp"

#include "engine/RenderPlan.hpp"

using namespace gv;
using model::NodeType;

namespace {

auto Saturation(const model::Node& node) -> float
{
    return model::ParamsOf<model::SaturationParams>(node).saturation;
}

auto SetSaturation(float value)
{
    return [value](model::Node& node) { model::ParamsOf<model::SaturationParams>(node).saturation = value; };
}

} // namespace

TEST_CASE("saturation: an effect, video in and out, leaving the video as it is by default", "[node:saturation]")
{
    const auto& def = model::Def(NodeType::Saturation);
    CHECK(def.effect);
    CHECK(std::string(def.jsonName) == "saturation");
    test::NodeScene scene;
    const auto& node = scene.Node(scene.Add(NodeType::Saturation));
    CHECK(node.id == "saturation-1");
    CHECK(Saturation(node) == 1.0f);
    CHECK(model::OutputKind(node) == model::PortKind::Video);
    CHECK(model::InputCount(node) == 1);
}

TEST_CASE("saturation: 0 (black and white) to 2", "[node:saturation]")
{
    CHECK(test::ParamErrors(NodeType::Saturation, SetSaturation(0.f)).empty());
    CHECK(test::ParamErrors(NodeType::Saturation, SetSaturation(2.f)).empty());
    CHECK(test::ParamErrors(NodeType::Saturation, SetSaturation(2.01f)) == std::vector<std::string>{"Saturation: saturation must be between 0 and 2."});
    CHECK(test::ParamErrors(NodeType::Saturation, SetSaturation(-1.f)).size() == 1);
}

TEST_CASE("saturation: saved as \"saturation\" and read back", "[node:saturation]")
{
    test::NodeScene scene;
    auto& node = scene.Node(scene.Add(NodeType::Saturation, SetSaturation(0.25f)));
    CHECK(test::RoundTrip(node) == node);
    auto [read, issues] = test::ReadNode(NodeType::Saturation, {{"saturation", 1.5}});
    CHECK(Saturation(read) == 1.5f);
    CHECK(issues.empty());
    CHECK(test::ReadNode(NodeType::Saturation, {{"saturation", "full"}}).second.size() == 1);
}

TEST_CASE("saturation: compiles to a saturation pass, in chain order", "[node:saturation]")
{
    test::NodeScene scene;
    const std::string preset = scene.Add(NodeType::Preset);
    const std::string video = scene.Add(NodeType::VideoSource);
    const std::string first = scene.Add(NodeType::Saturation, SetSaturation(0.f));
    const std::string second = scene.Add(NodeType::Saturation, SetSaturation(2.f));
    model::Connect(scene.Graph(), preset, "viewport", 0);
    model::Connect(scene.Graph(), video, first, 0);
    model::Connect(scene.Graph(), first, second, 0);
    model::Connect(scene.Graph(), second, preset, 0);
    const auto plan = engine::CompilePlan(scene.Graph());
    REQUIRE(plan.branches.size() == 1);
    CHECK(plan.branches[0].passes ==
          std::vector<model::EffectPass>{{NodeType::Saturation, 0.f, 0.f, false}, {NodeType::Saturation, 2.f, 0.f, false}});
}

TEST_CASE("saturation: on the canvas, a video input and a 0..2 slider", "[node:saturation]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::Saturation);
    test::FakeNodeContext context(scene);
    CHECK(app::NodeTitle(scene.Node(id), context) == "Saturation");
    CHECK(app::NodeInputs(scene.Node(id), context) == std::vector<app::InputSpec>{{0, {}, model::PortKind::Video, "Video"}});
    const auto& slider = test::ControlById(NodeType::Saturation, "##saturation");
    CHECK(slider.min == 0.0);
    CHECK(slider.max == 2.0);
    CHECK_FALSE(slider.logarithmic);
    CHECK(std::string(slider.undo) == "Change Saturation");
    CHECK(std::string(slider.field) == "saturation");
}

TEST_CASE("saturation: clicking the slider's left end turns the colour down", "[node:saturation]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::Saturation);
    test::FakeNodeContext context(scene);
    test::HeadlessUi ui;
    ui.Frame(context, id);
    const auto rect = context.itemRects.at("(slider)");
    ui.Click(context, id, ImVec2(rect.first.x + 3.f, (rect.first.y + rect.second.y) * 0.5f));
    CHECK(Saturation(scene.Node(id)) < 0.1f);
    REQUIRE_FALSE(context.edits.empty());
    CHECK(context.LastEdit() == "Change Saturation");
    CHECK(context.endedEdits == 1);
}
