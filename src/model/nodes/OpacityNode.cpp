#include "model/nodes/NodeDef.hpp"
#include "model/nodes/NodeJson.hpp"

namespace gv::model::nodes {

namespace {

constexpr const char* kFormerJsonNames[] = {"mix"};

} // namespace

/// How strongly a video goes into the preset input it's wired to.
auto OpacityDef() -> NodeDef
{
    NodeDef def = EffectBase(NodeType::Opacity, "opacity", "opacity", "Opacity", OpacityParams{});
    def.formerJsonNames = kFormerJsonNames;
    def.checkParams = [](const Node& node, std::vector<GraphError>& errors) {
        CheckRange(errors, node, "opacity", ParamsOf<OpacityParams>(node).opacity, 0.0, 1.0, "0 and 1");
    };
    def.write = [](const Node& node, nlohmann::json& object) { object["opacity"] = ParamsOf<OpacityParams>(node).opacity; };
    def.read = [](const nlohmann::json& object, Node& node, const std::string& path, std::vector<Issue>& issues) {
        ReadField(object, "opacity", ParamsOf<OpacityParams>(node).opacity, path, issues);
    };
    def.pass = [](const Node& node) { return EffectPass{NodeType::Opacity, ParamsOf<OpacityParams>(node).opacity, 0.0f, false}; };
    // How strongly the video goes into the preset input: the branch's alpha.
    def.shader = "alpha *= param.x;";
    return def;
}

} // namespace gv::model::nodes
