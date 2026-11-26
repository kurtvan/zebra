#include "app/nodes/NodeUi.hpp"

namespace gv::app::nodes {

auto ContrastUi() -> NodeUi
{
    NodeUi ui;
    ui.type = model::NodeType::Contrast;
    ui.menuGroup = 1;
    ui.inputLabel = "Video";
    ui.controls = {{Control::Kind::Inputs}, {Control::Kind::Params}};
    ui.addItems = {{"Contrast"}};
    return ui;
}

} // namespace gv::app::nodes
