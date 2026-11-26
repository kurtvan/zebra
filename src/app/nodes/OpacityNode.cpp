#include "app/nodes/NodeUi.hpp"

namespace gv::app::nodes {

auto OpacityUi() -> NodeUi
{
    NodeUi ui;
    ui.type = model::NodeType::Opacity;
    ui.menuGroup = 1;
    ui.inputLabel = "Video";
    ui.controls = {{Control::Kind::Inputs}, {Control::Kind::Params}};
    ui.addItems = {{"Opacity"}};
    return ui;
}

} // namespace gv::app::nodes
