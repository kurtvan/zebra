#include "app/nodes/NodeDraw.hpp"

#include "app/Shell.hpp"
#include "app/Widgets.hpp"

namespace gv::app {

using model::NodeType;

auto NodeWidth(NodeType type) -> float
{
    switch (type)
    {
        case NodeType::VideoSource:
            return 240.f;
        case NodeType::Preset:
            return 260.f;
        case NodeType::Switcher:
        case NodeType::Viewport:
            return 220.f;
        default:
            return 180.f; // effects
    }
}

auto NodeTitle(const model::Node& node, NodeContext& context) -> std::string
{
    switch (node.type)
    {
        case NodeType::VideoSource:
            return VideoSourceTitle(node, context);
        case NodeType::Preset:
            return PresetTitle(node);
        case NodeType::Switcher:
            return SwitcherTitle(node);
        default:
            return model::DisplayName(node.type);
    }
}

auto NodeStatusOf(const model::Node& node, NodeContext& context) -> NodeStatus
{
    switch (node.type)
    {
        case NodeType::VideoSource:
            return VideoSourceStatus(node, context);
        case NodeType::Preset:
            return PresetStatus(node, context);
        default:
            return context.live ? NodeStatus{kGreen, "The scene is live."} : NodeStatus{ImVec4(0.35f, 0.36f, 0.40f, 1.f), {}};
    }
}

auto NodeInputs(const model::Node& node, NodeContext& context) -> std::vector<InputSpec>
{
    switch (node.type)
    {
        case NodeType::VideoSource:
            return {};
        case NodeType::Preset:
            return PresetInputs(node, context);
        case NodeType::Switcher:
        {
            const auto& params = model::ParamsOf<model::SwitcherParams>(node);
            std::vector<InputSpec> inputs;
            for (int i = 0; i < params.inputCount; ++i)
            {
                const bool wired = context.sceneData.graph.WireInto(node.id, i) != nullptr;
                inputs.push_back({i, {}, params.kind, std::to_string(i + 1) + (wired ? "" : "  (free)")});
            }
            return inputs;
        }
        case NodeType::Viewport:
            return {{0, {}, model::PortKind::Frame, "Picture"}};
        default:
            return {{0, {}, model::PortKind::Video, "Video"}}; // effects
    }
}

void DrawNodeBody(const model::Node& node, NodeContext& context)
{
    switch (node.type)
    {
        case NodeType::VideoSource:
            DrawVideoSourceBody(node, context);
            break;
        case NodeType::Preset:
            DrawPresetBody(node, context);
            break;
        case NodeType::Switcher:
            DrawSwitcherBody(node, context);
            break;
        case NodeType::Viewport:
            DrawViewportBody(node, context);
            break;
        default:
            DrawEffectBody(node, context);
            break;
    }
}

void DrawNodePopup(const model::Node& node, int kind, NodeContext& context)
{
    switch (node.type)
    {
        case NodeType::VideoSource:
            DrawVideoSourcePopup(node, kind, context);
            break;
        case NodeType::Switcher:
            DrawSwitcherPopup(node, kind, context);
            break;
        default:
            ImGui::CloseCurrentPopup();
            break;
    }
}

} // namespace gv::app
