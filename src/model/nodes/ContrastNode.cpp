#include "model/nodes/NodeDef.hpp"
#include "model/nodes/NodeJson.hpp"

namespace gv::model::nodes {

auto ContrastDef() -> NodeDef
{
    NodeDef def = EffectBase(NodeType::Contrast, "contrast", "contrast", "Contrast", ContrastParams{});
    def.checkParams = [](const Node& node, std::vector<GraphError>& errors) {
        CheckRange(errors, node, "contrast", ParamsOf<ContrastParams>(node).contrast, 0.0, kMaxContrast, "0 and 100");
    };
    def.write = [](const Node& node, nlohmann::json& object) { object["contrast"] = ParamsOf<ContrastParams>(node).contrast; };
    def.read = [](const nlohmann::json& object, Node& node, const std::string& path, std::vector<Issue>& issues) {
        ReadField(object, "contrast", ParamsOf<ContrastParams>(node).contrast, path, issues);
    };
    def.pass = [](const Node& node) { return EffectPass{NodeType::Contrast, ParamsOf<ContrastParams>(node).contrast, 0.0f, false}; };
    // Around mid-grey; colour passes don't clamp, so a later pass sees the full range.
    def.shader = "color = (color - 0.5) * param.x + 0.5;";
    return def;
}

} // namespace gv::model::nodes
