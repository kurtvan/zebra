#include "app/nodes/NodeUi.hpp"

namespace gv::app::nodes {

auto AudioInUi() -> NodeUi
{
    NodeUi ui;
    ui.type = model::NodeType::AudioIn;
    ui.width = 160.f;
    ui.header = ImVec4(0.16f, 0.42f, 0.22f, 0.60f);
    ui.menuGroup = 4;
    Control about;
    about.kind = Control::Kind::Text;
    about.text = [](const model::Node&, NodeContext&) -> std::string { return "The Setup panel's input"; };
    ui.controls = {about};
    ui.addItems = {{"Audio In", nullptr, "The music coming in (the Setup panel's input, after its gain), for an Envelope to follow."}};
    return ui;
}

} // namespace gv::app::nodes
