#include "app/nodes/NodeUi.hpp"

namespace gv::app::nodes {

auto ViewportUi() -> NodeUi
{
    NodeUi ui;
    ui.type = model::NodeType::Viewport;
    ui.width = 220.f;
    ui.header = ImVec4(0.16f, 0.42f, 0.32f, 0.60f);
    ui.menuGroup = 3;
    ui.inputLabel = "Picture";
    Control output;
    output.kind = Control::Kind::Text;
    output.text = [](const model::Node&, NodeContext& context) -> std::string {
        const auto& canvas = context.Performance().canvas;
        if (canvas.matchScreen)
            return "Output: the screen's size";
        return "Output: " + std::to_string(canvas.width) + " x " + std::to_string(canvas.height);
    };
    ui.controls = {{Control::Kind::Inputs}, output};
    // Only when the graph has none: there's one per scene, and it can't be deleted.
    ui.addItems = {{"Viewport", nullptr, nullptr, false, [](const model::Graph& graph) { return graph.Viewport() == nullptr; }}};
    return ui;
}

} // namespace gv::app::nodes
