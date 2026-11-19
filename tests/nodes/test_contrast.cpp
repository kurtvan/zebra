// The Contrast node: model/nodes/ContrastNode.cpp, app/nodes/ContrastNode.cpp. Its shader: tests/gl/test_effect_nodes.cpp.

#include "nodes/NodeTestKit.hpp"

#include "engine/RenderPlan.hpp"

#include <catch2/matchers/catch_matchers_vector.hpp>

#include <cmath>
#include <limits>

using namespace gv;
using model::NodeType;
using Catch::Matchers::Equals;

namespace {

auto Contrast(const model::Node& node) -> float
{
    return model::ParamsOf<model::ContrastParams>(node).contrast;
}

} // namespace

TEST_CASE("contrast: an effect, video in and out, leaving the video as it is by default", "[node:contrast]")
{
    const auto& def = model::Def(NodeType::Contrast);
    CHECK(def.effect);
    CHECK(std::string(def.jsonName) == "contrast");
    CHECK(std::string(def.displayName) == "Contrast");

    test::NodeScene scene;
    const auto& node = scene.Node(scene.Add(NodeType::Contrast));
    CHECK(node.id == "contrast-1");
    CHECK(Contrast(node) == 1.0f);
    CHECK(model::OutputKind(node) == model::PortKind::Video);
    CHECK(model::InputCount(node) == 1);
    CHECK(model::InputKind(node, 0) == model::PortKind::Video);
}

TEST_CASE("contrast: 0 to 100", "[node:contrast]")
{
    auto set = [](float value) { return [value](model::Node& node) { model::ParamsOf<model::ContrastParams>(node).contrast = value; }; };
    CHECK(test::ParamErrors(NodeType::Contrast, set(0.f)).empty());
    CHECK(test::ParamErrors(NodeType::Contrast, set(model::kMaxContrast)).empty());
    CHECK_THAT(test::ParamErrors(NodeType::Contrast, set(-0.01f)), Equals(std::vector<std::string>{"Contrast: contrast must be between 0 and 100."}));
    CHECK(test::ParamErrors(NodeType::Contrast, set(100.5f)).size() == 1);
    CHECK(test::ParamErrors(NodeType::Contrast, set(std::numeric_limits<float>::quiet_NaN())).size() == 1);
}

TEST_CASE("contrast: saved as \"contrast\" and read back", "[node:contrast]")
{
    test::NodeScene scene;
    auto& node = scene.Node(scene.Add(NodeType::Contrast));
    model::ParamsOf<model::ContrastParams>(node).contrast = 3.5f;
    CHECK(test::RoundTrip(node) == node);

    auto [missing, none] = test::ReadNode(NodeType::Contrast, nlohmann::json::object());
    CHECK(Contrast(missing) == 1.0f);
    CHECK(none.empty());
    auto [wrong, issues] = test::ReadNode(NodeType::Contrast, {{"contrast", "high"}});
    CHECK(Contrast(wrong) == 1.0f);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "/node/contrast");
}

TEST_CASE("contrast: compiles to a contrast pass with its value", "[node:contrast]")
{
    test::NodeScene scene;
    const std::string preset = scene.Add(NodeType::Preset, [](model::Node& n) { model::ParamsOf<model::PresetParams>(n).path = "/p.milk"; });
    const std::string video = scene.Add(NodeType::VideoSource);
    const std::string contrast = scene.Add(NodeType::Contrast, [](model::Node& n) { model::ParamsOf<model::ContrastParams>(n).contrast = 2.f; });
    model::Connect(scene.Graph(), preset, "viewport", 0);
    model::Connect(scene.Graph(), video, contrast, 0);
    model::Connect(scene.Graph(), contrast, preset, static_cast<int>(model::PresetLayer::OnTop));
    REQUIRE(model::ValidateGraph(scene.Graph()).empty());

    const auto plan = engine::CompilePlan(scene.Graph());
    REQUIRE(plan.branches.size() == 1);
    CHECK(plan.branches[0].passes == std::vector<model::EffectPass>{{NodeType::Contrast, 2.f, 0.f, false}});
}

TEST_CASE("contrast: on the canvas, a video input and a logarithmic slider over the valid range", "[node:contrast]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::Contrast);
    test::FakeNodeContext context(scene);

    CHECK(app::NodeTitle(scene.Node(id), context) == "Contrast");
    CHECK(app::NodeInputs(scene.Node(id), context) == std::vector<app::InputSpec>{{0, {}, model::PortKind::Video, "Video"}});
    CHECK(app::NodeStatusOf(scene.Node(id), context).state == app::NodeState::Idle);
    context.live = true;
    CHECK(app::NodeStatusOf(scene.Node(id), context).state == app::NodeState::Live);

    const auto& slider = test::ControlById(NodeType::Contrast, "##contrast");
    CHECK(slider.kind == app::Control::Kind::Slider);
    CHECK(slider.min == 0.0);
    CHECK(slider.max == model::kMaxContrast);
    CHECK(slider.logarithmic);
    CHECK(std::string(slider.formatFor(1.5)) == "%.2f"); // fine steps where they matter
    CHECK(std::string(slider.formatFor(40.0)) == "%.0f");
    CHECK(std::string(slider.undo) == "Change Contrast");
    model::Node edited = scene.Node(id);
    slider.set(edited, 7.0);
    CHECK(Contrast(edited) == 7.0f);
    CHECK(slider.get(edited) == 7.0);
}

TEST_CASE("contrast: dragging the slider is one undo step that ends on release", "[node:contrast]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::Contrast);
    test::FakeNodeContext context(scene);
    test::HeadlessUi ui;

    ui.Frame(context, id);
    CHECK(context.inputRows.size() == 1);
    const ImVec2 slider = test::HeadlessUi::Centre(context, "(slider)");
    ui.Click(context, id, ImVec2(slider.x + 60.f, slider.y)); // right of the middle: more contrast
    REQUIRE_FALSE(context.edits.empty());
    for (const auto& edit : context.edits)
    {
        CHECK(edit.label == "Change Contrast");
        CHECK(edit.coalesce == "contrast");
    }
    CHECK(Contrast(scene.Node(id)) > 1.0f);
    CHECK(context.endedEdits == 1);
    CHECK(context.refused.empty());
}

TEST_CASE("contrast: in the Add menu with the effects", "[node:contrast]")
{
    const auto& ui = app::Ui(NodeType::Contrast);
    REQUIRE(ui.addItems.size() == 1);
    CHECK(std::string(ui.addItems[0].label) == "Contrast");
    CHECK(ui.menuGroup == app::Ui(NodeType::Saturation).menuGroup);
}
