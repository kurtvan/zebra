#include "app/nodes/NodeUi.hpp"

#include "app/WireLook.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace gv::app::nodes {

namespace {

using model::EnvelopeParams;

constexpr size_t kHistory = 120; //!< Frames of the plot: 2 s at 60 fps.
constexpr float kPlotHeight = 34.f;

/// The last kHistory values of each Envelope on the canvas, by node id: what its plot scrolls through. Only
/// for drawing; not saved and not undone.
struct History
{
    std::vector<float> values;
    int frame{-1}; //!< The ImGui frame of the last value, so a node drawn twice in a frame adds one.
};

/// The Envelope's level over the last two seconds, scrolling left, with a dot at its value now.
void DrawLevel(const model::Node& node, NodeContext& context)
{
    static std::map<std::string, History> histories;
    const auto signals = context.Signals();
    const auto it = signals.find(node.id);
    const float value = it != signals.end() ? std::clamp(it->second, 0.f, 1.f) : 0.f;
    History& history = histories[node.id];
    if (history.frame != ImGui::GetFrameCount())
    {
        history.frame = ImGui::GetFrameCount();
        history.values.push_back(value);
        if (history.values.size() > kHistory)
            history.values.erase(history.values.begin());
    }

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = context.width;
    const float inset = 3.f;
    auto at = [&](size_t i, float v) {
        const float x = static_cast<float>(kHistory - history.values.size() + i) / static_cast<float>(kHistory - 1);
        return ImVec2(min.x + x * width, min.y + inset + (1.f - v) * (kPlotHeight - 2.f * inset));
    };
    std::vector<ImVec2> points;
    for (size_t i = 0; i < history.values.size(); ++i)
        points.push_back(at(i, history.values[i]));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, ImVec2(min.x + width, min.y + kPlotHeight), ImGui::GetColorU32(ImGuiCol_FrameBg), 3.f);
    draw->AddPolyline(points.data(), static_cast<int>(points.size()), ImGui::GetColorU32(kNumberWire), ImDrawFlags_None, 1.5f);
    if (!points.empty())
        draw->AddCircleFilled(points.back(), 3.5f, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 1.f)));
    ImGui::Dummy(ImVec2(width, kPlotHeight));
}

} // namespace

auto EnvelopeUi() -> NodeUi
{
    NodeUi ui;
    ui.type = model::NodeType::Envelope;
    ui.header = ImVec4(0.55f, 0.32f, 0.14f, 0.60f);
    ui.menuGroup = 4;
    ui.inputLabel = "Audio";

    Control autoLevel;
    autoLevel.kind = Control::Kind::Checkbox;
    autoLevel.id = "##autoLevel";
    autoLevel.label = "Auto-level";
    autoLevel.get = ParamGet<EnvelopeParams, &EnvelopeParams::autoLevel>;
    autoLevel.set = ParamSet<EnvelopeParams, &EnvelopeParams::autoLevel>;
    autoLevel.undo = "Turn On Envelope Auto-level";
    autoLevel.undoOff = "Turn Off Envelope Auto-level";
    autoLevel.tooltip = "The loudest of the last few seconds is 1, so quiet and loud passages both use the whole range. Off: the gain sets it.";

    Control level;
    level.kind = Control::Kind::Custom;
    level.id = "##level";
    level.draw = DrawLevel;
    level.tooltip = "How loud the audio is now, from 0 to 1, over the last two seconds. Wire it into a setting's diamond to move that setting.";

    ui.controls = {{Control::Kind::Inputs}, ParamRow(0), ParamRow(1), autoLevel, ParamRow(2), ParamRow(3), level};
    ui.addItems = {{"Envelope", nullptr, "A number that follows how loud the music is, to move a setting."}};
    return ui;
}

} // namespace gv::app::nodes
