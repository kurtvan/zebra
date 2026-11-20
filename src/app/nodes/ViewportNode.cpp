#include "app/nodes/NodeDraw.hpp"

#include "app/Widgets.hpp"

namespace gv::app {

void DrawViewportBody(const model::Node& node, NodeContext& context)
{
    for (const auto& spec : NodeInputs(node, context))
        context.InputRow(node, spec);
    const auto& canvas = context.performance.canvas;
    if (canvas.matchScreen)
        ImGui::TextColored(kDim, "Output: the screen's size");
    else
        ImGui::TextColored(kDim, "Output: %d x %d", canvas.width, canvas.height);
}

} // namespace gv::app
