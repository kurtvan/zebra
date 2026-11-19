#include "app/nodes/NodeDraw.hpp"

#include "app/Widgets.hpp"

namespace gv::app {

namespace {

enum Popup
{
    kUnitPopup = 1,
};

auto UnitName(model::IntervalUnit unit) -> const char*
{
    switch (unit)
    {
        case model::IntervalUnit::Seconds:
            return "seconds";
        case model::IntervalUnit::Beats:
            return "beats";
        case model::IntervalUnit::Bars:
            return "bars";
    }
    return "";
}

} // namespace

auto SwitcherTitle(const model::Node& node) -> std::string
{
    return model::ParamsOf<model::SwitcherParams>(node).kind == model::PortKind::Frame ? "Preset switcher" : "Video switcher";
}

void DrawSwitcherBody(const model::Node& node, NodeContext& context)
{
    const auto& params = model::ParamsOf<model::SwitcherParams>(node);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Every");
    ImGui::SameLine();
    const float unitWidth = 76.f;
    const float valueWidth = context.width - ImGui::CalcTextSize("Every").x - ImGui::GetStyle().ItemSpacing.x * 2.f - unitWidth;
    ImGui::SetNextItemWidth(valueWidth);
    if (params.unit == model::IntervalUnit::Seconds)
    {
        double seconds = params.intervalSeconds;
        const double min = model::kMinPlaylistInterval;
        const double max = model::kMaxPlaylistInterval;
        if (ImGui::SliderScalar("##seconds", ImGuiDataType_Double, &seconds, &min, &max, "%.3g", ImGuiSliderFlags_Logarithmic))
            context.Edit(node, "Change Interval", "interval", [&](model::Node& n) { model::ParamsOf<model::SwitcherParams>(n).intervalSeconds = seconds; });
    }
    else
    {
        int count = params.intervalCount;
        if (ImGui::SliderInt("##count", &count, 1, model::kMaxPlaylistCount, "%d", ImGuiSliderFlags_Logarithmic))
            context.Edit(node, "Change Interval", "interval", [&](model::Node& n) { model::ParamsOf<model::SwitcherParams>(n).intervalCount = count; });
    }
    context.EndEditIfReleased();
    context.Tooltip("Double-click (or Cmd+click) to type a value.");
    ImGui::SameLine();
    if (ImGui::Button((std::string(UnitName(params.unit)) + "##unit").c_str(), ImVec2(unitWidth, 0.f)))
        context.OpenPopup(node, kUnitPopup);
    context.Tooltip("Beats and bars follow the MIDI clock.");

    bool shuffle = params.shuffle;
    if (ImGui::Checkbox("Shuffle", &shuffle))
        context.EditOnce(node, shuffle ? "Shuffle" : "Play in Order", [&](model::Node& n) { model::ParamsOf<model::SwitcherParams>(n).shuffle = shuffle; });
    context.Tooltip("Off: top to bottom. On: random order, never the same input twice in a row.");

    for (const auto& spec : NodeInputs(node, context))
        context.InputRow(node, spec);
}

void DrawSwitcherPopup(const model::Node& node, int kind, NodeContext& context)
{
    if (kind != kUnitPopup)
        return;
    const auto current = model::ParamsOf<model::SwitcherParams>(node).unit;
    for (auto unit : {model::IntervalUnit::Seconds, model::IntervalUnit::Beats, model::IntervalUnit::Bars})
    {
        if (ImGui::Selectable(UnitName(unit), unit == current) && unit != current)
            context.EditOnce(node, "Change Interval Unit", [&](model::Node& n) { model::ParamsOf<model::SwitcherParams>(n).unit = unit; });
    }
}

} // namespace gv::app
