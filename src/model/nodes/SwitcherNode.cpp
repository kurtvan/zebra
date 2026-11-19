#include "model/nodes/NodeDef.hpp"
#include "model/nodes/NodeJson.hpp"

#include <map>
#include <set>

namespace gv::model::nodes {

/// Passes one of its inputs at a time, moving on every interval: presets' pictures (a Preset switcher) or
/// videos (a Video switcher). It always has one empty input after its last wired one (NormalizeSwitcherInputs).
auto SwitcherDef() -> NodeDef
{
    NodeDef def;
    def.type = NodeType::Switcher;
    def.jsonName = "switcher";
    def.idPrefix = "switcher";
    def.displayName = "Switcher";
    def.defaults = SwitcherParams{};
    def.output = [](const Node& node) -> std::optional<PortKind> { return ParamsOf<SwitcherParams>(node).kind; };
    def.inputCount = [](const Node& node) { return ParamsOf<SwitcherParams>(node).inputCount; };
    def.inputKind = [](const Node& node, int) { return ParamsOf<SwitcherParams>(node).kind; };
    def.label = [](const Node& node) -> std::string {
        return ParamsOf<SwitcherParams>(node).kind == PortKind::Video ? "Video switcher" : "Preset switcher";
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
    // Not fed, directly or further upstream, by another switcher of the same kind.
    def.checkInGraph = [](const Graph& graph, const Node& node, std::vector<GraphError>& errors) {
        std::map<std::string, std::vector<std::string>> upstream;
        for (const auto& wire : graph.wires)
            upstream[wire.toNode].push_back(wire.fromNode);
        const PortKind kind = ParamsOf<SwitcherParams>(node).kind;
        std::set<std::string> seen;
        std::vector<std::string> stack = upstream[node.id];
        while (!stack.empty())
        {
            const std::string id = stack.back();
            stack.pop_back();
            if (!seen.insert(id).second)
                continue;
            const Node* other = graph.Find(id);
            if (other != nullptr && other->type == NodeType::Switcher && ParamsOf<SwitcherParams>(*other).kind == kind)
            {
                errors.push_back({node.id, "A " + Label(node) + " can't be fed by another " + Label(node) + "."});
                return;
            }
            for (const auto& next : upstream[id])
                stack.push_back(next);
        }
    };
    def.write = [](const Node& node, nlohmann::json& object) {
        const auto& params = ParamsOf<SwitcherParams>(node);
        object["kind"] = params.kind == PortKind::Video ? "video" : "frame";
        object["intervalUnit"] = params.unit == IntervalUnit::Seconds ? "seconds" : params.unit == IntervalUnit::Beats ? "beats" : "bars";
        object["intervalSeconds"] = params.intervalSeconds;
        object["intervalCount"] = params.intervalCount;
        object["shuffle"] = params.shuffle;
    };
    def.read = [](const nlohmann::json& object, Node& node, const std::string& path, std::vector<Issue>& issues) {
        auto& params = ParamsOf<SwitcherParams>(node);
        std::string kind = "frame";
        ReadField(object, "kind", kind, path, issues);
        if (kind == "video")
            params.kind = PortKind::Video;
        else if (kind != "frame")
            issues.push_back(JsonError(path + "/kind", "unknown switcher kind \"" + kind + "\" (expected \"frame\" or \"video\")"));
        ParseTiming(object, params, path, issues);
    };
    return def;
}

} // namespace gv::model::nodes
