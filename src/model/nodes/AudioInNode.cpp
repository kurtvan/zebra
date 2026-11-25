#include "model/nodes/NodeDef.hpp"

namespace gv::model::nodes {

/// The audio coming in: the Setup panel's input, after its gain. An Envelope follows it.
auto AudioInDef() -> NodeDef
{
    NodeDef def;
    def.type = NodeType::AudioIn;
    def.jsonName = "audioIn";
    def.idPrefix = "audio";
    def.displayName = "Audio In";
    def.defaults = AudioInParams{};
    def.output = [](const Node&) -> std::optional<PortKind> { return PortKind::Audio; };
    return def;
}

} // namespace gv::model::nodes
