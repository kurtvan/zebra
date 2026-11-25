#include "model/Arrange.hpp"

#include <algorithm>
#include <set>
#include <tuple>

namespace gv::model {

namespace {

/// The wires between two nodes that exist, by source node, sorted so the layout never depends on wire order.
auto Outgoing(const Graph& graph) -> std::map<std::string, std::vector<const Wire*>>
{
    std::map<std::string, std::vector<const Wire*>> out;
    for (const auto& wire : graph.wires)
    {
        if (graph.Find(wire.fromNode) != nullptr && graph.Find(wire.toNode) != nullptr)
            out[wire.fromNode].push_back(&wire);
    }
    for (auto& [id, wires] : out)
    {
        std::sort(wires.begin(), wires.end(), [](const Wire* a, const Wire* b) {
            return std::tie(a->toNode, a->toInput, a->textureName) < std::tie(b->toNode, b->toInput, b->textureName);
        });
    }
    return out;
}

/// The longest path from `id` to an end, memoized in `depths`. `visiting` holds the nodes on the current path,
/// so a wire back into one of them (a loop) is ignored.
auto Depth(const std::string& id, const std::map<std::string, std::vector<const Wire*>>& out, std::map<std::string, int>& depths,
           std::set<std::string>& visiting) -> int
{
    if (auto it = depths.find(id); it != depths.end())
        return it->second;
    int depth = 0;
    visiting.insert(id);
    if (auto it = out.find(id); it != out.end())
    {
        for (const Wire* wire : it->second)
        {
            if (visiting.count(wire->toNode) == 0)
                depth = std::max(depth, Depth(wire->toNode, out, depths, visiting) + 1);
        }
    }
    visiting.erase(id);
    depths[id] = depth;
    return depth;
}

} // namespace

auto ArrangeDepths(const Graph& graph) -> std::map<std::string, int>
{
    const auto out = Outgoing(graph);
    std::map<std::string, int> depths;
    std::set<std::string> visiting;
    for (const auto& node : graph.nodes)
        Depth(node.id, out, depths, visiting);
    return depths;
}

auto ArrangeIntoColumns(const Graph& graph) -> ArrangeColumns
{
    const auto out = Outgoing(graph);
    std::set<std::string> wired;
    for (const auto& [from, wires] : out)
    {
        wired.insert(from);
        for (const Wire* wire : wires)
            wired.insert(wire->toNode);
    }
    const auto depths = ArrangeDepths(graph);

    std::vector<std::string> loose;
    std::vector<std::vector<std::string>> byDepth; // index = depth, 0 = rightmost
    for (const auto& node : graph.nodes)
    {
        if (wired.count(node.id) == 0 && node.type != NodeType::Viewport)
        {
            loose.push_back(node.id);
            continue;
        }
        const auto depth = static_cast<size_t>(depths.at(node.id));
        if (byDepth.size() <= depth)
            byDepth.resize(depth + 1);
        byDepth[depth].push_back(node.id);
    }
    std::sort(loose.begin(), loose.end());

    // Where each placed node sits in its column, 0..1, so columns of different lengths compare.
    std::map<std::string, double> row;
    const auto placeRows = [&](const std::vector<std::string>& column) {
        for (size_t i = 0; i < column.size(); ++i)
            row[column[i]] = (static_cast<double>(i) + 0.5) / static_cast<double>(column.size());
    };
    if (!byDepth.empty())
    {
        // The ends: the viewport first, then loose chains' ends.
        std::sort(byDepth[0].begin(), byDepth[0].end(), [&](const std::string& a, const std::string& b) {
            const bool aViewport = graph.Find(a)->type == NodeType::Viewport;
            const bool bViewport = graph.Find(b)->type == NodeType::Viewport;
            return std::tie(bViewport, a) < std::tie(aViewport, b);
        });
        placeRows(byDepth[0]);
    }
    for (size_t depth = 1; depth < byDepth.size(); ++depth)
    {
        // Every node a node feeds has a smaller depth, so it is placed already (except along an ignored loop).
        struct Key
        {
            double barycentre;
            int input;
            std::string id;
        };
        std::vector<Key> keys;
        for (const auto& id : byDepth[depth])
        {
            double sum = 0.0;
            int count = 0;
            int input = 0;
            for (const Wire* wire : out.at(id))
            {
                if (auto it = row.find(wire->toNode); it != row.end())
                {
                    if (count == 0)
                        input = wire->toInput;
                    sum += it->second;
                    count++;
                }
            }
            keys.push_back({count > 0 ? sum / count : 2.0, input, id});
        }
        std::sort(keys.begin(), keys.end(),
                  [](const Key& a, const Key& b) { return std::tie(a.barycentre, a.input, a.id) < std::tie(b.barycentre, b.input, b.id); });
        for (size_t i = 0; i < keys.size(); ++i)
            byDepth[depth][i] = keys[i].id;
        placeRows(byDepth[depth]);
    }

    ArrangeColumns columns;
    if (!loose.empty())
        columns.push_back(loose);
    for (auto it = byDepth.rbegin(); it != byDepth.rend(); ++it)
        columns.push_back(*it);
    return columns;
}

auto PlaceColumns(const ArrangeColumns& columns, const std::map<std::string, ArrangeSize>& sizes, Vec2 origin) -> std::map<std::string, Vec2>
{
    const auto sizeOf = [&](const std::string& id) {
        auto it = sizes.find(id);
        return it != sizes.end() ? it->second : ArrangeSize{};
    };
    std::vector<float> heights;
    float tallest = 0.0f;
    for (const auto& column : columns)
    {
        float height = 0.0f;
        for (const auto& id : column)
            height += sizeOf(id).height;
        if (!column.empty())
            height += kArrangeRowGap * static_cast<float>(column.size() - 1);
        heights.push_back(height);
        tallest = std::max(tallest, height);
    }

    std::map<std::string, Vec2> positions;
    float x = origin.x;
    for (size_t c = 0; c < columns.size(); ++c)
    {
        float y = origin.y + (tallest - heights[c]) * 0.5f;
        float width = 0.0f;
        for (const auto& id : columns[c])
        {
            const ArrangeSize size = sizeOf(id);
            positions[id] = {x, y};
            y += size.height + kArrangeRowGap;
            width = std::max(width, size.width);
        }
        x += width + kArrangeColumnGap;
    }
    return positions;
}

auto ArrangeLayered(const Graph& graph, const std::map<std::string, ArrangeSize>& sizes) -> std::map<std::string, Vec2>
{
    if (graph.nodes.empty())
        return {};
    Vec2 origin{graph.nodes.front().x, graph.nodes.front().y};
    for (const auto& node : graph.nodes)
    {
        origin.x = std::min(origin.x, node.x);
        origin.y = std::min(origin.y, node.y);
    }
    return PlaceColumns(ArrangeIntoColumns(graph), sizes, origin);
}

} // namespace gv::model
