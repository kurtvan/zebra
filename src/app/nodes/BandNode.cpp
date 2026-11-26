#include "app/nodes/NodeUi.hpp"

#include "app/WireLook.hpp"
#include "model/nodes/BandNode.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace gv::app::nodes {

namespace {

using model::BandParams;

constexpr float kStripHeight = 22.f;
constexpr const char* kPresetNames[] = {model::kBandPresets[0].name, model::kBandPresets[1].name, model::kBandPresets[2].name};

/// How far along the strip `hz` is, 0..1: by octaves, as the ear hears it.
auto StripX(double hz) -> float
{
    const double at = std::log(hz / model::kBandMinHz) / std::log(model::kBandMaxHz / model::kBandMinHz);
    return static_cast<float>(std::clamp(at, 0.0, 1.0));
}

/// The whole range of hearing, 20 Hz to 20 kHz, with the Band's range lit and written over it.
void DrawStrip(const model::Node& node, NodeContext& context)
{
    const auto& params = model::ParamsOf<BandParams>(node);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = context.width;
    const ImVec2 max(min.x + width, min.y + kStripHeight);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.f);
    // The preset ranges' edges, faintly.
    for (double edge : {model::kBandPresets[1].low, model::kBandPresets[1].high})
    {
        const float x = min.x + StripX(edge) * width;
        draw->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), ImGui::GetColorU32(ImGuiCol_Border));
    }
    ImVec4 lit = kAudioWire;
    lit.w = 0.55f;
    const float left = min.x + StripX(params.low) * width;
    const float right = std::max(left + 2.f, min.x + StripX(params.high) * width);
    draw->AddRectFilled(ImVec2(left, min.y), ImVec2(right, max.y), ImGui::GetColorU32(lit), 3.f);
    const std::string text = model::BandRangeText(params);
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    draw->AddText(ImVec2(min.x + (width - size.x) * 0.5f, min.y + (kStripHeight - size.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), text.c_str());
    ImGui::Dummy(ImVec2(width, kStripHeight));
}

auto RangeSlider(const char* id, double BandParams::*end, const char* format, const char* undo, const char* tooltip) -> Control
{
    Control slider;
    slider.kind = Control::Kind::Slider;
    slider.id = id;
    slider.min = model::kBandMinHz;
    slider.max = model::kBandMaxHz;
    slider.logarithmic = true;
    slider.format = format;
    slider.undo = undo;
    slider.field = id;
    slider.tooltip = tooltip;
    slider.get = [end](const model::Node& node) { return model::ParamsOf<BandParams>(node).*end; };
    // Moving one end past the other pushes it along, so the range never turns inside out.
    slider.set = [end](model::Node& node, double value) {
        auto& params = model::ParamsOf<BandParams>(node);
        params.*end = value;
        if (end == &BandParams::low)
            params.high = std::max(params.high, value);
        else
            params.low = std::min(params.low, value);
    };
    return slider;
}

} // namespace

auto BandUi() -> NodeUi
{
    NodeUi ui;
    ui.type = model::NodeType::Band;
    ui.width = 200.f;
    ui.header = ImVec4(0.16f, 0.42f, 0.22f, 0.60f);
    ui.menuGroup = 4;
    ui.inputLabel = "Audio";

    Control preset;
    preset.kind = Control::Kind::Choice;
    preset.id = "##preset";
    preset.radio = true;
    preset.options = kPresetNames;
    preset.get = [](const model::Node& node) { return static_cast<double>(model::BandPresetOf(model::ParamsOf<BandParams>(node))); };
    preset.set = [](model::Node& node, double value) {
        const auto& chosen = model::kBandPresets[static_cast<size_t>(std::clamp(static_cast<int>(value), 0, 2))];
        model::ParamsOf<BandParams>(node) = {chosen.low, chosen.high};
    };
    preset.undo = "Choose Band Range";
    preset.tooltip = "Bass is below 250 Hz (the kick and bass line), mid is 250 to 4000 Hz (voices, most instruments), treble is above 4000 Hz (hi-hats, cymbals).";

    Control strip;
    strip.kind = Control::Kind::Custom;
    strip.id = "##strip";
    strip.draw = DrawStrip;
    strip.tooltip = "The range it lets through, out of everything you can hear (20 Hz to 20 kHz). Wire it into an Envelope to follow just this range.";

    const Control low = RangeSlider("##low", &BandParams::low, "low %.0f Hz", "Change Band Low End", "Sound below this is cut.");
    const Control high = RangeSlider("##high", &BandParams::high, "high %.0f Hz", "Change Band High End", "Sound above this is cut.");

    ui.controls = {{Control::Kind::Inputs}, preset, strip, low, high};
    ui.addItems = {{"Band", nullptr, "Lets one frequency range of the audio through (the bass, say), for an Envelope to follow."}};
    return ui;
}

} // namespace gv::app::nodes
