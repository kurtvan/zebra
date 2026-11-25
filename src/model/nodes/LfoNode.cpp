#include "model/nodes/LfoNode.hpp"
#include "model/nodes/NodeJson.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gv::model::nodes {

namespace {

constexpr const char* kShapeNames[] = {"sine", "triangle", "saw", "square"};

/// The LFO's value at `x` (0..1) into its cycle.
auto Shape(LfoShape shape, double x) -> float
{
    switch (shape)
    {
        case LfoShape::Sine: return static_cast<float>(0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * x));
        case LfoShape::Triangle: return static_cast<float>(x < 0.5 ? 2.0 * x : 2.0 - 2.0 * x);
        case LfoShape::Saw: return static_cast<float>(x);
        case LfoShape::Square: return x < 0.5 ? 1.0f : 0.0f;
    }
    return 0.0f;
}

} // namespace

} // namespace gv::model::nodes

namespace gv::model {

auto LfoPosition(const LfoParams& params, const SignalInputs& inputs) -> double
{
    const double cycles = inputs.beats ? *inputs.beats / params.beats : inputs.seconds / params.periodSeconds;
    return cycles - std::floor(cycles);
}

auto LfoValueAt(const LfoParams& params, double position) -> float
{
    const double x = position + params.phase;
    return nodes::Shape(params.shape, x - std::floor(x));
}

auto LfoCurve(const LfoParams& params, int count) -> std::vector<float>
{
    std::vector<float> curve;
    for (int i = 0; i < count; ++i)
        curve.push_back(LfoValueAt(params, static_cast<double>(i) / count));
    return curve;
}

} // namespace gv::model

namespace gv::model::nodes {

/// A number from 0 to 1 that rises and falls in a loop: in time with the MIDI clock while it runs, else
/// every few seconds.
auto LfoDef() -> NodeDef
{
    NodeDef def;
    def.type = NodeType::Lfo;
    def.jsonName = "lfo";
    def.idPrefix = "lfo";
    def.displayName = "LFO";
    def.defaults = LfoParams{};
    def.output = [](const Node&) -> std::optional<PortKind> { return PortKind::Number; };
    def.checkParams = [](const Node& node, std::vector<GraphError>& errors) {
        const auto& params = ParamsOf<LfoParams>(node);
        if (std::ranges::find(kLfoBeats, params.beats) == std::end(kLfoBeats))
            errors.push_back({node.id, Label(node) + ": a cycle is 1/4, 1/2, 1, 2, 4 or 8 beats."});
        CheckRange(errors, node, "phase", params.phase, 0.0, 1.0, "0 and 1");
        CheckRange(errors, node, "the cycle without a clock", params.periodSeconds, 0.1, 60.0, "0.1 and 60 seconds");
    };
    def.signal = [](const Node& node, const SignalInputs& inputs) {
        const auto& params = ParamsOf<LfoParams>(node);
        return LfoValueAt(params, LfoPosition(params, inputs));
    };
    def.write = [](const Node& node, nlohmann::json& object) {
        const auto& params = ParamsOf<LfoParams>(node);
        object["shape"] = kShapeNames[static_cast<size_t>(params.shape)];
        object["beats"] = params.beats;
        object["phase"] = params.phase;
        object["periodSeconds"] = params.periodSeconds;
    };
    def.read = [](const nlohmann::json& object, Node& node, const std::string& path, std::vector<Issue>& issues) {
        auto& params = ParamsOf<LfoParams>(node);
        std::string shape = kShapeNames[0];
        ReadField(object, "shape", shape, path, issues);
        if (const auto* it = std::ranges::find(kShapeNames, shape); it != std::end(kShapeNames))
            params.shape = static_cast<LfoShape>(it - std::begin(kShapeNames));
        else
            issues.push_back(JsonError(path + "/shape", "unknown shape \"" + shape + "\" (expected sine, triangle, saw or square)"));
        ReadField(object, "beats", params.beats, path, issues);
        ReadField(object, "phase", params.phase, path, issues);
        ReadField(object, "periodSeconds", params.periodSeconds, path, issues);
    };
    return def;
}

} // namespace gv::model::nodes
