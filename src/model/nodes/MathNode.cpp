#include "model/nodes/MathNode.hpp"
#include "model/nodes/NodeDef.hpp"
#include "model/nodes/NodeJson.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <span>

namespace gv::model {

namespace {

constexpr const char* kOpNames[] = {"Sum", "Difference", "Multiply", "Divide", "Log", "Absolute"};    // by MathOp
constexpr const char* kOpJson[] = {"sum", "difference", "multiply", "divide", "log", "absolute"};     // by MathOp
constexpr float kInputLimit = 10.0f; //!< An unwired input's number: -10..10.

auto Grows(MathOp op) -> bool
{
    return op == MathOp::Sum || op == MathOp::Multiply;
}

/// The value of input `index`, or the identity if the node has no such input.
auto ValueAt(const MathParams& params, size_t index) -> float
{
    return index < params.values.size() ? params.values[index] : MathIdentity(params.op);
}

/// Input "a" is values[0], "b" values[1]... Other params (Log's curve): -1.
auto InputIndex(std::string_view name) -> int
{
    return name.size() == 1 && name[0] >= 'a' && name[0] < 'a' + kMaxMathInputs ? name[0] - 'a' : -1;
}

constexpr auto Input(const char* name, const char* label, const char* format) -> ParamDef
{
    return {.name = name,
            .label = label,
            .low = -kInputLimit,
            .high = kInputLimit,
            .get = [](const Node& node, const ParamDef& def) { return ValueAt(ParamsOf<MathParams>(node), static_cast<size_t>(InputIndex(def.name))); },
            .set =
                [](Node& node, const ParamDef& def, float value) {
                    auto& values = ParamsOf<MathParams>(node).values;
                    const auto index = static_cast<size_t>(InputIndex(def.name));
                    if (index >= values.size())
                        values.resize(index + 1, MathIdentity(ParamsOf<MathParams>(node).op));
                    values[index] = value;
                },
            .format = format,
            .tooltip = "Wire a number in, or set one here to use while nothing is wired.",
            .undo = "Change Math Input",
            .passThrough = true};
}

constexpr ParamDef kLettered[] = {
    Input("a", "a", "a %.2f"), Input("b", "b", "b %.2f"), Input("c", "c", "c %.2f"), Input("d", "d", "d %.2f"),
    Input("e", "e", "e %.2f"), Input("f", "f", "f %.2f"), Input("g", "g", "g %.2f"), Input("h", "h", "h %.2f"),
};
static_assert(std::size(kLettered) == kMaxMathInputs);

constexpr ParamDef kLog[] = {
    Input("a", "Input", "in %.2f"),
    {.name = "curve",
     .label = "Curve",
     .get = [](const Node& node, const ParamDef&) { return ParamsOf<MathParams>(node).curve; },
     .set = [](Node& node, const ParamDef&, float value) { ParamsOf<MathParams>(node).curve = value; },
     .format = "curve %.2f",
     .tooltip = "How strongly the curve bends: 0 is a straight line, 1 lifts the low end most and softens the top.",
     .undo = "Change Log Curve"},
};

auto ParamsOfOp(const Node& node) -> std::span<const ParamDef>
{
    const auto* params = std::get_if<MathParams>(&node.params);
    if (params == nullptr)
        return {};
    switch (params->op)
    {
        case MathOp::Log: return kLog;
        case MathOp::Absolute: return std::span(kLog).first(1);
        default: break;
    }
    return std::span(kLettered).first(std::clamp(params->values.size(), size_t{1}, std::size(kLettered)));
}

/// `value` as a float, if it is a finite one.
auto Finite(double value) -> std::optional<float>
{
    if (!std::isfinite(value) || std::abs(value) > static_cast<double>(std::numeric_limits<float>::max()))
        return std::nullopt;
    return static_cast<float>(value);
}

} // namespace

auto MathOpName(MathOp op) -> const char*
{
    return kOpNames[static_cast<size_t>(op)];
}

auto MathIdentity(MathOp op) -> float
{
    return op == MathOp::Multiply || op == MathOp::Divide ? 1.0f : 0.0f;
}

auto MathInputCount(MathOp op, int lastWired) -> int
{
    if (Grows(op))
        return std::clamp(lastWired + 2, 2, kMaxMathInputs);
    return op == MathOp::Difference || op == MathOp::Divide ? 2 : 1;
}

auto MathDefaults(MathOp op) -> MathParams
{
    MathParams params;
    params.op = op;
    params.values.assign(static_cast<size_t>(MathInputCount(op, -1)), MathIdentity(op));
    return params;
}

auto LogCurve(float x, float curve) -> float
{
    const double clamped = std::clamp(static_cast<double>(x), 0.0, 1.0);
    // log(1 + k x) / log(1 + k): k = 0 is the straight line, k = 999 bends hard (x = 0.1 is already 0.67).
    const double k = std::pow(1000.0, std::clamp(static_cast<double>(curve), 0.0, 1.0)) - 1.0;
    if (k < 1e-6)
        return static_cast<float>(clamped);
    return static_cast<float>(std::log1p(k * clamped) / std::log1p(k));
}

auto MathAnswerOf(const MathParams& params) -> MathAnswer
{
    if (std::ranges::any_of(params.values, [](float value) { return !std::isfinite(value); }))
        return {std::nullopt, "An input isn't a number: holding the last value."};
    const double a = ValueAt(params, 0);
    const double b = ValueAt(params, 1);
    std::optional<float> value;
    switch (params.op)
    {
        case MathOp::Sum:
        {
            double sum = 0.0;
            for (float v : params.values)
                sum += v;
            value = Finite(sum);
            break;
        }
        case MathOp::Multiply:
        {
            double product = 1.0;
            for (float v : params.values)
                product *= v;
            value = Finite(product);
            break;
        }
        case MathOp::Difference: value = Finite(a - b); break;
        case MathOp::Divide:
            if (b == 0.0)
                return {std::nullopt, "Dividing by zero: holding the last value."};
            value = Finite(a / b);
            break;
        case MathOp::Log:
            if (a < 0.0)
                return {std::nullopt, "Below zero: holding the last value."};
            value = LogCurve(static_cast<float>(a), params.curve);
            break;
        case MathOp::Absolute: value = Finite(std::abs(a)); break;
    }
    if (!value)
        return {std::nullopt, "The answer is too big: holding the last value."};
    return {value, nullptr};
}

} // namespace gv::model

namespace gv::model::nodes {

/// A number worked out from the numbers wired into it (or set on its inputs): their sum, difference,
/// product or quotient, a log curve of one, or its absolute value. Its answer isn't clamped; the setting it
/// moves clamps it. While there's no answer (dividing by zero), it holds its last.
auto MathDef() -> NodeDef
{
    NodeDef def;
    def.type = NodeType::Math;
    def.jsonName = "math";
    def.idPrefix = "math";
    def.displayName = "Math";
    def.defaults = MathParams{};
    def.output = [](const Node&) -> std::optional<PortKind> { return PortKind::Number; };
    def.paramsFor = ParamsOfOp;
    def.label = [](const Node& node) -> std::string {
        const auto* params = std::get_if<MathParams>(&node.params);
        return params != nullptr ? MathOpName(params->op) : "Math";
    };
    def.followWires = [](const Graph& graph, Node& node) {
        auto* params = std::get_if<MathParams>(&node.params);
        if (params == nullptr)
            return; // wrong settings: ValidateGraph reports it
        int last = -1;
        for (const auto& wire : graph.wires)
        {
            if (wire.toNode == node.id)
                last = std::max(last, InputIndex(wire.param));
        }
        params->values.resize(static_cast<size_t>(MathInputCount(params->op, last)), MathIdentity(params->op));
    };
    def.checkParams = [](const Node& node, std::vector<GraphError>& errors) {
        const auto& params = ParamsOf<MathParams>(node);
        if (params.values.size() != static_cast<size_t>(MathInputCount(params.op, static_cast<int>(params.values.size()) - 2)))
            errors.push_back({node.id, Label(node) + ": it has the wrong number of inputs."});
        for (float value : params.values)
            CheckRange(errors, node, "an input's number", value, -kInputLimit, kInputLimit, "-10 and 10");
        CheckRange(errors, node, "the curve", params.curve, 0.0, 1.0, "0 and 1");
    };
    def.signal = [](const Node& node, const SignalInputs& inputs) {
        if (const auto value = MathAnswerOf(ParamsOf<MathParams>(node)).value)
            return *value;
        const auto it = inputs.previous.find(node.id);
        return it != inputs.previous.end() && std::isfinite(it->second) ? it->second : 0.0f;
    };
    def.write = [](const Node& node, nlohmann::json& object) {
        const auto& params = ParamsOf<MathParams>(node);
        object["op"] = kOpJson[static_cast<size_t>(params.op)];
        object["values"] = params.values;
        if (params.op == MathOp::Log)
            object["curve"] = params.curve;
    };
    def.read = [](const nlohmann::json& object, Node& node, const std::string& path, std::vector<Issue>& issues) {
        std::string op = kOpJson[0];
        ReadField(object, "op", op, path, issues);
        auto& params = ParamsOf<MathParams>(node);
        if (const auto* it = std::ranges::find(kOpJson, op); it != std::end(kOpJson))
            params = MathDefaults(static_cast<MathOp>(it - std::begin(kOpJson)));
        else
            issues.push_back(JsonError(path + "/op", "unknown operation \"" + op + "\" (expected sum, difference, multiply, divide, log or absolute)"));
        ReadField(object, "values", params.values, path, issues);
        ReadField(object, "curve", params.curve, path, issues);
    };
    return def;
}

} // namespace gv::model::nodes
