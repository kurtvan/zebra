#include "model/NodeList.hpp"

#include "model/nodes/NodeDef.hpp"

#include <algorithm>
#include <cctype>
#include <tuple>

namespace gv::model {

namespace {

auto Lower(std::string_view text) -> std::string
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

} // namespace

auto NodesReachingViewport(const Graph& graph) -> std::set<std::string>
{
    std::set<std::string> reached;
    const Node* viewport = graph.Viewport();
    if (viewport == nullptr)
        return reached;
    std::vector<std::string> todo{viewport->id};
    reached.insert(viewport->id);
    while (!todo.empty())
    {
        const std::string node = std::move(todo.back());
        todo.pop_back();
        for (const auto& wire : graph.wires)
        {
            if (wire.toNode == node && reached.insert(wire.fromNode).second)
                todo.push_back(wire.fromNode);
        }
    }
    return reached;
}

auto ListNodes(const Graph& graph, std::string_view filter, const std::function<std::string(const Node&)>& label) -> std::vector<NodeRow>
{
    const std::set<std::string> reached = NodesReachingViewport(graph);
    const std::string needle = Lower(filter);
    std::vector<NodeRow> rows;
    for (const auto& node : graph.nodes)
    {
        NodeRow row{node.id, label ? label(node) : Label(node), DisplayName(node.type), reached.count(node.id) > 0};
        if (!needle.empty() && Lower(row.label).find(needle) == std::string::npos && Lower(row.type).find(needle) == std::string::npos)
            continue;
        rows.push_back(std::move(row));
    }
    std::stable_sort(rows.begin(), rows.end(), [](const NodeRow& a, const NodeRow& b) {
        return std::make_tuple(!a.connected, Lower(a.label), a.id) < std::make_tuple(!b.connected, Lower(b.label), b.id);
    });
    return rows;
}

} // namespace gv::model
