#include "app/nodes/NodeUi.hpp"

#include "model/nodes/NodeDef.hpp"

#include <cmath>
#include <cstdio>

namespace gv::app::nodes {

namespace {

using model::SwitcherParams;

constexpr const char* kUnits[] = {"seconds", "beats", "bars"}; // by IntervalUnit

/// ", 12 s" / ", 3 beats" until the passing input moves on; ", waiting for the MIDI clock".
auto Countdown(const engine::SwitcherState& now) -> std::string
{
    char text[48] = "";
    if (now.waitingForClock)
        std::snprintf(text, sizeof(text), ", waiting for the MIDI clock");
    else if (now.secondsToNext >= 0.0)
        std::snprintf(text, sizeof(text), ", %.0f s", std::ceil(now.secondsToNext));
    else if (now.beatsToNext >= 0.0)
        std::snprintf(text, sizeof(text), ", %.0f %s", std::ceil(now.beatsToNext), std::ceil(now.beatsToNext) == 1.0 ? "beat" : "beats");
    return text;
}

auto CountsSeconds(const model::Node& node) -> bool
{
    return model::ParamsOf<SwitcherParams>(node).unit == model::IntervalUnit::Seconds;
}

} // namespace

auto SwitcherUi() -> NodeUi
{
    NodeUi ui;
    ui.type = model::NodeType::Switcher;
    ui.width = 220.f;
    ui.header = ImVec4(0.50f, 0.37f, 0.16f, 0.60f);
    ui.menuGroup = 2;
    // One row per input, and whether it's free: the last one always is (FollowWires). While the scene is
    // live, the input passing is highlighted with the time until it moves on, and the next one is marked.
    // Double-clicking a wired input switches to it.
    ui.inputs = [](const model::Node& node, NodeContext& context) {
        const auto& params = model::ParamsOf<SwitcherParams>(node);
        const auto now = context.Switcher(node);
        const bool presets = !model::SwitchesVideos(context.SceneGraph(), node);
        std::vector<InputSpec> inputs;
        for (int i = 0; i < params.inputCount; ++i)
        {
            InputSpec spec{i, {}, model::PortKind::Picture, std::to_string(i + 1)};
            if (context.SceneGraph().WireInto(node.id, i) == nullptr)
            {
                spec.label += "  (free)";
            }
            else
            {
                if (now && now->passing == i)
                {
                    spec.passing = true;
                    spec.label += "  passing" + Countdown(*now);
                }
                else if (now && now->next == i)
                {
                    spec.label += "  next";
                }
                if (presets || context.Live())
                    spec.tooltip = presets ? "Double-click to go live on this preset now." : "Double-click to switch to this video now.";
            }
            inputs.push_back(std::move(spec));
        }
        return inputs;
    };
    ui.inputDoubleClicked = [](const model::Node& node, int input, NodeContext& context) {
        if (context.SceneGraph().WireInto(node.id, input) != nullptr)
            context.JumpSwitcher(node, input);
    };

    Control seconds;
    seconds.kind = Control::Kind::Slider;
    seconds.id = "##seconds";
    seconds.label = "Every";
    seconds.get = ParamGet<SwitcherParams, &SwitcherParams::intervalSeconds>;
    seconds.set = ParamSet<SwitcherParams, &SwitcherParams::intervalSeconds>;
    seconds.min = model::kMinPlaylistInterval;
    seconds.max = model::kMaxPlaylistInterval;
    seconds.logarithmic = true;
    seconds.format = "%.3g";
    seconds.undo = "Change Interval";
    seconds.field = "interval";
    seconds.tooltip = "Double-click (or Cmd+click) to type a value.";
    seconds.visible = CountsSeconds;

    Control count = seconds;
    count.id = "##count";
    count.get = ParamGet<SwitcherParams, &SwitcherParams::intervalCount>;
    count.set = ParamSet<SwitcherParams, &SwitcherParams::intervalCount>;
    count.min = 1;
    count.max = model::kMaxPlaylistCount;
    count.integer = true;
    count.format = "%d";
    count.visible = [](const model::Node& node) { return !CountsSeconds(node); };

    Control unit;
    unit.kind = Control::Kind::Choice;
    unit.id = "##unit";
    unit.sameLine = true;
    unit.width = 76.f;
    unit.options = kUnits;
    unit.get = [](const model::Node& node) { return static_cast<double>(model::ParamsOf<SwitcherParams>(node).unit); };
    unit.set = [](model::Node& node, double value) { model::ParamsOf<SwitcherParams>(node).unit = static_cast<model::IntervalUnit>(value); };
    unit.undo = "Change Interval Unit";
    unit.tooltip = "Beats and bars follow the MIDI clock.";

    Control shuffle;
    shuffle.kind = Control::Kind::Checkbox;
    shuffle.id = "##shuffle";
    shuffle.label = "Shuffle";
    shuffle.get = ParamGet<SwitcherParams, &SwitcherParams::shuffle>;
    shuffle.set = ParamSet<SwitcherParams, &SwitcherParams::shuffle>;
    shuffle.undo = "Shuffle";
    shuffle.undoOff = "Play in Order";
    shuffle.tooltip = "Off: top to bottom. On: random order, never the same input twice in a row.";

    ui.controls = {seconds, count, unit, shuffle, {Control::Kind::Inputs}};
    ui.addItems = {{"Switcher", nullptr, "Switches videos or Milkdrop presets: whatever you wire into it."}};
    return ui;
}

} // namespace gv::app::nodes
