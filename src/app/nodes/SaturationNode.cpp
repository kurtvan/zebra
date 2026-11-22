#include "app/nodes/NodeUi.hpp"

namespace gv::app::nodes {

auto SaturationUi() -> NodeUi
{
    NodeUi ui;
    ui.type = model::NodeType::Saturation;
    ui.menuGroup = 1;
    ui.inputLabel = "Video";
    Control saturation;
    saturation.kind = Control::Kind::Slider;
    saturation.id = "##saturation";
    saturation.get = ParamGet<model::SaturationParams, &model::SaturationParams::saturation>;
    saturation.set = ParamSet<model::SaturationParams, &model::SaturationParams::saturation>;
    saturation.min = 0.0;
    saturation.max = 2.0;
    saturation.undo = "Change Saturation";
    saturation.field = "saturation";
    saturation.tooltip = "1.00 leaves the video as it is; 0 is black and white.";
    ui.controls = {{Control::Kind::Inputs}, saturation};
    ui.addItems = {{"Saturation"}};
    return ui;
}

} // namespace gv::app::nodes
