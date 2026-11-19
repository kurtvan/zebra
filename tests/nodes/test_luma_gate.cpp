// The Luma gate node: model/nodes/LumaGateNode.cpp, app/nodes/LumaGateNode.cpp. Its shader: tests/gl/test_effect_nodes.cpp.

#include "nodes/NodeTestKit.hpp"

#include "engine/RenderPlan.hpp"

using namespace gv;
using model::LumaGateParams;
using model::NodeType;

namespace {

auto Gate(const model::Node& node) -> const LumaGateParams&
{
    return model::ParamsOf<LumaGateParams>(node);
}

} // namespace

TEST_CASE("luma gate: an effect that cuts nothing by default", "[node:luma-gate]")
{
    const auto& def = model::Def(NodeType::LumaGate);
    CHECK(def.effect);
    CHECK(std::string(def.jsonName) == "lumaGate");
    CHECK(std::string(def.displayName) == "Luma gate");
    test::NodeScene scene;
    const auto& node = scene.Node(scene.Add(NodeType::LumaGate));
    CHECK(node.id == "gate-1");
    CHECK(Gate(node) == LumaGateParams{0.f, 0.f, false});
    CHECK(model::InputKind(node, 0) == model::PortKind::Video);
}

TEST_CASE("luma gate: threshold and softness are each 0 to 1, and both are reported", "[node:luma-gate]")
{
    auto set = [](float threshold, float softness) {
        return [=](model::Node& node) {
            model::ParamsOf<LumaGateParams>(node).threshold = threshold;
            model::ParamsOf<LumaGateParams>(node).softness = softness;
        };
    };
    CHECK(test::ParamErrors(NodeType::LumaGate, set(1.f, 1.f)).empty());
    CHECK(test::ParamErrors(NodeType::LumaGate, set(1.5f, -0.5f)) ==
          std::vector<std::string>{"Luma gate: threshold must be between 0 and 1.", "Luma gate: softness must be between 0 and 1."});
}

TEST_CASE("luma gate: saved with threshold, softness and invert, and read back", "[node:luma-gate]")
{
    test::NodeScene scene;
    auto& node = scene.Node(scene.Add(NodeType::LumaGate, [](model::Node& n) { model::ParamsOf<LumaGateParams>(n) = {0.3f, 0.2f, true}; }));
    CHECK(test::RoundTrip(node) == node);
    auto [partial, issues] = test::ReadNode(NodeType::LumaGate, {{"invert", true}});
    CHECK(Gate(partial) == LumaGateParams{0.f, 0.f, true});
    CHECK(issues.empty());
    CHECK(test::ReadNode(NodeType::LumaGate, {{"invert", 3.5}}).second.size() == 1);
}

TEST_CASE("luma gate: compiles to a gate pass carrying all three settings", "[node:luma-gate]")
{
    test::NodeScene scene;
    const std::string preset = scene.Add(NodeType::Preset);
    const std::string video = scene.Add(NodeType::VideoSource);
    const std::string gate = scene.Add(NodeType::LumaGate, [](model::Node& n) { model::ParamsOf<LumaGateParams>(n) = {0.4f, 0.1f, true}; });
    model::Connect(scene.Graph(), preset, "viewport", 0);
    model::Connect(scene.Graph(), video, gate, 0);
    model::Connect(scene.Graph(), gate, preset, static_cast<int>(model::PresetLayer::Feedback));
    const auto plan = engine::CompilePlan(scene.Graph());
    REQUIRE(plan.branches.size() == 1);
    CHECK(plan.branches[0].passes == std::vector<model::EffectPass>{{NodeType::LumaGate, 0.4f, 0.1f, true}});
}

TEST_CASE("luma gate: on the canvas, two sliders and an Invert checkbox with its own undo labels", "[node:luma-gate]")
{
    const auto sliders = test::ControlsOf(NodeType::LumaGate, app::Control::Kind::Slider);
    REQUIRE(sliders.size() == 2);
    CHECK(std::string(sliders[0]->field) == "threshold");
    CHECK(std::string(sliders[1]->field) == "softness");
    for (const auto* slider : sliders)
    {
        CHECK(slider->min == 0.0);
        CHECK(slider->max == 1.0);
    }
    const auto& invert = test::ControlById(NodeType::LumaGate, "##invert");
    CHECK(std::string(invert.undo) == "Invert Luma Gate");
    CHECK(std::string(invert.undoOff) == "Uninvert Luma Gate");
}

TEST_CASE("luma gate: clicking Invert turns it on, and again off, one undo step each", "[node:luma-gate]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::LumaGate);
    test::FakeNodeContext context(scene);
    test::HeadlessUi ui;
    ui.Frame(context, id);
    const ImVec2 invert = test::HeadlessUi::Centre(context, "Keeps the video's dark parts instead of its bright ones.");

    ui.Click(context, id, invert);
    CHECK(Gate(scene.Node(id)).invert);
    REQUIRE(context.edits.size() == 1);
    CHECK(context.edits[0].label == "Invert Luma Gate");
    CHECK(context.edits[0].coalesce.empty());

    ui.Click(context, id, invert);
    CHECK_FALSE(Gate(scene.Node(id)).invert);
    REQUIRE(context.edits.size() == 2);
    CHECK(context.edits[1].label == "Uninvert Luma Gate");
}
