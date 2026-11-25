#include "model/nodes/NodeDef.hpp"
#include "model/nodes/NodeJson.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace gv::model::nodes {

/// Passes one of its inputs at a time, moving on every interval. It switches whatever pictures are wired into
/// it, videos or Milkdrop presets' (not both yet: ValidateGraph). It always has one empty input after its last
/// wired one.
auto SwitcherDef() -> NodeDef
{
    NodeDef def;
    def.type = NodeType::Switcher;
    def.jsonName = "switcher";
    def.idPrefix = "switcher";
    def.displayName = "Switcher";
    def.defaults = SwitcherParams{};
    def.output = [](const Node&) -> std::optional<PortKind> { return PortKind::Picture; };
    def.inputCount = [](const Node& node) { return ParamsOf<SwitcherParams>(node).inputCount; };
    def.inputKind = [](const Node&, int) { return PortKind::Picture; };
    def.followWires = [](const Graph& graph, Node& node) {
        auto* params = std::get_if<SwitcherParams>(&node.params);
        if (params == nullptr)
            return; // wrong settings: ValidateGraph reports it
        int last = -1;
        for (const auto& wire : graph.wires)
        {
            if (wire.toNode == node.id)
                last = std::max(last, wire.toInput);
        }
        params->inputCount = last + 2;
    };
    def.checkParams = [](const Node& node, std::vector<GraphError>& errors) {
        const auto& params = ParamsOf<SwitcherParams>(node);
        if (params.unit == IntervalUnit::Seconds)
            CheckRange(errors, node, "the interval", params.intervalSeconds, kMinPlaylistInterval, kMaxPlaylistInterval, "1 and 3600 seconds");
        else
            CheckRange(errors, node, "the interval", params.intervalCount, 1, kMaxPlaylistCount, "1 and 256");
        if (params.inputCount < 1 || params.inputCount > kMaxPlaylistCount + 1)
            errors.push_back({node.id, Label(node) + ": it can have at most 256 inputs."});
    };
    // Not supported yet: fed by another switcher, directly or through effects. A preset makes a new picture,
    // so a switcher further up, behind a preset, is fine.
    def.checkInGraph = [](const Graph& graph, const Node& node, std::vector<GraphError>& errors) {
        std::map<std::string, std::vector<std::string>> upstream;
        for (const auto& wire : graph.wires)
            upstream[wire.toNode].push_back(wire.fromNode);
        std::set<std::string> seen;
        std::vector<std::string> stack = upstream[node.id];
        while (!stack.empty())
        {
            const std::string id = stack.back();
            stack.pop_back();
            if (!seen.insert(id).second)
                continue;
            const Node* other = graph.Find(id);
            if (other != nullptr && other->type == NodeType::Switcher)
            {
                errors.push_back({node.id, "A switcher can't be fed by another switcher."});
                return;
            }
            if (other != nullptr && other->type == NodeType::MilkdropPreset)
                continue;
            for (const auto& next : upstream[id])
                stack.push_back(next);
        }
    };
    def.write = [](const Node& node, nlohmann::json& object) {
        const auto& params = ParamsOf<SwitcherParams>(node);
        object["intervalUnit"] = params.unit == IntervalUnit::Seconds ? "seconds" : params.unit == IntervalUnit::Beats ? "beats" : "bars";
        object["intervalSeconds"] = params.intervalSeconds;
        object["intervalCount"] = params.intervalCount;
        object["shuffle"] = params.shuffle;
    };
    // Files before one picture kind have "kind": "video" or "frame". It's ignored: the wires say it all.
    def.read = [](const nlohmann::json& object, Node& node, const std::string& path, std::vector<Issue>& issues) {
        ParseTiming(object, ParamsOf<SwitcherParams>(node), path, issues);
    };
    return def;
}

} // namespace gv::model::nodes
