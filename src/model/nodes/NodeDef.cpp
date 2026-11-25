#include "model/nodes/NodeDef.hpp"

#include <algorithm>

#include <array>
#include <cstdlib>
#include <string>

namespace gv::model {

namespace {

auto BuildDefs() -> std::array<NodeDef, kNodeTypeCount>
{
    std::array<NodeDef, kNodeTypeCount> defs = {
        nodes::VideoSourceDef(), nodes::ContrastDef(), nodes::SaturationDef(), nodes::LumaGateDef(),
        nodes::OpacityDef(),         nodes::MilkdropPresetDef(),   nodes::SwitcherDef(),   nodes::ViewportDef(),
        nodes::IsfShaderDef(),  nodes::LfoDef(),
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

auto EvaluateSignals(const Graph& graph, const SignalInputs& inputs) -> SignalValues
{
    SignalValues values;
    for (const auto& node : graph.nodes)
    {
        if (const auto signal = Def(node.type).signal; signal != nullptr && node.params.index() == Def(node.type).defaults.index())
            values[node.id] = signal(node, inputs);
    }
    return values;
}

auto DepthOf(const Node& node, int param) -> float
{
    const auto it = node.depths.find(Def(node.type).params[static_cast<size_t>(param)].name);
    return it != node.depths.end() ? it->second : 1.0f;
}

auto Modulate(const Node& node, int param, float signal) -> float
{
    const ParamDef& def = Def(node.type).params[static_cast<size_t>(param)];
    const float value = def.get(node) + (signal - 0.5f) * DepthOf(node, param) * (def.high - def.low);
    return std::clamp(value, def.low, def.high);
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
