#include "model/nodes/NodeDef.hpp"
#include "model/nodes/NodeJson.hpp"

namespace gv::model::nodes {

auto SaturationDef() -> NodeDef
{
    NodeDef def = EffectBase(NodeType::Saturation, "saturation", "saturation", "Saturation", SaturationParams{});
    def.checkParams = [](const Node& node, std::vector<GraphError>& errors) {
        CheckRange(errors, node, "saturation", ParamsOf<SaturationParams>(node).saturation, 0.0, 2.0, "0 and 2");
    };
    def.write = [](const Node& node, nlohmann::json& object) { object["saturation"] = ParamsOf<SaturationParams>(node).saturation; };
    def.read = [](const nlohmann::json& object, Node& node, const std::string& path, std::vector<Issue>& issues) {
        ReadField(object, "saturation", ParamsOf<SaturationParams>(node).saturation, path, issues);
    };
    def.pass = [](const Node& node) { return EffectPass{NodeType::Saturation, ParamsOf<SaturationParams>(node).saturation, 0.0f, false}; };
    // From the pixel's grey (0) through itself (1) to twice as colourful (2).
    def.shader = "color = mix(vec3(dot(color, lumaWeights)), color, param.x);";
    return def;
}

} // namespace gv::model::nodes
