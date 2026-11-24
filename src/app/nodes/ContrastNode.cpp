#include "app/nodes/NodeUi.hpp"

namespace gv::app::nodes {

auto ContrastUi() -> NodeUi
{
    NodeUi ui;
    ui.type = model::NodeType::Contrast;
    ui.menuGroup = 1;
    ui.inputLabel = "Video";
    Control contrast;
    contrast.kind = Control::Kind::Slider;
    contrast.id = "##contrast";
    contrast.get = ParamGet<model::ContrastParams, &model::ContrastParams::contrast>;
    contrast.set = ParamSet<model::ContrastParams, &model::ContrastParams::contrast>;
    contrast.min = 0.0;
    contrast.max = model::kMaxContrast;
    contrast.logarithmic = true;
    contrast.formatFor = [](double value) { return value < 10.0 ? "%.2f" : "%.0f"; };
    contrast.undo = "Change Contrast";
    contrast.field = "contrast";
    contrast.tooltip = "1.00 leaves the video as it is. All the way up, with Saturation at 0, gives hard black and white.\n"
                       "Double-click (or Cmd+click) to type a value.";
    ui.controls = {{Control::Kind::Inputs}, contrast};
    ui.addItems = {{"Contrast"}};
    return ui;
}

} // namespace gv::app::nodes
