#include "model/Graph.hpp"

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
    switch (type)
    {
    case NodeType::VideoSource: return VideoSourceParams{};
    case NodeType::Contrast: return ContrastParams{};
    case NodeType::Saturation: return SaturationParams{};
    case NodeType::LumaGate: return LumaGateParams{};
    case NodeType::Mix: return MixParams{};
    case NodeType::Preset: return PresetParams{};
    case NodeType::Switcher: return SwitcherParams{};
    case NodeType::Viewport: return ViewportParams{};
    }
    return ViewportParams{};
}

auto IdPrefix(NodeType type) -> const char*
{
    switch (type)
    {
    case NodeType::VideoSource: return "video";
    case NodeType::Contrast: return "contrast";
    case NodeType::Saturation: return "saturation";
    case NodeType::LumaGate: return "gate";
    case NodeType::Mix: return "mix";
    case NodeType::Preset: return "preset";
    case NodeType::Switcher: return "switcher";
    case NodeType::Viewport: return "viewport";
    }
    return "node";
}

auto DisplayName(NodeType type) -> const char*
{
    switch (type)
    {
    case NodeType::VideoSource: return "Video source";
    case NodeType::Contrast: return "Contrast";
    case NodeType::Saturation: return "Saturation";
    case NodeType::LumaGate: return "Luma gate";
    case NodeType::Mix: return "Mix";
    case NodeType::Preset: return "Preset";
    case NodeType::Switcher: return "Switcher";
    case NodeType::Viewport: return "Viewport";
    }
    return "Node";
}

auto IsEffect(NodeType type) -> bool
{
    return type == NodeType::Contrast || type == NodeType::Saturation || type == NodeType::LumaGate || type == NodeType::Mix;
}

auto OutputKind(const Node& node) -> std::optional<PortKind>
{
    switch (node.type)
    {
    case NodeType::VideoSource:
    case NodeType::Contrast:
    case NodeType::Saturation:
    case NodeType::LumaGate:
    case NodeType::Mix: return PortKind::Video;
    case NodeType::Preset: return PortKind::Frame;
    case NodeType::Switcher: return std::get<SwitcherParams>(node.params).kind;
    case NodeType::Viewport: return std::nullopt;
    }
    return std::nullopt;
}

auto InputCount(const Node& node) -> int
{
    switch (node.type)
    {
    case NodeType::VideoSource: return 0;
    case NodeType::Contrast:
    case NodeType::Saturation:
    case NodeType::LumaGate:
    case NodeType::Mix:
    case NodeType::Viewport: return 1;
    case NodeType::Preset: return kPresetInputs;
    case NodeType::Switcher: return std::get<SwitcherParams>(node.params).inputCount;
    }
    return 0;
}

auto InputKind(const Node& node, int input) -> std::optional<PortKind>
{
    if (input < 0 || input >= InputCount(node))
        return std::nullopt;
    switch (node.type)
    {
    case NodeType::Viewport: return PortKind::Frame;
    case NodeType::Switcher: return std::get<SwitcherParams>(node.params).kind;
    default: return PortKind::Video;
    }
}

namespace {

auto KindName(PortKind kind) -> const char*
{
    return kind == PortKind::Video ? "video" : "a preset's picture";
}

auto Named(const Node& node) -> std::string
{
    if (node.type == NodeType::Switcher)
        return std::get<SwitcherParams>(node.params).kind == PortKind::Video ? "Video switcher" : "Preset switcher";
    return DisplayName(node.type);
}

void CheckRange(std::vector<GraphError>& errors, const Node& node, const char* what, double value, double low, double high, const char* range)
{
    if (!(value >= low && value <= high))
        errors.push_back({node.id, Named(node) + ": " + what + " must be between " + range + "."});
}

void CheckParams(std::vector<GraphError>& errors, const Node& node)
{
    std::visit(
        [&](const auto& params) {
            using T = std::decay_t<decltype(params)>;
            if constexpr (std::is_same_v<T, ContrastParams>)
                CheckRange(errors, node, "contrast", params.contrast, 0.0, kMaxContrast, "0 and 100");
            else if constexpr (std::is_same_v<T, SaturationParams>)
                CheckRange(errors, node, "saturation", params.saturation, 0.0, 2.0, "0 and 2");
            else if constexpr (std::is_same_v<T, LumaGateParams>)
            {
                CheckRange(errors, node, "threshold", params.threshold, 0.0, 1.0, "0 and 1");
                CheckRange(errors, node, "softness", params.softness, 0.0, 1.0, "0 and 1");
            }
            else if constexpr (std::is_same_v<T, MixParams>)
                CheckRange(errors, node, "opacity", params.opacity, 0.0, 1.0, "0 and 1");
            else if constexpr (std::is_same_v<T, SwitcherParams>)
            {
                if (params.unit == IntervalUnit::Seconds)
                    CheckRange(errors, node, "the interval", params.intervalSeconds, kMinPlaylistInterval, kMaxPlaylistInterval, "1 and 3600 seconds");
                else
                    CheckRange(errors, node, "the interval", params.intervalCount, 1, kMaxPlaylistCount, "1 and 256");
                if (params.inputCount < 1 || params.inputCount > kMaxPlaylistCount + 1)
                    errors.push_back({node.id, Named(node) + ": it can have at most 256 inputs."});
            }
        },
        node.params);
}

/// Walks upstream from `node` through effect nodes (one input each) to the video source or Video switcher
/// the chain starts from. Null if the chain has no start yet (or loops).
auto RootOf(const Graph& graph, const Node* node) -> const Node*
{
    for (int steps = 0; node != nullptr && steps <= static_cast<int>(graph.nodes.size()); ++steps)
    {
        if (!IsEffect(node->type))
            return node;
        node = graph.Upstream(node->id, 0);
    }
    return nullptr;
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
        if (node.params.index() != DefaultParams(node.type).index())
            errors.push_back({node.id, Named(node) + " has settings of the wrong kind."});
        else
            CheckParams(errors, node);
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
        const bool textureInput = to->type == NodeType::Preset && wire.toInput == static_cast<int>(PresetLayer::Texture);
        if (!out)
            errors.push_back({from->id, Named(*from) + " has no output."});
        else if (!in)
            errors.push_back({to->id, Named(*to) + " has no input " + std::to_string(wire.toInput + 1) + "."});
        else if (*out != *in)
            errors.push_back({to->id, "This input of the " + Named(*to) + " takes " + KindName(*in) + ", not " + KindName(*out) + "."});
        if (textureInput && wire.textureName.empty())
            errors.push_back({to->id, "A texture input needs the name of the texture it replaces."});
        else if (!textureInput && !wire.textureName.empty())
            errors.push_back({to->id, "Only texture inputs have a texture name."});
        if (!taken.insert({wire.toNode, wire.toInput, wire.textureName}).second)
            errors.push_back({to->id, Named(*to) + " has two wires into one input."});
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

    // A switcher can't be fed (directly or further upstream) by another switcher of the same kind.
    std::map<std::string, std::vector<std::string>> upstream;
    for (const Wire* wire : usable)
        upstream[wire->toNode].push_back(wire->fromNode);
    for (const auto& node : graph.nodes)
    {
        if (node.type != NodeType::Switcher)
            continue;
        const PortKind kind = std::get<SwitcherParams>(node.params).kind;
        std::set<std::string> seen;
        std::vector<std::string> stack = upstream[node.id];
        bool nested = false;
        while (!stack.empty() && !nested)
        {
            const std::string id = stack.back();
            stack.pop_back();
            if (!seen.insert(id).second)
                continue;
            const Node* other = graph.Find(id);
            if (other != nullptr && other->type == NodeType::Switcher && std::get<SwitcherParams>(other->params).kind == kind)
                nested = true;
            for (const auto& next : upstream[id])
                stack.push_back(next);
        }
        if (nested)
            errors.push_back({node.id, std::string("A ") + Named(node) + " can't be fed by another " + Named(node) + "."});
    }

    // One video source at a time: every video input of a preset starts from the same source or Video switcher.
    for (const auto& node : graph.nodes)
    {
        if (node.type != NodeType::Preset)
            continue;
        std::set<std::string> roots;
        for (const Wire* wire : usable)
        {
            if (wire->toNode != node.id)
                continue;
            if (const Node* root = RootOf(graph, graph.Find(wire->fromNode)); root != nullptr)
                roots.insert(root->id);
        }
        if (roots.size() > 1)
            errors.push_back({node.id, "A preset can show only one video at a time, but its inputs come from " + std::to_string(roots.size()) +
                                           " different sources."});
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
    NormalizeSwitcherInputs(graph);
    return true;
}

auto Connect(Graph& graph, const std::string& from, const std::string& to, int input, const std::string& textureName) -> bool
{
    if (graph.Find(from) == nullptr || graph.Find(to) == nullptr)
        return false;
    Disconnect(graph, to, input, textureName);
    graph.wires.push_back({from, to, input, textureName});
    NormalizeSwitcherInputs(graph);
    return true;
}

auto Disconnect(Graph& graph, const std::string& to, int input, const std::string& textureName) -> bool
{
    const auto before = graph.wires.size();
    std::erase_if(graph.wires, [&](const Wire& wire) { return wire.toNode == to && wire.toInput == input && wire.textureName == textureName; });
    if (graph.wires.size() == before)
        return false;
    NormalizeSwitcherInputs(graph);
    return true;
}

void NormalizeSwitcherInputs(Graph& graph)
{
    for (auto& node : graph.nodes)
    {
        if (node.type != NodeType::Switcher)
            continue;
        int last = -1;
        for (const auto& wire : graph.wires)
        {
            if (wire.toNode == node.id)
                last = std::max(last, wire.toInput);
        }
        std::get<SwitcherParams>(node.params).inputCount = last + 2;
    }
}

auto PresetsOf(const Graph& graph) -> std::vector<std::filesystem::path>
{
    std::vector<std::filesystem::path> presets;
    for (const auto& node : graph.nodes)
    {
        if (node.type == NodeType::Preset)
            presets.push_back(std::get<PresetParams>(node.params).path);
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
