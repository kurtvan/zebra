#include "model/Graph.hpp"

#include "model/nodes/NodeDef.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <tuple>

namespace gv::model {

auto Graph::operator==(const Graph& other) const -> bool
{
    if (nodes != other.nodes || wires.size() != other.wires.size())
        return false;
    auto sorted = [](std::vector<Wire> list) {
        std::sort(list.begin(), list.end(), [](const Wire& a, const Wire& b) {
            return std::tie(a.toNode, a.toInput, a.textureName, a.fromNode) < std::tie(b.toNode, b.toInput, b.textureName, b.fromNode);
        });
        return list;
    };
    return sorted(wires) == sorted(other.wires);
}

auto Graph::Find(const std::string& id) const -> const Node*
{
    for (const auto& node : nodes)
    {
        if (node.id == id)
            return &node;
    }
    return nullptr;
}

auto Graph::Find(const std::string& id) -> Node*
{
    for (auto& node : nodes)
    {
        if (node.id == id)
            return &node;
    }
    return nullptr;
}

auto Graph::Viewport() const -> const Node*
{
    for (const auto& node : nodes)
    {
        if (node.type == NodeType::Viewport)
            return &node;
    }
    return nullptr;
}

auto Graph::WireInto(const std::string& node, int input, const std::string& textureName) const -> const Wire*
{
    for (const auto& wire : wires)
    {
        if (wire.toNode == node && wire.toInput == input && wire.textureName == textureName)
            return &wire;
    }
    return nullptr;
}

auto Graph::Upstream(const std::string& node, int input, const std::string& textureName) const -> const Node*
{
    const Wire* wire = WireInto(node, input, textureName);
    return wire != nullptr ? Find(wire->fromNode) : nullptr;
}

auto DefaultParams(NodeType type) -> NodeParams
{
    return Def(type).defaults;
}

auto IdPrefix(NodeType type) -> const char*
{
    return Def(type).idPrefix;
}

auto DisplayName(NodeType type) -> const char*
{
    return Def(type).displayName;
}

auto IsEffect(NodeType type) -> bool
{
    return Def(type).effect;
}

auto OutputKind(const Node& node) -> std::optional<PortKind>
{
    const NodeDef& def = Def(node.type);
    return def.output != nullptr ? def.output(node) : std::nullopt;
}

auto InputCount(const Node& node) -> int
{
    const NodeDef& def = Def(node.type);
    return def.inputCount != nullptr ? def.inputCount(node) : 0;
}

auto InputKind(const Node& node, int input) -> std::optional<PortKind>
{
    if (input < 0 || input >= InputCount(node))
        return std::nullopt;
    return Def(node.type).inputKind(node, input);
}

namespace {

auto KindName(PortKind kind) -> const char*
{
    return kind == PortKind::Video ? "video" : "a preset's picture";
}

} // namespace

auto ValidateGraph(const Graph& graph) -> std::vector<GraphError>
{
    std::vector<GraphError> errors;

    // Nodes.
    if (graph.nodes.size() > static_cast<size_t>(kMaxGraphNodes))
        errors.push_back({"", "A scene can have at most " + std::to_string(kMaxGraphNodes) + " nodes."});
    int viewports = 0;
    std::set<std::string> ids;
    for (const auto& node : graph.nodes)
    {
        if (node.id.empty())
            errors.push_back({"", "A node has no id."});
        else if (!ids.insert(node.id).second)
            errors.push_back({node.id, "Two nodes share the id \"" + node.id + "\"."});
        const NodeDef& def = Def(node.type);
        if (node.params.index() != def.defaults.index())
            errors.push_back({node.id, Label(node) + " has settings of the wrong kind."});
        else if (def.checkParams != nullptr)
            def.checkParams(node, errors);
        if (node.type == NodeType::Viewport && ++viewports > 1)
            errors.push_back({node.id, "There can only be one viewport."});
    }
    if (viewports == 0)
        errors.push_back({"", "There's no viewport."});

    // Wires: ends, inputs, kinds, one wire per input.
    std::set<std::tuple<std::string, int, std::string>> taken;
    std::vector<const Wire*> usable; // wires whose ends exist, for the graph walks below
    for (const auto& wire : graph.wires)
    {
        const Node* from = graph.Find(wire.fromNode);
        const Node* to = graph.Find(wire.toNode);
        if (from == nullptr || to == nullptr)
        {
            errors.push_back({to != nullptr ? to->id : std::string(), "A wire comes from or goes to a node that isn't there."});
            continue;
        }
        usable.push_back(&wire);
        const auto out = OutputKind(*from);
        const auto in = InputKind(*to, wire.toInput);
        const bool textureInput = wire.toInput == Def(to->type).textureInput;
        if (!out)
            errors.push_back({from->id, Label(*from) + " has no output."});
        else if (!in)
            errors.push_back({to->id, Label(*to) + " has no input " + std::to_string(wire.toInput + 1) + "."});
        else if (*out != *in)
            errors.push_back({to->id, "This input of the " + Label(*to) + " takes " + KindName(*in) + ", not " + KindName(*out) + "."});
        if (textureInput && wire.textureName.empty())
            errors.push_back({to->id, "A texture input needs the name of the texture it replaces."});
        else if (!textureInput && !wire.textureName.empty())
            errors.push_back({to->id, "Only texture inputs have a texture name."});
        if (!taken.insert({wire.toNode, wire.toInput, wire.textureName}).second)
            errors.push_back({to->id, Label(*to) + " has two wires into one input."});
    }

    // Loops: depth-first search over the wires (from -> to).
    std::map<std::string, std::vector<std::string>> downstream;
    for (const Wire* wire : usable)
        downstream[wire->fromNode].push_back(wire->toNode);
    std::map<std::string, int> state; // 0 new, 1 on the stack, 2 done
    bool loop = false;
    std::function<void(const std::string&)> visit = [&](const std::string& id) {
        state[id] = 1;
        for (const auto& next : downstream[id])
        {
            if (state[next] == 1)
            {
                if (!loop)
                    errors.push_back({next, "This would make a loop."});
                loop = true;
            }
            else if (state[next] == 0)
                visit(next);
        }
        state[id] = 2;
    };
    for (const auto& node : graph.nodes)
    {
        if (state[node.id] == 0)
            visit(node.id);
    }
    if (loop)
        return errors; // The walks below assume no loops.

    // Each node type's rules about its place in the graph (wires with both ends only).
    Graph trimmed; // only made when some wire has a missing end
    if (usable.size() != graph.wires.size())
    {
        trimmed.nodes = graph.nodes;
        for (const Wire* wire : usable)
            trimmed.wires.push_back(*wire);
    }
    const Graph& wired = usable.size() == graph.wires.size() ? graph : trimmed;
    for (const auto& node : graph.nodes)
    {
        if (const NodeDef& def = Def(node.type); def.checkInGraph != nullptr && node.params.index() == def.defaults.index())
            def.checkInGraph(wired, node, errors);
    }

    // Each error once.
    std::vector<GraphError> unique;
    for (auto& error : errors)
    {
        if (std::find(unique.begin(), unique.end(), error) == unique.end())
            unique.push_back(std::move(error));
    }
    return unique;
}

auto UniqueNodeId(const Graph& graph, NodeType type) -> std::string
{
    if (type == NodeType::Viewport && graph.Find("viewport") == nullptr)
        return "viewport";
    const std::string prefix = IdPrefix(type);
    for (int n = 1;; ++n)
    {
        std::string candidate = prefix + "-" + std::to_string(n);
        if (graph.Find(candidate) == nullptr)
            return candidate;
    }
}

auto AddNode(Graph& graph, NodeType type, float x, float y) -> std::string
{
    Node node;
    node.id = UniqueNodeId(graph, type);
    node.type = type;
    node.x = x;
    node.y = y;
    node.params = DefaultParams(type);
    graph.nodes.push_back(node);
    return node.id;
}

auto RemoveNode(Graph& graph, const std::string& id) -> bool
{
    auto it = std::find_if(graph.nodes.begin(), graph.nodes.end(), [&](const Node& node) { return node.id == id; });
    if (it == graph.nodes.end() || it->type == NodeType::Viewport)
        return false;
    graph.nodes.erase(it);
    std::erase_if(graph.wires, [&](const Wire& wire) { return wire.fromNode == id || wire.toNode == id; });
    FollowWires(graph);
    return true;
}

auto Connect(Graph& graph, const std::string& from, const std::string& to, int input, const std::string& textureName) -> bool
{
    if (graph.Find(from) == nullptr || graph.Find(to) == nullptr)
        return false;
    Disconnect(graph, to, input, textureName);
    graph.wires.push_back({from, to, input, textureName});
    FollowWires(graph);
    return true;
}

auto ConnectErrors(const Graph& graph, const std::string& from, const std::string& to, int input, const std::string& textureName)
    -> std::vector<GraphError>
{
    Graph trial = graph;
    if (!Connect(trial, from, to, input, textureName))
        return {{"", "That node isn't there any more."}};
    FollowWires(trial);
    return ValidateGraph(trial);
}

auto Disconnect(Graph& graph, const std::string& to, int input, const std::string& textureName) -> bool
{
    const auto before = graph.wires.size();
    std::erase_if(graph.wires, [&](const Wire& wire) { return wire.toNode == to && wire.toInput == input && wire.textureName == textureName; });
    if (graph.wires.size() == before)
        return false;
    FollowWires(graph);
    return true;
}

void FollowWires(Graph& graph)
{
    for (auto& node : graph.nodes)
    {
        if (const auto follow = Def(node.type).followWires; follow != nullptr)
            follow(graph, node);
    }
}

auto TakesAnyKind(const Graph& graph, const Node& node) -> bool
{
    return Def(node.type).anyKindWhenUnwired &&
           std::none_of(graph.wires.begin(), graph.wires.end(), [&](const Wire& wire) { return wire.fromNode == node.id || wire.toNode == node.id; });
}

auto PresetsOf(const Graph& graph) -> std::vector<std::filesystem::path>
{
    std::vector<std::filesystem::path> presets;
    for (const auto& node : graph.nodes)
    {
        if (node.type == NodeType::MilkdropPreset)
            presets.push_back(std::get<MilkdropPresetParams>(node.params).path);
    }
    return presets;
}

auto VideoIdsOf(const Graph& graph) -> std::vector<std::string>
{
    std::vector<std::string> ids;
    for (const auto& node : graph.nodes)
    {
        if (node.type != NodeType::VideoSource)
            continue;
        const auto& source = std::get<VideoSourceParams>(node.params).source;
        if (source.type == VideoSourceType::File && !source.videoId.empty())
            ids.push_back(source.videoId);
    }
    return ids;
}

} // namespace gv::model
