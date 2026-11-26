#include "app/nodes/NodeUi.hpp"

namespace gv::app::nodes {

auto LumaGateUi() -> NodeUi
{
    using model::LumaGateParams;
    NodeUi ui;
    ui.type = model::NodeType::LumaGate;
    ui.menuGroup = 1;
    ui.inputLabel = "Video";

    Control invert;
    invert.kind = Control::Kind::Checkbox;
    invert.id = "##invert";
    invert.label = "Invert";
    invert.get = ParamGet<LumaGateParams, &LumaGateParams::invert>;
    invert.set = ParamSet<LumaGateParams, &LumaGateParams::invert>;
    invert.undo = "Invert Luma Gate";
    invert.undoOff = "Uninvert Luma Gate";
    invert.tooltip = "Keeps the video's dark parts instead of its bright ones.";

    ui.controls = {{Control::Kind::Inputs}, {Control::Kind::Params}, invert};
    ui.addItems = {{"Luma gate"}};
    return ui;
}

} // namespace gv::app::nodes
