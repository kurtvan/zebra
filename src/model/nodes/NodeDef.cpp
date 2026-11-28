#include "model/nodes/NodeDef.hpp"

#include <algorithm>

#include <array>
#include <cstdlib>
#include <functional>
#include <set>
#include <string>

namespace gv::model {

namespace {

auto BuildDefs() -> std::array<NodeDef, kNodeTypeCount>
{
    std::array<NodeDef, kNodeTypeCount> defs = {
        nodes::VideoSourceDef(), nodes::ContrastDef(), nodes::SaturationDef(), nodes::LumaGateDef(),
        nodes::OpacityDef(),         nodes::MilkdropPresetDef(),   nodes::SwitcherDef(),   nodes::ViewportDef(),
        nodes::IsfShaderDef(),  nodes::LfoDef(),          nodes::AudioInDef(),    nodes::EnvelopeDef(),
        nodes::BandDef(),
    };
    // Def() indexes by type, so each definition must sit at its own type's index.
    for (int i = 0; i < kNodeTypeCount; ++i)
    {
        if (static_cast<int>(defs[static_cast<size_t>(i)].type) != i)
            std::abort();
    }
    return defs;
}

auto Defs() -> const std::array<NodeDef, kNodeTypeCount>&
{
    static const auto defs = BuildDefs();
    return defs;
}

} // namespace

auto Def(NodeType type) -> const NodeDef&
{
    return Defs()[static_cast<size_t>(type)];
}

auto NodeDefs() -> std::span<const NodeDef>
{
    return Defs();
}

auto NodeTypeFromJson(std::string_view name) -> std::optional<NodeType>
{
    for (const auto& def : Defs())
    {
        if (name == def.jsonName || std::ranges::find(def.formerJsonNames, name) != def.formerJsonNames.end())
            return def.type;
    }
    return std::nullopt;
}

auto Label(const Node& node) -> std::string
{
    const NodeDef& def = Def(node.type);
    return def.label != nullptr ? def.label(node) : def.displayName;
}

void CheckRange(std::vector<GraphError>& errors, const Node& node, const char* what, double value, double low, double high, const char* range)
{
    if (!(value >= low && value <= high))
        errors.push_back({node.id, Label(node) + ": " + what + " must be between " + range + "."});
}

auto ChainStart(const Graph& graph, const Node* node) -> const Node*
{
    for (int steps = 0; node != nullptr && steps <= static_cast<int>(graph.nodes.size()); ++steps)
    {
        if (!IsEffect(node->type) && node->type != NodeType::IsfShader)
            return node;
        node = graph.Upstream(node->id, 0);
    }
    return nullptr;
}

auto ParamsFor(const Node& node) -> std::span<const ParamDef>
{
    const NodeDef& def = Def(node.type);
    return def.paramsFor != nullptr ? def.paramsFor(node) : def.params;
}

auto ParamIndex(const Node& node, std::string_view name) -> int
{
    const auto params = ParamsFor(node);
    const auto it = std::ranges::find_if(params, [&](const ParamDef& param) { return name == param.name; });
    return it != params.end() ? static_cast<int>(it - params.begin()) : -1;
}

auto WiredParams(const Graph& graph, const Node& node) -> std::vector<std::pair<int, std::string>>
{
    std::vector<std::pair<int, std::string>> wired;
    for (int param = 0; param < static_cast<int>(ParamsFor(node).size()); ++param)
    {
        if (const Node* from = graph.Upstream(node.id, ParamInput(node, param)); from != nullptr)
            wired.emplace_back(param, from->id);
    }
    return wired;
}

auto EvaluateSignals(const Graph& graph, const SignalInputs& inputs) -> SignalValues
{
    SignalValues values;
    std::set<std::string> started; // evaluated or being evaluated: a loop (ValidateGraph refuses those) stops here
    std::function<void(const Node&)> evaluate = [&](const Node& node) {
        const auto signal = Def(node.type).signal;
        if (signal == nullptr || node.params.index() != Def(node.type).defaults.index() || !started.insert(node.id).second)
            return;
        const auto wired = WiredParams(graph, node);
        for (const auto& [param, from] : wired) // the Number nodes moving this one's settings go first
        {
            if (const Node* upstream = graph.Find(from))
                evaluate(*upstream);
        }
        values[node.id] = signal(ModulatedNode(node, wired, values), inputs);
    };
    for (const auto& node : graph.nodes)
        evaluate(node);
    return values;
}

auto DepthOf(const Node& node, int param) -> float
{
    const auto it = node.depths.find(ParamsFor(node)[static_cast<size_t>(param)].name);
    return it != node.depths.end() ? it->second : 1.0f;
}

auto Modulate(const Node& node, int param, float signal) -> float
{
    const ParamDef& def = ParamsFor(node)[static_cast<size_t>(param)];
    const float value = def.get(node, def) + (signal - 0.5f) * DepthOf(node, param) * (def.high - def.low);
    return std::clamp(value, def.low, def.high);
}

auto ModulatedNode(const Node& node, std::span<const std::pair<int, std::string>> wired, const SignalValues& signals) -> Node
{
    Node modulated = node;
    const auto params = ParamsFor(node);
    for (const auto& [param, from] : wired)
    {
        if (const auto it = signals.find(from); it != signals.end())
        {
            const ParamDef& def = params[static_cast<size_t>(param)];
            def.set(modulated, def, Modulate(node, param, it->second));
        }
    }
    return modulated;
}

auto ModulatedNode(const Graph& graph, const Node& node, const SignalValues& signals) -> Node
{
    return ModulatedNode(node, WiredParams(graph, node), signals);
}

namespace nodes {

auto EffectBase(NodeType type, const char* jsonName, const char* idPrefix, const char* displayName, NodeParams defaults) -> NodeDef
{
    NodeDef def;
    def.type = type;
    def.jsonName = jsonName;
    def.idPrefix = idPrefix;
    def.displayName = displayName;
    def.defaults = std::move(defaults);
    def.effect = true;
    def.output = [](const Node&) -> std::optional<PortKind> { return PortKind::Picture; };
    def.inputCount = [](const Node&) { return 1; };
    def.inputKind = [](const Node&, int) { return PortKind::Picture; };
    return def;
}

} // namespace nodes

} // namespace gv::model
