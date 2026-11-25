#include "app/nodes/NodeUi.hpp"

#include "app/WireLook.hpp"
#include "model/nodes/LfoNode.hpp"

#include <algorithm>
#include <iterator>
#include <vector>

namespace gv::app::nodes {

namespace {

using model::LfoParams;

constexpr const char* kShapes[] = {"Sine", "Triangle", "Saw", "Square"};            // by LfoShape
constexpr const char* kBeats[] = {"1/4 beat", "1/2 beat", "1 beat", "2 beats", "4 beats", "8 beats"}; // by kLfoBeats

constexpr int kCurvePoints = 64;
constexpr float kPlotHeight = 34.f;

/// One cycle of the wave (the phase applied) and a playhead where the LFO is now, its value on the curve.
void DrawWave(const model::Node& node, NodeContext& context)
{
    const auto& params = model::ParamsOf<LfoParams>(node);
    const auto curve = model::LfoCurve(params, kCurvePoints);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = context.width;
    const float inset = 3.f; // the line's width stays inside the plot
    auto at = [&](double position, float value) {
        return ImVec2(min.x + static_cast<float>(position) * width, min.y + inset + (1.f - value) * (kPlotHeight - 2.f * inset));
    };
    std::vector<ImVec2> points;
    for (int i = 0; i <= kCurvePoints; ++i)
    {
        const double position = static_cast<double>(i) / kCurvePoints;
        // A square's edges are vertical: both levels at the same x.
        if (i > 0 && i < kCurvePoints && params.shape == model::LfoShape::Square && curve[static_cast<size_t>(i)] != curve[static_cast<size_t>(i - 1)])
            points.push_back(at(position, curve[static_cast<size_t>(i - 1)]));
        points.push_back(at(position, i < kCurvePoints ? curve[static_cast<size_t>(i)] : model::LfoValueAt(params, 1.0)));
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, ImVec2(min.x + width, min.y + kPlotHeight), ImGui::GetColorU32(ImGuiCol_FrameBg), 3.f);
    draw->AddPolyline(points.data(), static_cast<int>(points.size()), ImGui::GetColorU32(kNumberWire), ImDrawFlags_None, 1.5f);
    const double position = model::LfoPosition(params, context.SignalInputs());
    const ImVec2 head = at(position, model::LfoValueAt(params, position));
    draw->AddLine(ImVec2(head.x, min.y), ImVec2(head.x, min.y + kPlotHeight), ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.25f)));
    draw->AddCircleFilled(head, 3.5f, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 1.f)));
    ImGui::Dummy(ImVec2(width, kPlotHeight));
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

    Control wave;
    wave.kind = Control::Kind::Custom;
    wave.id = "##wave";
    wave.draw = DrawWave;
    wave.tooltip = "One cycle of the LFO, and where it is now (the dot). Wire it into a setting's diamond to move that setting.";

    ui.controls = {shape, beats, phase, period, wave};
    ui.addItems = {{"LFO", nullptr, "A number that rises and falls in time with the beat, to move a setting."}};
    return ui;
}

} // namespace gv::app::nodes
