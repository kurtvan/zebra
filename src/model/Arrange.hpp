#pragma once

#include "model/Graph.hpp"

#include <map>
#include <string>
#include <vector>

namespace gv::model {

constexpr float kArrangeColumnGap = 80.0f; //!< Canvas units between the widest node of a column and the next column.
constexpr float kArrangeRowGap = 40.0f;    //!< Canvas units between two nodes of a column.
constexpr float kArrangeDefaultWidth = 220.0f; //!< For a node with no measured size (not drawn yet).
constexpr float kArrangeDefaultHeight = 120.0f;

/// A node's size on the canvas, as the editor measured it.
struct ArrangeSize
{
    float width{kArrangeDefaultWidth};
    float height{kArrangeDefaultHeight};
};

/// A node's new top-left corner on the canvas.
struct Vec2
{
    float x{0.0f};
    float y{0.0f};

    auto operator==(const Vec2&) const -> bool = default;
};

/// The columns of a layered layout, left to right, each listing node ids top to bottom.
using ArrangeColumns = std::vector<std::vector<std::string>>;

/// How many wires the longest path from each node to a node with no outgoing wires has (the viewport, or the
/// end of a loose chain): 0 for those ends. Wires that would close a loop are ignored.
auto ArrangeDepths(const Graph& graph) -> std::map<std::string, int>;

/// Splits the nodes into columns by ArrangeDepths (deepest on the left, depth 0 on the right) and orders each
/// column to cut down crossings: by where the nodes they feed sit (one barycentre pass from right to left,
/// then input order), ties by id. Nodes with no wires at all get their own column on the far left, by id. The
/// viewport always stays in the rightmost column, at its top.
auto ArrangeIntoColumns(const Graph& graph) -> ArrangeColumns;

/// Places `columns` left to right with kArrangeColumnGap between them, stacking each column with kArrangeRowGap
/// and centring it on the same middle line. Starts at `origin` (the top left of the tallest column). Nodes
/// missing from `sizes` get the default size.
auto PlaceColumns(const ArrangeColumns& columns, const std::map<std::string, ArrangeSize>& sizes, Vec2 origin = {})
    -> std::map<std::string, Vec2>;

/// The Arrange button: every node's new position, laid out from the wires (sources on the left, the viewport on
/// the right), keeping the layout's top left where the graph's top left was. The same graph always gives the
/// same layout.
auto ArrangeLayered(const Graph& graph, const std::map<std::string, ArrangeSize>& sizes) -> std::map<std::string, Vec2>;

} // namespace gv::model
