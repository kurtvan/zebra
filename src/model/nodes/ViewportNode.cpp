#include "model/nodes/NodeDef.hpp"

namespace gv::model::nodes {

/// What the scene shows: one picture input, no output. Every graph has exactly one, with the id "viewport".
auto ViewportDef() -> NodeDef
{
    NodeDef def;
    def.type = NodeType::Viewport;
    def.jsonName = "viewport";
    def.idPrefix = "viewport";
    def.displayName = "Viewport";
    def.defaults = ViewportParams{};
    def.inputCount = [](const Node&) { return 1; };
    def.inputKind = [](const Node&, int) { return PortKind::Frame; };
    return def;
}

} // namespace gv::model::nodes
