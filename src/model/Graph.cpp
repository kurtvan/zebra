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

auto ParamInput(const Node& node, int param) -> int
{
    const NodeDef& def = Def(node.type);
    return (def.inputCount != nullptr ? def.inputCount(node) : 0) + param;
}

auto InputCount(const Node& node) -> int
{
    return ParamInput(node, static_cast<int>(Def(node.type).params.size()));
}

auto InputKind(const Node& node, int input) -> std::optional<PortKind>
{
    if (input < 0 || input >= InputCount(node))
        return std::nullopt;
    if (input >= ParamInput(node, 0))
        return PortKind::Number;
    return Def(node.type).inputKind(node, input);
}

namespace {

/// "a picture": what a wire of `kind` carries, for messages.
auto KindName(PortKind kind) -> std::string
{
    switch (kind)
    {
        case PortKind::Picture: return "a picture";
        case PortKind::Number: return "a number";
        case PortKind::Audio: return "audio";
    }
    return {};
}

/// "a number (from an LFO or an Envelope)": what an input of `kind` takes, and where it comes from.
auto Takes(PortKind kind) -> std::string
{
    switch (kind)
    {
        case PortKind::Picture: return KindName(kind);
        case PortKind::Number: return "a number (from an LFO or an Envelope)";
        case PortKind::Audio: return "audio (from Audio In or a Band)";
    }
    return {};
}

auto PictureOf(const Graph& graph, const Node& node, std::set<std::string>& seen) -> PictureFrom
{
    if (node.type == NodeType::MilkdropPreset)
        return {false, true};
    if (node.type != NodeType::Switcher)
        return {true, false};
    PictureFrom picture;
    if (!seen.insert(node.id).second)
        return picture; // a loop: ValidateGraph reports it
    for (const auto& wire : graph.wires)
    {
        const Node* from = wire.toNode == node.id ? graph.Find(wire.fromNode) : nullptr;
        if (from == nullptr)
            continue;
        const PictureFrom in = PictureOf(graph, *from, seen);
        picture.video |= in.video;
        picture.preset |= in.preset;
    }
    return picture;
}

// --- Not supported yet ---------------------------------------------------------------------------
// Pictures the engine can't render yet, one rule each, so each of Project E's tickets deletes its own.

/// A preset's picture into another preset.
void CheckPresetTakesVideo(const Node& to, PictureFrom picture, std::vector<GraphError>& errors)
{
    if (to.type == NodeType::MilkdropPreset && picture.preset)
        errors.push_back({to.id, Label(to) + ": its inputs take a video, not another preset (not supported yet)."});
}

/// A preset's picture into an effect or a shader.
void CheckEffectTakesVideo(const Node& to, PictureFrom picture, std::vector<GraphError>& errors)
{
    if ((IsEffect(to.type) || to.type == NodeType::IsfShader) && picture.preset)
        errors.push_back({to.id, Label(to) + ": an effect works on a video, not on a preset's picture (not supported yet)."});
}

/// A switcher that cuts between a video and a preset.
void CheckSwitcherDoesNotMix(const Graph& graph, const Node& node, std::vector<GraphError>& errors)
{
    if (node.type != NodeType::Switcher)
        return;
    if (const PictureFrom picture = PictureOf(graph, node); picture.video && picture.preset)
        errors.push_back({node.id, Label(node) + ": it switches videos or presets, not both (not supported yet)."});
}

void CheckNotSupportedYet(const Graph& graph, std::vector<GraphError>& errors)
{
    for (const auto& wire : graph.wires)
    {
        const Node* from = graph.Find(wire.fromNode);
        const Node* to = graph.Find(wire.toNode);
        if (from == nullptr || to == nullptr || OutputKind(*from) != PortKind::Picture || InputKind(*to, wire.toInput) != PortKind::Picture)
            continue; // reported already, or a number
        const PictureFrom picture = PictureOf(graph, *from);
        CheckPresetTakesVideo(*to, picture, errors);
        CheckEffectTakesVideo(*to, picture, errors);
    }
    for (const auto& node : graph.nodes)
        CheckSwitcherDoesNotMix(graph, node, errors);
}

} // namespace

auto PictureOf(const Graph& graph, const Node& node) -> PictureFrom
{
    std::set<std::string> seen;
    return PictureOf(graph, node, seen);
}

auto SwitchesVideos(const Graph& graph, const Node& node) -> bool
{
    if (node.type != NodeType::Switcher)
        return false;
    if (std::any_of(graph.wires.begin(), graph.wires.end(), [&](const Wire& wire) { return wire.toNode == node.id; }))
        return PictureOf(graph, node).video;
    return std::any_of(graph.wires.begin(), graph.wires.end(), [&](const Wire& wire) {
        const Node* to = wire.fromNode == node.id ? graph.Find(wire.toNode) : nullptr;
        return to != nullptr && to->type != NodeType::Viewport && to->type != NodeType::Switcher;
    });
}

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
        for (int param = 0; param < static_cast<int>(def.params.size()); ++param)
            CheckRange(errors, node, (std::string(def.params[static_cast<size_t>(param)].name) + " depth").c_str(), DepthOf(node, param), -1.0, 1.0, "-1 and 1");
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
        const bool textureInput = wire.toInput == Def(to->type).textureInput;
        if (!OutputKind(*from))
            errors.push_back({from->id, Label(*from) + " has no output."});
        else if (!InputKind(*to, wire.toInput))
            errors.push_back({to->id, Label(*to) + " has no input " + std::to_string(wire.toInput + 1) + "."});
        else if (*InputKind(*to, wire.toInput) != *OutputKind(*from))
            errors.push_back({to->id, Label(*to) + ": this input takes " + Takes(*InputKind(*to, wire.toInput)) + ", not " + KindName(*OutputKind(*from)) + "."});
        if (textureInput && wire.textureName.empty())
            errors.push_back({to->id, "A texture input needs the name of the texture it replaces."});
        else if (!textureInput && !wire.textureName.empty())
            errors.push_back({to->id, "Only texture inputs have a texture name."});
        if (!taken.insert({wire.toNode, wire.toInput, wire.textureName}).second)
            errors.push_back({to->id, Label(*to) + " has two wires into one input."});
    }

    CheckNotSupportedYet(graph, errors);

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
