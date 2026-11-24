#pragma once

#include "model/Graph.hpp"

#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace gv::model {

/// One row of the Nodes pane.
struct NodeRow
{
    std::string id;
    std::string label;      //!< What the canvas's title bar says.
    std::string type;       //!< DisplayName of its type ("Luma gate").
    bool connected{false};  //!< It reaches the viewport (through any switcher input), or it is the viewport.

    auto operator==(const NodeRow&) const -> bool = default;
};

/// The ids of every node whose output reaches the viewport along wires, through every input of a switcher
/// (not only the one passing now), plus the viewport itself. Empty if the graph has no viewport.
auto NodesReachingViewport(const Graph& graph) -> std::set<std::string>;

/// The nodes of `graph` whose label or type name contains `filter` (ignoring case; empty matches all): the
/// connected ones first, then the rest, each by label (ignoring case), then id. `label` names a node; null:
/// model::Label.
auto ListNodes(const Graph& graph, std::string_view filter, const std::function<std::string(const Node&)>& label = {})
    -> std::vector<NodeRow>;

} // namespace gv::model
