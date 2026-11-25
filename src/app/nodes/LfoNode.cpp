#include "app/nodes/NodeUi.hpp"

#include "app/WireLook.hpp"

#include <algorithm>
#include <iterator>

namespace gv::app::nodes {

namespace {

using model::LfoParams;

constexpr const char* kShapes[] = {"Sine", "Triangle", "Saw", "Square"};            // by LfoShape
constexpr const char* kBeats[] = {"1/4 beat", "1/2 beat", "1 beat", "2 beats", "4 beats", "8 beats"}; // by kLfoBeats

/// The LFO's value now, as a bar across the node.
void DrawValue(const model::Node& node, NodeContext& context)
{
    const auto signals = context.Signals();
    const auto it = signals.find(node.id);
    const float value = it != signals.end() ? std::clamp(it->second, 0.f, 1.f) : 0.f;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + context.width, min.y + 6.f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), 2.f);
    draw->AddRectFilled(min, ImVec2(min.x + context.width * value, max.y), ImGui::GetColorU32(kNumberWire), 2.f);
    ImGui::Dummy(ImVec2(context.width, 6.f));
}

} // namespace

auto LfoUi() -> NodeUi
{
    NodeUi ui;
    ui.type = model::NodeType::Lfo;
    ui.header = ImVec4(0.55f, 0.32f, 0.14f, 0.60f);
    ui.menuGroup = 4;

    Control shape;
    shape.kind = Control::Kind::Choice;
    shape.id = "##shape";
    shape.label = "Shape";
    shape.options = kShapes;
    shape.get = [](const model::Node& node) { return static_cast<double>(model::ParamsOf<LfoParams>(node).shape); };
    shape.set = [](model::Node& node, double value) { model::ParamsOf<LfoParams>(node).shape = static_cast<model::LfoShape>(value); };
    shape.undo = "Change LFO Shape";

    Control beats;
    beats.kind = Control::Kind::Choice;
    beats.id = "##beats";
    beats.label = "Every";
    beats.options = kBeats;
    beats.get = [](const model::Node& node) {
        const float value = model::ParamsOf<LfoParams>(node).beats;
        return static_cast<double>(std::ranges::find(model::kLfoBeats, value) - std::begin(model::kLfoBeats));
    };
    beats.set = [](model::Node& node, double value) { model::ParamsOf<LfoParams>(node).beats = model::kLfoBeats[static_cast<size_t>(value)]; };
    beats.undo = "Change LFO Rate";
    beats.tooltip = "One cycle's length while the MIDI clock runs, in time with the song.";

    Control phase;
    phase.kind = Control::Kind::Slider;
    phase.id = "##phase";
    phase.get = ParamGet<LfoParams, &LfoParams::phase>;
    phase.set = ParamSet<LfoParams, &LfoParams::phase>;
    phase.format = "phase %.2f";
    phase.undo = "Change LFO Phase";
    phase.field = "phase";
    phase.tooltip = "How far into its cycle the LFO starts: 0.5 is half a cycle later.";

    Control period;
    period.kind = Control::Kind::Slider;
    period.id = "##period";
    period.get = ParamGet<LfoParams, &LfoParams::periodSeconds>;
    period.set = ParamSet<LfoParams, &LfoParams::periodSeconds>;
    period.min = 0.1;
    period.max = 60.0;
    period.logarithmic = true;
    period.format = "no clock: %.1f s";
    period.undo = "Change LFO Period";
    period.field = "periodSeconds";
    period.tooltip = "One cycle's length when there's no MIDI clock running.";

    Control value;
    value.kind = Control::Kind::Custom;
    value.id = "##value";
    value.draw = DrawValue;
    value.tooltip = "The LFO's value now, from 0 to 1. Wire it into a setting's diamond to move that setting.";

    ui.controls = {shape, beats, phase, period, value};
    ui.addItems = {{"LFO", nullptr, "A number that rises and falls in time with the beat, to move a setting."}};
    return ui;
}

} // namespace gv::app::nodes
