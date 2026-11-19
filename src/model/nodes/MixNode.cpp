#include "model/nodes/NodeDef.hpp"
#include "model/nodes/NodeJson.hpp"

namespace gv::model::nodes {

auto MixDef() -> NodeDef
{
    NodeDef def = EffectBase(NodeType::Mix, "mix", "mix", "Mix", MixParams{});
    def.checkParams = [](const Node& node, std::vector<GraphError>& errors) {
        CheckRange(errors, node, "opacity", ParamsOf<MixParams>(node).opacity, 0.0, 1.0, "0 and 1");
    };
    def.write = [](const Node& node, nlohmann::json& object) { object["opacity"] = ParamsOf<MixParams>(node).opacity; };
    def.read = [](const nlohmann::json& object, Node& node, const std::string& path, std::vector<Issue>& issues) {
        ReadField(object, "opacity", ParamsOf<MixParams>(node).opacity, path, issues);
    };
    def.pass = [](const Node& node) { return EffectPass{NodeType::Mix, ParamsOf<MixParams>(node).opacity, 0.0f, false}; };
    // How strongly the video goes into the preset input: the branch's alpha.
    def.shader = "alpha *= param.x;";
    return def;
}

} // namespace gv::model::nodes
