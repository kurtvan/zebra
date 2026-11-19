#include "app/nodes/NodeUi.hpp"

namespace gv::app::nodes {

auto MixUi() -> NodeUi
{
    NodeUi ui;
    ui.type = model::NodeType::Mix;
    ui.menuGroup = 1;
    ui.inputLabel = "Video";
    Control opacity;
    opacity.kind = Control::Kind::Slider;
    opacity.id = "##opacity";
    opacity.get = ParamGet<model::MixParams, &model::MixParams::opacity>;
    opacity.set = ParamSet<model::MixParams, &model::MixParams::opacity>;
    opacity.format = "opacity %.2f";
    opacity.undo = "Change Mix";
    opacity.field = "opacity";
    opacity.tooltip = "How strongly the video goes into the input it's wired to.";
    ui.controls = {{Control::Kind::Inputs}, opacity};
    ui.addItems = {{"Mix"}};
    return ui;
}

} // namespace gv::app::nodes
