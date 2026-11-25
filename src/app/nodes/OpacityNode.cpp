#include "app/nodes/NodeUi.hpp"

namespace gv::app::nodes {

auto OpacityUi() -> NodeUi
{
    NodeUi ui;
    ui.type = model::NodeType::Opacity;
    ui.menuGroup = 1;
    ui.inputLabel = "Video";
    Control opacity;
    opacity.kind = Control::Kind::Slider;
    opacity.id = "##opacity";
    opacity.get = ParamGet<model::OpacityParams, &model::OpacityParams::opacity>;
    opacity.set = ParamSet<model::OpacityParams, &model::OpacityParams::opacity>;
    opacity.format = "opacity %.2f";
    opacity.undo = "Change Opacity";
    opacity.field = "opacity";
    opacity.param = 0;
    opacity.tooltip = "How strongly the video goes into the input it's wired to.";
    ui.controls = {{Control::Kind::Inputs}, opacity};
    ui.addItems = {{"Opacity"}};
    return ui;
}

} // namespace gv::app::nodes
