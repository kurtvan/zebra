#include "app/nodes/NodeUi.hpp"

#include "model/nodes/NodeDef.hpp"

namespace gv::app::nodes {

namespace {

using model::SwitcherParams;

constexpr const char* kUnits[] = {"seconds", "beats", "bars"}; // by IntervalUnit

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
    // One row per input, and whether it's free: the last one always is (NormalizeSwitcherInputs).
    ui.inputs = [](const model::Node& node, NodeContext& context) {
        const auto& params = model::ParamsOf<SwitcherParams>(node);
        std::vector<InputSpec> inputs;
        for (int i = 0; i < params.inputCount; ++i)
        {
            const bool wired = context.SceneGraph().WireInto(node.id, i) != nullptr;
            inputs.push_back({i, {}, params.kind, std::to_string(i + 1) + (wired ? "" : "  (free)")});
        }
        return inputs;
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
    ui.addItems = {
        {"Preset switcher"},
        {"Video switcher", [](model::Node& node) { model::ParamsOf<SwitcherParams>(node).kind = model::PortKind::Video; }},
    };
    return ui;
}

} // namespace gv::app::nodes
