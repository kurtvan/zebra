#include "app/nodes/NodeUi.hpp"

#include "app/WireLook.hpp"
#include "model/nodes/MathNode.hpp"

namespace gv::app::nodes {

namespace {

using model::MathOp;

template <MathOp Op>
void Configure(model::Node& node)
{
    node.params = model::MathDefaults(Op);
}

/// While the node has no answer: why, and that it holds its last value.
auto HoldingNote(const model::Node& node, NodeContext& context) -> std::string
{
    const model::Node modulated = model::ModulatedNode(context.SceneGraph(), node, context.Signals());
    const char* holding = model::MathAnswerOf(model::ParamsOf<model::MathParams>(modulated)).holding;
    return holding != nullptr ? holding : "";
}

/// The node's value this frame.
void DrawValue(const model::Node& node, NodeContext& context)
{
    const auto signals = context.Signals();
    const auto it = signals.find(node.id);
    ImGui::TextColored(kNumberWire, "= %.3f", it != signals.end() ? static_cast<double>(it->second) : 0.0);
}

} // namespace

auto MathUi() -> NodeUi
{
    NodeUi ui;
    ui.type = model::NodeType::Math;
    ui.width = 160.f;
    ui.header = ImVec4(0.55f, 0.32f, 0.14f, 0.60f);
    ui.menuGroup = 5;

    Control holding;
    holding.kind = Control::Kind::Text;
    holding.id = "##holding";
    holding.text = HoldingNote;

    Control value;
    value.kind = Control::Kind::Custom;
    value.id = "##value";
    value.draw = DrawValue;
    value.tooltip = "The answer now. It isn't kept between 0 and 1: the setting it moves stops at its own ends.";

    ui.controls = {{Control::Kind::Params}, holding, value};
    ui.addItems = {
        {"Sum", Configure<MathOp::Sum>, "Adds numbers together. A new input appears as you wire the last one."},
        {"Difference", Configure<MathOp::Difference>, "Takes b away from a."},
        {"Multiply", Configure<MathOp::Multiply>, "Multiplies numbers together: an LFO times 0.3 swings gently. A new input appears as you wire the last one."},
        {"Divide", Configure<MathOp::Divide>, "Divides a by b. Dividing by zero holds the last value."},
        {"Log", Configure<MathOp::Log>, "Bends a number from 0 to 1 along a log curve: lifts the low end and softens the top."},
        {"Absolute", Configure<MathOp::Absolute>, "Makes a negative number positive."},
    };
    return ui;
}

} // namespace gv::app::nodes
