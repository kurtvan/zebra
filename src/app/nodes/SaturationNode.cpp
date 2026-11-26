#include "app/nodes/NodeUi.hpp"

namespace gv::app::nodes {

auto SaturationUi() -> NodeUi
{
    NodeUi ui;
    ui.type = model::NodeType::Saturation;
    ui.menuGroup = 1;
    ui.inputLabel = "Video";
    ui.controls = {{Control::Kind::Inputs}, {Control::Kind::Params}};
    ui.addItems = {{"Saturation"}};
    return ui;
}

} // namespace gv::app::nodes
