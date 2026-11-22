#include "app/nodes/NodeUi.hpp"

#include "app/Widgets.hpp"
#include "model/nodes/NodeDef.hpp"

#include <array>
#include <cstdlib>

namespace gv::app {

using model::NodeType;

namespace {

auto BuildUis() -> std::array<NodeUi, model::kNodeTypeCount>
{
    std::array<NodeUi, model::kNodeTypeCount> uis = {
        nodes::VideoSourceUi(), nodes::ContrastUi(), nodes::SaturationUi(), nodes::LumaGateUi(),
        nodes::OpacityUi(),         nodes::MilkdropPresetUi(),   nodes::SwitcherUi(),   nodes::ViewportUi(),
    };
    // Ui() indexes by type, so each definition must sit at its own type's index.
    for (int i = 0; i < model::kNodeTypeCount; ++i)
    {
        if (static_cast<int>(uis[static_cast<size_t>(i)].type) != i)
            std::abort();
    }
    return uis;
}

auto Uis() -> const std::array<NodeUi, model::kNodeTypeCount>&
{
    static const auto uis = BuildUis();
    return uis;
}

auto Visible(const Control& control, const model::Node& node) -> bool
{
    return control.visible == nullptr || control.visible(node);
}

/// The width left for `controls[index]` on its row: the node's width less what's already on the row and the
/// fixed-width controls after it on the same row.
auto RemainingWidth(const std::vector<Control>& controls, size_t index, const model::Node& node, float rowStart, float nodeWidth) -> float
{
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    float after = 0.f;
    for (size_t next = index + 1; next < controls.size() && controls[next].sameLine; ++next)
    {
        if (Visible(controls[next], node))
            after += controls[next].width + spacing;
    }
    return std::max(rowStart + nodeWidth - ImGui::GetCursorScreenPos().x - after, 20.f);
}

void DrawControl(const std::vector<Control>& controls, size_t index, const model::Node& node, NodeContext& context, float& rowStart)
{
    const Control& control = controls[index];
    if (control.sameLine)
        ImGui::SameLine();
    else
        rowStart = ImGui::GetCursorScreenPos().x;
    ImGui::PushID(static_cast<int>(index));
    context.control = static_cast<int>(index);

    if (control.label != nullptr && control.kind != Control::Kind::Checkbox)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(control.label);
        ImGui::SameLine();
    }
    const float width = control.width > 0.f ? control.width : RemainingWidth(controls, index, node, rowStart, context.width);
    const auto set = control.set;

    switch (control.kind)
    {
        case Control::Kind::Inputs:
            for (const auto& spec : NodeInputs(node, context))
                context.InputRow(node, spec);
            break;
        case Control::Kind::Slider:
        {
            const ImGuiSliderFlags flags = control.logarithmic ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None;
            const double current = control.get(node);
            const char* format = control.formatFor != nullptr ? control.formatFor(current) : control.format;
            ImGui::SetNextItemWidth(width);
            bool changed = false;
            double value = current;
            if (control.integer)
            {
                int whole = static_cast<int>(current);
                changed = ImGui::SliderInt(control.id, &whole, static_cast<int>(control.min), static_cast<int>(control.max), format, flags);
                value = whole;
            }
            else
            {
                changed = ImGui::SliderScalar(control.id, ImGuiDataType_Double, &value, &control.min, &control.max, format, flags);
            }
            if (changed)
                context.Edit(node, control.undo, control.field != nullptr ? control.field : control.id, [&](model::Node& n) { set(n, value); });
            context.EndEditIfReleased();
            break;
        }
        case Control::Kind::Checkbox:
        {
            bool on = control.get(node) != 0.0;
            if (ImGui::Checkbox(control.label != nullptr ? control.label : control.id, &on))
            {
                const char* undo = on || control.undoOff == nullptr ? control.undo : control.undoOff;
                context.EditOnce(node, undo, [&](model::Node& n) { set(n, on ? 1.0 : 0.0); });
            }
            break;
        }
        case Control::Kind::Choice:
        {
            const int current = static_cast<int>(control.get(node));
            if (control.radio)
            {
                for (int i = 0; i < static_cast<int>(control.options.size()); ++i)
                {
                    if (i > 0)
                        ImGui::SameLine();
                    if (ImGui::RadioButton(control.options[static_cast<size_t>(i)], current == i) && i != current)
                        context.EditOnce(node, control.undo, [&](model::Node& n) { set(n, i); });
                }
            }
            else
            {
                const bool valid = current >= 0 && current < static_cast<int>(control.options.size());
                const std::string text = std::string(valid ? control.options[static_cast<size_t>(current)] : "?") + control.id;
                if (ImGui::Button(text.c_str(), ImVec2(width, 0.f)))
                    context.OpenPopup(node, static_cast<int>(index));
            }
            break;
        }
        case Control::Kind::Text:
        {
            const std::string text = control.text(node, context);
            if (!text.empty())
                ImGui::TextColored(control.warning ? kRed : kDim, "%s", text.c_str());
            break;
        }
        case Control::Kind::Custom:
            control.draw(node, context);
            break;
    }
    if (control.tooltip != nullptr)
        context.Tooltip(control.tooltip);
    context.control = -1;
    ImGui::PopID();
}

} // namespace

auto StateColor(NodeState state) -> ImVec4
{
    switch (state)
    {
        case NodeState::Empty:
            return kDim;
        case NodeState::Missing:
            return kRed;
        case NodeState::Ready:
            return ImVec4(0.85f, 0.86f, 0.9f, 1.f);
        case NodeState::Waiting:
            return kAmber;
        case NodeState::Live:
            return kGreen;
        case NodeState::Idle:
            break;
    }
    return ImVec4(0.35f, 0.36f, 0.40f, 1.f);
}

auto Ui(NodeType type) -> const NodeUi&
{
    return Uis()[static_cast<size_t>(type)];
}

auto NodeUis() -> std::span<const NodeUi>
{
    return Uis();
}

auto NodeTitle(const model::Node& node, NodeContext& context) -> std::string
{
    const NodeUi& ui = Ui(node.type);
    return ui.title != nullptr ? ui.title(node, context) : model::Label(node);
}

auto NodeStatusOf(const model::Node& node, NodeContext& context) -> NodeStatus
{
    const NodeUi& ui = Ui(node.type);
    if (ui.status != nullptr)
        return ui.status(node, context);
    return context.Live() ? NodeStatus{NodeState::Live, "The scene is live."} : NodeStatus{};
}

auto NodeInputs(const model::Node& node, NodeContext& context) -> std::vector<InputSpec>
{
    const NodeUi& ui = Ui(node.type);
    if (ui.inputs != nullptr)
        return ui.inputs(node, context);
    std::vector<InputSpec> inputs;
    for (int i = 0; i < model::InputCount(node); ++i)
        inputs.push_back({i, {}, *model::InputKind(node, i), ui.inputLabel});
    return inputs;
}

void DrawNodeBody(const model::Node& node, NodeContext& context)
{
    const auto& controls = Ui(node.type).controls;
    float rowStart = ImGui::GetCursorScreenPos().x;
    for (size_t i = 0; i < controls.size(); ++i)
    {
        if (Visible(controls[i], node))
            DrawControl(controls, i, node, context, rowStart);
    }
}

void DrawNodePopup(const model::Node& node, int kind, NodeContext& context)
{
    const auto& controls = Ui(node.type).controls;
    if (kind < 0 || kind >= static_cast<int>(controls.size()))
    {
        ImGui::CloseCurrentPopup();
        return;
    }
    const Control& control = controls[static_cast<size_t>(kind)];
    if (control.kind == Control::Kind::Choice)
    {
        const int current = static_cast<int>(control.get(node));
        const auto set = control.set;
        for (int i = 0; i < static_cast<int>(control.options.size()); ++i)
        {
            if (ImGui::Selectable(control.options[static_cast<size_t>(i)], i == current) && i != current)
                context.EditOnce(node, control.undo, [&](model::Node& n) { set(n, i); });
        }
    }
    else if (control.kind == Control::Kind::Custom && control.popup != nullptr)
    {
        control.popup(node, context);
    }
    else
    {
        ImGui::CloseCurrentPopup();
    }
}

} // namespace gv::app
