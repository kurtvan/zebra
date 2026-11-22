#include "app/nodes/NodeUi.hpp"

namespace gv::app::nodes {

auto LumaGateUi() -> NodeUi
{
    using model::LumaGateParams;
    NodeUi ui;
    ui.type = model::NodeType::LumaGate;
    ui.menuGroup = 1;
    ui.inputLabel = "Video";

    Control threshold;
    threshold.kind = Control::Kind::Slider;
    threshold.id = "##threshold";
    threshold.get = ParamGet<LumaGateParams, &LumaGateParams::threshold>;
    threshold.set = ParamSet<LumaGateParams, &LumaGateParams::threshold>;
    threshold.format = "threshold %.2f";
    threshold.undo = "Change Luma Gate";
    threshold.field = "threshold";
    threshold.tooltip = "Pixels darker than this are cut (brighter ones when inverted).";

    Control softness = threshold;
    softness.id = "##softness";
    softness.get = ParamGet<LumaGateParams, &LumaGateParams::softness>;
    softness.set = ParamSet<LumaGateParams, &LumaGateParams::softness>;
    softness.format = "softness %.2f";
    softness.undo = "Change Softness";
    softness.field = "softness";
    softness.tooltip = "The cut's edge: 0 is hard, higher feathers it.";

    Control invert;
    invert.kind = Control::Kind::Checkbox;
    invert.id = "##invert";
    invert.label = "Invert";
    invert.get = ParamGet<LumaGateParams, &LumaGateParams::invert>;
    invert.set = ParamSet<LumaGateParams, &LumaGateParams::invert>;
    invert.undo = "Invert Luma Gate";
    invert.undoOff = "Uninvert Luma Gate";
    invert.tooltip = "Keeps the video's dark parts instead of its bright ones.";

    ui.controls = {{Control::Kind::Inputs}, threshold, softness, invert};
    ui.addItems = {{"Luma gate"}};
    return ui;
}

} // namespace gv::app::nodes
