#include "app/nodes/NodeUi.hpp"

#include "app/Widgets.hpp"
#include "app/WireLook.hpp"
#include "model/nodes/NodeDef.hpp"

#include <array>
#include <cmath>
#include <cstdlib>

namespace gv::app {

using model::NodeType;

namespace {

auto BuildUis() -> std::array<NodeUi, model::kNodeTypeCount>
{
    std::array<NodeUi, model::kNodeTypeCount> uis = {
        nodes::VideoSourceUi(), nodes::ContrastUi(), nodes::SaturationUi(), nodes::LumaGateUi(),
        nodes::OpacityUi(),         nodes::MilkdropPresetUi(),   nodes::SwitcherUi(),   nodes::ViewportUi(),
        nodes::IsfShaderUi(),  nodes::LfoUi(),          nodes::AudioInUi(),     nodes::EnvelopeUi(),
        nodes::BandUi(),        nodes::MathUi(),
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

/// Where `value` sits along `control`'s slider, 0..1, as ImGui places its grab: a logarithmic slider spaces
/// values by ratio, from its smallest step (0.01) up.
auto SliderRatio(const Control& control, double value) -> double
{
    if (!control.logarithmic)
        return (value - control.min) / (control.max - control.min);
    const double floor = std::max(control.min, 0.01);
    return std::log(std::max(value, floor) / floor) / std::log(control.max / floor);
}

/// After a modulated slider, on its row: the value the setting has this frame, as a marker on the slider, then
/// the depth.
void DrawModulation(const model::Node& node, const Control& control, const model::Node& modulator, float width, NodeContext& context)
{
    const auto signals = context.Signals();
    if (const auto it = signals.find(modulator.id); it != signals.end())
    {
        const double value = model::Modulate(node, control.param, it->second);
        const float grab = 2.f + ImGui::GetStyle().GrabMinSize * 0.5f; // where ImGui's grab centre stops at either end
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const float x = min.x + grab + static_cast<float>(SliderRatio(control, value)) * (max.x - min.x - 2.f * grab);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(x, min.y + 1.f), ImVec2(x, max.y - 1.f), ImGui::GetColorU32(WireColor(model::PortKind::Number)), 2.f);
    }
    const std::string name = model::ParamsFor(node)[static_cast<size_t>(control.param)].name;
    float depth = model::DepthOf(node, control.param);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(width);
    if (ImGui::SliderFloat("##depth", &depth, -1.f, 1.f, "depth %.2f"))
        context.Edit(node, "Change Depth", ("depth:" + name).c_str(), [&](model::Node& n) { n.depths[name] = depth; });
    context.EndEditIfReleased();
    context.Tooltip("How far the wire moves the setting either side of the slider (negative: the other way).");
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
    const auto set = control.set;
    const model::Node* modulator = nullptr; // what's wired into the slider's modulation input
    if (control.param >= 0)
    {
        const int input = model::ParamInput(node, control.param);
        context.ParamPin(node, {input, {}, model::PortKind::Number, model::ParamsFor(node)[static_cast<size_t>(control.param)].label});
        ImGui::SameLine(0.f, 4.f);
        modulator = context.SceneGraph().Upstream(node.id, input);
    }
    const float rest = control.width > 0.f ? control.width : RemainingWidth(controls, index, node, rowStart, context.width);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float width = modulator != nullptr ? std::floor((rest - spacing) * 0.6f) : rest;

    switch (control.kind)
    {
        case Control::Kind::Inputs:
            // One row per input: its pin and label (drawn by the canvas), then its tooltip on hover. A
            // double-click on the row goes to the node type's handler, if it has one (a switcher jumps to
            // that input).
            for (const auto& spec : NodeInputs(node, context))
            {
                const bool doubleClicked = context.InputRow(node, spec);
                if (!spec.tooltip.empty())
                    context.Tooltip(spec.tooltip.c_str());
                if (const auto& onDoubleClick = Ui(node.type).inputDoubleClicked; doubleClicked && onDoubleClick != nullptr)
                    onDoubleClick(node, spec.input, context);
            }
            break;
        case Control::Kind::Slider:
        {
            if (modulator != nullptr && model::ParamsFor(node)[static_cast<size_t>(control.param)].passThrough)
            {
                // The wire's number is the setting: shown, not a slider to drag.
                const auto signals = context.Signals();
                const auto it = signals.find(modulator->id);
                ImGui::AlignTextToFramePadding();
                if (it != signals.end())
                    ImGui::TextColored(kNumberWire, control.format, static_cast<double>(it->second));
                else
                    ImGui::TextDisabled("%s", model::ParamsFor(node)[static_cast<size_t>(control.param)].label);
                break;
            }
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
            if (modulator != nullptr && control.tooltip != nullptr)
                context.Tooltip(control.tooltip);
            if (modulator != nullptr)
                DrawModulation(node, control, *modulator, rest - width - spacing, context);
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
            {
                // Wrapped to the node's width: a compiler message would otherwise run past its edge.
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
                ImGui::PushStyleColor(ImGuiCol_Text, control.warning ? kRed : kDim);
                ImGui::TextUnformatted(text.c_str());
                ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
            }
            break;
        }
        case Control::Kind::Custom:
            control.draw(node, context);
            break;
        case Control::Kind::Params:
            break; // NodeControls has made it the param sliders
    }
    if (control.tooltip != nullptr && modulator == nullptr)
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
    for (int i = 0; i < model::ParamInput(node, 0); ++i)
        inputs.push_back({i, {}, *model::InputKind(node, i), ui.inputLabel});
    return inputs;
}

auto ParamSlider(const model::Node& node, int param) -> Control
{
    const model::ParamDef& def = model::ParamsFor(node)[static_cast<size_t>(param)];
    Control slider;
    slider.kind = Control::Kind::Slider;
    slider.id = "##param"; // each control has its own ImGui id scope (its index), so this needn't be unique
    // The node type keeps its params alive (ParamsFor), so the slider may point at this one.
    slider.get = [p = &def](const model::Node& n) { return static_cast<double>(p->get(n, *p)); };
    slider.set = [p = &def](model::Node& n, double value) { p->set(n, *p, static_cast<float>(value)); };
    slider.min = def.low;
    slider.max = def.high;
    slider.integer = def.integer;
    slider.logarithmic = def.logarithmic;
    slider.format = def.format;
    slider.formatFor = def.formatFor;
    slider.param = param;
    slider.undo = def.undo;
    slider.field = def.name;
    slider.tooltip = def.tooltip;
    return slider;
}

auto NodeControls(const model::Node& node, NodeContext& context) -> std::vector<Control>
{
    const NodeUi& ui = Ui(node.type);
    std::vector<Control> controls;
    for (auto& control : ui.controlsFor != nullptr ? ui.controlsFor(node, context) : ui.controls)
    {
        if (control.kind != Control::Kind::Params)
        {
            controls.push_back(std::move(control));
            continue;
        }
        const auto params = model::ParamsFor(node);
        for (int param = 0; param < static_cast<int>(params.size()); ++param)
        {
            if (control.param >= 0 && param != control.param)
                continue;
            // A hidden param still shows while it's wired, so its wire has a pin to plug into.
            const auto visible = params[static_cast<size_t>(param)].visible;
            if (visible == nullptr || visible(node) || context.SceneGraph().Upstream(node.id, model::ParamInput(node, param)) != nullptr)
                controls.push_back(ParamSlider(node, param));
        }
    }
    return controls;
}

void DrawNodeBody(const model::Node& node, NodeContext& context)
{
    const auto controls = NodeControls(node, context);
    float rowStart = ImGui::GetCursorScreenPos().x;
    for (size_t i = 0; i < controls.size(); ++i)
    {
        if (Visible(controls[i], node))
            DrawControl(controls, i, node, context, rowStart);
    }
}

void DrawNodePopup(const model::Node& node, int kind, NodeContext& context)
{
    const auto controls = NodeControls(node, context);
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
