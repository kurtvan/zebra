#include "model/nodes/NodeDef.hpp"
#include "model/nodes/NodeJson.hpp"

namespace gv::model::nodes {

namespace {

constexpr ParamDef kParams[] = {
    {.name = "threshold",
     .label = "Threshold",
     .get = [](const Node& node) { return ParamsOf<LumaGateParams>(node).threshold; },
     .set = [](Node& node, float value) { ParamsOf<LumaGateParams>(node).threshold = value; },
     .format = "threshold %.2f",
     .tooltip = "Pixels darker than this are cut (brighter ones when inverted).",
     .undo = "Change Luma Gate"},
    {.name = "softness",
     .label = "Softness",
     .get = [](const Node& node) { return ParamsOf<LumaGateParams>(node).softness; },
     .set = [](Node& node, float value) { ParamsOf<LumaGateParams>(node).softness = value; },
     .format = "softness %.2f",
     .tooltip = "The cut's edge: 0 is hard, higher feathers it.",
     .undo = "Change Softness"},
};

} // namespace

auto LumaGateDef() -> NodeDef
{
    NodeDef def = EffectBase(NodeType::LumaGate, "lumaGate", "gate", "Luma gate", LumaGateParams{});
    def.params = kParams;
    def.checkParams = [](const Node& node, std::vector<GraphError>& errors) {
        const auto& params = ParamsOf<LumaGateParams>(node);
        CheckRange(errors, node, "threshold", params.threshold, 0.0, 1.0, "0 and 1");
        CheckRange(errors, node, "softness", params.softness, 0.0, 1.0, "0 and 1");
    };
    def.write = [](const Node& node, nlohmann::json& object) {
        const auto& params = ParamsOf<LumaGateParams>(node);
        object["threshold"] = params.threshold;
        object["softness"] = params.softness;
        object["invert"] = params.invert;
    };
    def.read = [](const nlohmann::json& object, Node& node, const std::string& path, std::vector<Issue>& issues) {
        auto& params = ParamsOf<LumaGateParams>(node);
        ReadField(object, "threshold", params.threshold, path, issues);
        ReadField(object, "softness", params.softness, path, issues);
        ReadField(object, "invert", params.invert, path, issues);
    };
    def.pass = [](const Node& node) {
        const auto& params = ParamsOf<LumaGateParams>(node);
        return EffectPass{NodeType::LumaGate, params.threshold, params.softness, params.invert};
    };
    // Cuts (makes transparent) pixels darker than the threshold, or brighter ones when inverted; softness
    // feathers the edge over its width, centred on the threshold.
    def.shader = R"GLSL(
            color = clamp(color, 0.0, 1.0);
            float luma = dot(color, lumaWeights);
            float edge = param.y * 0.5;
            float mask = edge > 0.0 ? smoothstep(param.x - edge, param.x + edge, luma) : step(param.x, luma);
            alpha *= flag ? 1.0 - mask : mask;)GLSL";
    return def;
}

} // namespace gv::model::nodes
