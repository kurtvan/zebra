#include "model/nodes/NodeDef.hpp"
#include "model/nodes/NodeJson.hpp"

namespace gv::model::nodes {

namespace {

constexpr ParamDef kParams[] = {
    {.name = "contrast",
     .label = "Contrast",
     .low = 0.0f,
     .high = kMaxContrast,
     .get = [](const Node& node) { return ParamsOf<ContrastParams>(node).contrast; },
     .set = [](Node& node, float value) { ParamsOf<ContrastParams>(node).contrast = value; },
     .formatFor = [](double value) { return value < 10.0 ? "%.2f" : "%.0f"; },
     .logarithmic = true,
     .tooltip = "1.00 leaves the video as it is. All the way up, with Saturation at 0, gives hard black and white.\n"
                "Double-click (or Cmd+click) to type a value.",
     .undo = "Change Contrast"},
};

} // namespace

auto ContrastDef() -> NodeDef
{
    NodeDef def = EffectBase(NodeType::Contrast, "contrast", "contrast", "Contrast", ContrastParams{});
    def.params = kParams;
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
