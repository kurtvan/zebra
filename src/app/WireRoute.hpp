#pragma once

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <tuple>
#include <utility>
#include <vector>

namespace gv::app {

constexpr float kWireStub = 16.f;        //!< How far a wire runs straight out of an output, or into an input.
constexpr float kWireLaneSpacing = 6.f;  //!< How far apart wires sharing a run are drawn.
constexpr float kWireLoopClearance = 60.f; //!< How far a backwards wire drops below its ends when they're level.
constexpr float kWireNodeMargin = 14.f;  //!< The gap wires keep round every node (canvas units).
constexpr int kWireSideLanes = 2;        //!< Lanes either side of a run's first choice, for wires that would share it.
constexpr float kWireBendCost = 40.f;    //!< A turn costs as much as this much more length: fewer turns look tidier.
constexpr float kWireShareCost = 1.5f;  //!< Running along another wire costs this much more per unit of length.
constexpr float kWireSideWeight = 1.05f;  //!< Running along a node's margin costs a bit more than down a gap's middle.

/// How far lane `lane` sits from the middle: 0, then one spacing either side, then two, and so on.
inline auto LaneOffset(int lane) -> float
{
    const int step = (lane + 1) / 2;
    return static_cast<float>(lane % 2 == 1 ? step : -step) * kWireLaneSpacing;
}

/// `points` without repeated points or points in the middle of a straight run.
inline auto Simplified(const std::vector<ImVec2>& points) -> std::vector<ImVec2>
{
    std::vector<ImVec2> out;
    for (const ImVec2& p : points)
    {
        if (!out.empty() && out.back().x == p.x && out.back().y == p.y)
            continue;
        if (out.size() >= 2)
        {
            const ImVec2& a = out[out.size() - 2];
            const ImVec2& b = out.back();
            if ((a.x == b.x && b.x == p.x) || (a.y == b.y && b.y == p.y))
                out.back() = p; // b is on the straight run from a to p
            else
                out.push_back(p);
            continue;
        }
        out.push_back(p);
    }
    return out;
}

/// A wire from the output at `from` to the input at `to`, as right-angle runs (canvas units): out of the output
/// to the right, down or up a vertical lane between the two, and into the input from the left. When the input
/// isn't far enough to the right of the output, it loops round: right, down or up, back left, then into the
/// input from the left. `laneIndex` (0 and up) moves the wire's vertical and looping runs a few units, so wires
/// that would share a run (see AssignLanes) stay apart. The same arguments always give the same points.
inline auto RouteWire(ImVec2 from, ImVec2 to, int laneIndex) -> std::vector<ImVec2>
{
    const float shift = LaneOffset(laneIndex);
    if (to.x - from.x >= 2.f * kWireStub)
    {
        if (std::abs(to.y - from.y) < 0.5f)
            return {from, to}; // level: one straight run
        const float x = std::clamp(std::round((from.x + to.x) * 0.5f) + shift, from.x + kWireStub, to.x - kWireStub);
        return Simplified({from, ImVec2(x, from.y), ImVec2(x, to.y), to});
    }
    // Behind the output: loop round, between the two ends, or below them when they're about level.
    const float out = static_cast<float>(laneIndex) * kWireLaneSpacing;
    const float right = from.x + kWireStub + out;
    const float left = to.x - kWireStub - out;
    float y = std::round((from.y + to.y) * 0.5f) + shift;
    if (std::abs(to.y - from.y) < 2.f * kWireLoopClearance)
        y = std::max(from.y, to.y) + kWireLoopClearance + out;
    return Simplified({from, ImVec2(right, from.y), ImVec2(right, y), ImVec2(left, y), ImVec2(left, to.y), to});
}

/// Lanes for wires drawn together, given each one's output and input (in a fixed order, e.g. the graph's
/// wires). A wire gets the lowest lane in which none of its inner runs lies along an inner run of an earlier
/// wire: wires leaving one output, or entering one column, don't draw over each other. The runs out of an
/// output and into an input aren't counted: wires from one output share their first run.
inline auto AssignLanes(const std::vector<std::pair<ImVec2, ImVec2>>& ends) -> std::vector<int>
{
    struct Run
    {
        bool vertical;
        float at;     //!< x of a vertical run, y of a horizontal one.
        float lo, hi; //!< Its extent along the other axis.
    };
    auto runsOf = [](const std::vector<ImVec2>& line) {
        std::vector<Run> runs;
        for (size_t i = 1; i + 2 < line.size(); ++i) // run i goes from point i to i + 1; skip the first and last
        {
            const ImVec2 a = line[i];
            const ImVec2 b = line[i + 1];
            const bool vertical = a.x == b.x;
            runs.push_back({vertical, vertical ? a.x : a.y, vertical ? std::min(a.y, b.y) : std::min(a.x, b.x),
                            vertical ? std::max(a.y, b.y) : std::max(a.x, b.x)});
        }
        return runs;
    };
    auto clash = [](const std::vector<Run>& a, const std::vector<Run>& b) {
        for (const Run& r : a)
        {
            for (const Run& s : b)
            {
                if (r.vertical == s.vertical && std::abs(r.at - s.at) < kWireLaneSpacing && r.lo < s.hi && s.lo < r.hi)
                    return true;
            }
        }
        return false;
    };

    std::vector<int> lanes;
    std::vector<std::vector<Run>> placed;
    for (const auto& [from, to] : ends)
    {
        int lane = 0;
        std::vector<Run> runs = runsOf(RouteWire(from, to, lane));
        for (bool again = true; again && lane < 32;)
        {
            again = false;
            for (const auto& other : placed)
            {
                if (clash(runs, other))
                {
                    runs = runsOf(RouteWire(from, to, ++lane));
                    again = true;
                    break;
                }
            }
        }
        lanes.push_back(lane);
        placed.push_back(std::move(runs));
    }
    return lanes;
}

/// A node's rectangle (canvas units), for wires to route round.
struct WireBox
{
    ImVec2 min{0, 0};
    ImVec2 max{0, 0};

    friend auto operator==(const WireBox& a, const WireBox& b) -> bool
    {
        return a.min.x == b.min.x && a.min.y == b.min.y && a.max.x == b.max.x && a.max.y == b.max.y;
    }
};

/// A wire to route: from an output at `from` to an input at `to`. `fromBox` and `toBox` are the indices of their
/// nodes in the boxes, or -1 for a free end (the pointer, while a wire is dragged).
struct WireEnds
{
    ImVec2 from{0, 0};
    ImVec2 to{0, 0};
    int fromBox{-1};
    int toBox{-1};

    friend auto operator==(const WireEnds& a, const WireEnds& b) -> bool
    {
        return a.from.x == b.from.x && a.from.y == b.from.y && a.to.x == b.to.x && a.to.y == b.to.y && a.fromBox == b.fromBox &&
               a.toBox == b.toBox;
    }
};

namespace wire_detail {

struct Line
{
    float at;     //!< x of a vertical line, y of a horizontal one.
    float weight; //!< Cost per unit of running along it.
};

/// The lines wires may run along on one axis, for boxes spanning `spans` on it: each side of each box and the
/// lanes outside it, and the middle of each gap between sides and the lanes either side of it.
inline void AddBoxLines(std::vector<Line>& lines, const std::vector<std::pair<float, float>>& spans)
{
    std::vector<float> sides;
    for (const auto& [lo, hi] : spans)
    {
        for (int k = 0; k <= kWireSideLanes; ++k)
        {
            const float weight = kWireSideWeight + 0.01f * static_cast<float>(k);
            lines.push_back({lo - static_cast<float>(k) * kWireLaneSpacing, weight});
            lines.push_back({hi + static_cast<float>(k) * kWireLaneSpacing, weight});
        }
        sides.push_back(lo);
        sides.push_back(hi);
    }
    std::sort(sides.begin(), sides.end());
    for (size_t i = 1; i < sides.size(); ++i)
    {
        if (sides[i] - sides[i - 1] < 2.f * kWireLaneSpacing)
            continue;
        const float mid = std::round((sides[i] + sides[i - 1]) * 0.5f);
        for (int k = -kWireSideLanes; k <= kWireSideLanes; ++k)
            lines.push_back({mid + static_cast<float>(k) * kWireLaneSpacing, 1.f + 0.01f * static_cast<float>(std::abs(k))});
    }
}

/// The candidate lines, sorted, each once (the cheapest weight where two coincide).
inline void Sorted(std::vector<Line>& lines, std::vector<float>& at, std::vector<float>& weight)
{
    std::sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.at < b.at || (a.at == b.at && a.weight < b.weight); });
    for (const Line& line : lines)
    {
        if (at.empty() || at.back() != line.at)
        {
            at.push_back(line.at);
            weight.push_back(line.weight);
        }
    }
}

/// The first line at or after `value`.
inline auto IndexOf(const std::vector<float>& lines, float value) -> size_t
{
    return static_cast<size_t>(std::lower_bound(lines.begin(), lines.end(), value) - lines.begin());
}

/// The first line after `value`.
inline auto IndexAfter(const std::vector<float>& lines, float value) -> size_t
{
    return static_cast<size_t>(std::upper_bound(lines.begin(), lines.end(), value) - lines.begin());
}

/// A sparse grid of the lines wires may run along. A step between neighbouring points is blocked when it runs
/// through the inside of a box. A box's edge isn't its inside, so wires may run along it.
struct Grid
{
    std::vector<float> xs, xWeight, ys, yWeight;
    std::vector<uint8_t> blockedH, blockedV; //!< The step right, or down, from each point.
    std::vector<uint16_t> usedH, usedV;      //!< How many wires already take that step.
    std::vector<int> trunk;                  //!< The output whose wires' first run takes the step right, or -1.

    Grid(const std::vector<WireBox>& boxes, std::vector<Line> xLines, std::vector<Line> yLines)
    {
        Sorted(xLines, xs, xWeight);
        Sorted(yLines, ys, yWeight);
        const size_t nx = xs.size();
        blockedH.assign(nx * ys.size(), 0);
        blockedV.assign(nx * ys.size(), 0);
        usedH.assign(nx * ys.size(), 0);
        usedV.assign(nx * ys.size(), 0);
        trunk.assign(nx * ys.size(), -1);
        for (const WireBox& box : boxes)
        {
            const size_t left = IndexOf(xs, box.min.x);
            const size_t right = IndexOf(xs, box.max.x);
            const size_t top = IndexOf(ys, box.min.y);
            const size_t bottom = IndexOf(ys, box.max.y);
            const size_t xInside = IndexAfter(xs, box.min.x);
            const size_t yInside = IndexAfter(ys, box.min.y);
            for (size_t j = yInside; j < bottom; ++j) // horizontal lines strictly inside
            {
                for (size_t i = left; i < right; ++i)
                    blockedH[j * nx + i] = 1;
            }
            for (size_t j = top; j < bottom; ++j) // vertical lines strictly inside
            {
                for (size_t i = xInside; i < right; ++i)
                    blockedV[j * nx + i] = 1;
            }
        }
    }

    [[nodiscard]] auto Point(size_t p) const -> ImVec2 { return {xs[p % xs.size()], ys[p / xs.size()]}; }

    /// The step from point `p` in direction `d` (0 right, 1 left, 2 down, 3 up): its neighbour, length, cost per
    /// unit, and how many other wires take it (a wire from output `source` shares its first run with the others
    /// from that output). False if it's off the grid or blocked.
    auto Step(size_t p, int d, int source, size_t& to, float& length, float& weight, int& shared) const -> bool
    {
        const size_t nx = xs.size();
        const size_t i = p % nx;
        const size_t j = p / nx;
        if (d < 2)
        {
            if (d == 0 ? i + 1 >= nx : i == 0)
                return false;
            const size_t edge = d == 0 ? p : p - 1;
            if (blockedH[edge] != 0)
                return false;
            to = d == 0 ? p + 1 : p - 1;
            length = xs[edge % nx + 1] - xs[edge % nx];
            weight = yWeight[j];
            shared = usedH[edge] + (trunk[edge] >= 0 && trunk[edge] != source ? 1 : 0);
            return true;
        }
        if (d == 2 ? j + 1 >= ys.size() : j == 0)
            return false;
        const size_t edge = d == 2 ? p : p - nx;
        if (blockedV[edge] != 0)
            return false;
        to = d == 2 ? p + nx : p - nx;
        length = ys[edge / nx + 1] - ys[edge / nx];
        weight = xWeight[i];
        shared = usedV[edge];
        return true;
    }

    /// Some step from point `p` isn't blocked.
    [[nodiscard]] auto Open(size_t p) const -> bool
    {
        size_t to = 0;
        float length = 0.f;
        float weight = 0.f;
        int shared = 0;
        for (int d = 0; d < 4; ++d)
        {
            if (Step(p, d, -1, to, length, weight, shared))
                return true;
        }
        return false;
    }

    /// The cheapest path of steps from `start` to `goal` (A*: length, turns, and steps other wires take), as
    /// points, or empty if there's none. `right`: the path leaves `start`, and enters `goal`, heading right.
    /// `source`: the wire's output (see Step).
    auto Search(size_t start, bool startRight, size_t goal, bool goalRight, int source) -> std::vector<size_t>
    {
        if (!Open(start) || !Open(goal))
            return {}; // inside a node's margin: no way out, or in
        // The search's state is a point and the way it's heading. Its buffers are kept from wire to wire, and
        // `seen` says which entries are this search's.
        const size_t states = xs.size() * ys.size() * 4;
        m_cost.resize(states);
        m_parent.resize(states);
        m_seen.resize(states, 0);
        ++m_search;
        auto cost = [&](size_t s) { return m_seen[s] == m_search ? m_cost[s] : std::numeric_limits<float>::max(); };
        auto reach = [&](size_t s, float g, uint32_t from) {
            m_seen[s] = m_search;
            m_cost[s] = g;
            m_parent[s] = from;
        };
        const ImVec2 target = Point(goal);
        // At least the straight-line length and the turns it takes to head for the goal.
        auto estimate = [&](size_t p, int heading) {
            const ImVec2 at = Point(p);
            const float dx = target.x - at.x;
            const float dy = target.y - at.y;
            const bool toward = (heading == 0 && dx > 0.f) || (heading == 1 && dx < 0.f) || (heading == 2 && dy > 0.f) || (heading == 3 && dy < 0.f);
            const bool away = (heading == 0 && dx < 0.f) || (heading == 1 && dx > 0.f) || (heading == 2 && dy < 0.f) || (heading == 3 && dy > 0.f);
            int turns = 0;
            if (dx != 0.f && dy != 0.f)
                turns = away ? 2 : 1;
            else if (dx != 0.f || dy != 0.f)
                turns = toward ? 0 : (away ? 2 : 1);
            return std::abs(dx) + std::abs(dy) + kWireBendCost * static_cast<float>(turns);
        };
        // Cheapest first; then nearest the goal, which keeps the search narrow; then the lower state, so the same
        // graph always gives the same route.
        using Entry = std::tuple<float, float, uint32_t>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
        for (int d = 0; d < 4; ++d)
        {
            if (startRight && d != 0)
                continue;
            reach(start * 4 + static_cast<size_t>(d), 0.f, UINT32_MAX);
            const float h = estimate(start, d);
            open.push({h, h, static_cast<uint32_t>(start * 4 + static_cast<size_t>(d))});
        }
        while (!open.empty())
        {
            const auto [f, h, state] = open.top();
            open.pop();
            const size_t p = state / 4;
            const int heading = static_cast<int>(state % 4);
            const float g = cost(state);
            if (f > g + h + 1e-3f)
                continue; // a cheaper way here was found
            if (p == goal)
            {
                std::vector<size_t> path;
                for (uint32_t s = state; s != UINT32_MAX; s = m_parent[s])
                    path.push_back(s / 4);
                std::reverse(path.begin(), path.end());
                return path;
            }
            for (int d = 0; d < 4; ++d)
            {
                size_t to = 0;
                float length = 0.f;
                float weight = 0.f;
                int shared = 0;
                if (d == (heading ^ 1) || !Step(p, d, source, to, length, weight, shared))
                    continue; // no turning back
                float next = g + length * (weight + kWireShareCost * static_cast<float>(shared)) + (d != heading ? kWireBendCost : 0.f);
                if (to == goal && goalRight && d != 0)
                    next += kWireBendCost; // it turns again into the input
                const size_t s = to * 4 + static_cast<size_t>(d);
                if (next < cost(s))
                {
                    reach(s, next, state);
                    const float rest = estimate(to, d);
                    open.push({next + rest, rest, static_cast<uint32_t>(s)});
                }
            }
        }
        return {};
    }

    std::vector<float> m_cost;
    std::vector<uint32_t> m_parent;
    std::vector<uint32_t> m_seen;
    uint32_t m_search{0};

    /// Counts the steps along `path`, a wire from output `source`, as taken, so later wires keep off them. Its
    /// first run, right from the output, is left to the other wires from that output.
    void Take(const std::vector<size_t>& path, int source)
    {
        bool first = source >= 0;
        for (size_t k = 1; k < path.size(); ++k)
        {
            const size_t a = std::min(path[k - 1], path[k]);
            const bool horizontal = std::max(path[k - 1], path[k]) == a + 1;
            first = first && horizontal && path[k] > path[k - 1];
            if (first && (trunk[a] == -1 || trunk[a] == source))
            {
                trunk[a] = source;
                continue;
            }
            uint16_t& used = horizontal ? usedH[a] : usedV[a];
            used = static_cast<uint16_t>(used + 1);
        }
    }
};

} // namespace wire_detail

/// Every wire in `wires` (in a fixed order, e.g. the graph's), routed round the nodes `boxes` (canvas units):
/// right-angle runs that never pass under a node and keep kWireNodeMargin from every node, its own included.
/// A wire leaves its output to the right and enters its input from the left, through its node's margin. Wires
/// keep off runs earlier wires take, moving to a lane beside them. A wire with no way round (e.g. its input
/// boxed in by overlapping nodes) gets RouteWire's simple route instead. The same arguments give the same points.
inline auto RouteWires(const std::vector<WireBox>& boxes, const std::vector<WireEnds>& wires) -> std::vector<std::vector<ImVec2>>
{
    std::vector<WireBox> grown;
    std::vector<std::pair<float, float>> xSpans;
    std::vector<std::pair<float, float>> ySpans;
    for (const WireBox& box : boxes)
    {
        grown.push_back({ImVec2(box.min.x - kWireNodeMargin, box.min.y - kWireNodeMargin), ImVec2(box.max.x + kWireNodeMargin, box.max.y + kWireNodeMargin)});
        xSpans.emplace_back(grown.back().min.x, grown.back().max.x);
        ySpans.emplace_back(grown.back().min.y, grown.back().max.y);
    }
    // Where each wire joins the grid: out through its output node's margin, in through its input node's.
    auto box = [&](int index) { return index >= 0 && index < static_cast<int>(grown.size()) ? &grown[static_cast<size_t>(index)] : nullptr; };
    std::vector<std::pair<ImVec2, ImVec2>> joins;
    std::vector<wire_detail::Line> xLines;
    std::vector<wire_detail::Line> yLines;
    wire_detail::AddBoxLines(xLines, xSpans);
    wire_detail::AddBoxLines(yLines, ySpans);
    for (const WireEnds& wire : wires)
    {
        const WireBox* out = box(wire.fromBox);
        const WireBox* in = box(wire.toBox);
        joins.emplace_back(out != nullptr ? ImVec2(out->max.x, wire.from.y) : wire.from, in != nullptr ? ImVec2(in->min.x, wire.to.y) : wire.to);
        for (const ImVec2& p : {joins.back().first, joins.back().second})
        {
            xLines.push_back({p.x, 1.f});
            yLines.push_back({p.y, 1.f});
        }
    }
    wire_detail::Grid grid(grown, std::move(xLines), std::move(yLines));

    std::vector<std::vector<ImVec2>> lines(wires.size());
    std::vector<size_t> simple;
    for (size_t w = 0; w < wires.size(); ++w)
    {
        const WireEnds& wire = wires[w];
        const auto [join, leave] = joins[w];
        const bool fromNode = box(wire.fromBox) != nullptr;
        const bool toNode = box(wire.toBox) != nullptr;
        std::vector<size_t> path;
        if ((!fromNode || wire.from.x < join.x) && (!toNode || wire.to.x > leave.x)) // pins inside their nodes
        {
            const auto at = [&](ImVec2 p) { return wire_detail::IndexOf(grid.ys, p.y) * grid.xs.size() + wire_detail::IndexOf(grid.xs, p.x); };
            path = grid.Search(at(join), fromNode, at(leave), toNode, wire.fromBox);
        }
        if (path.empty())
        {
            simple.push_back(w);
            continue;
        }
        grid.Take(path, wire.fromBox);
        std::vector<ImVec2> points;
        if (fromNode)
            points.push_back(wire.from);
        for (size_t p : path)
            points.push_back(grid.Point(p));
        if (toNode)
            points.push_back(wire.to);
        lines[w] = Simplified(points);
    }

    // No way round: the simple route, in lanes of its own.
    std::vector<std::pair<ImVec2, ImVec2>> ends;
    for (size_t w : simple)
        ends.emplace_back(wires[w].from, wires[w].to);
    const std::vector<int> lanes = AssignLanes(ends);
    for (size_t k = 0; k < simple.size(); ++k)
        lines[simple[k]] = RouteWire(ends[k].first, ends[k].second, lanes[k]);
    return lines;
}

/// The distance from `point` to the nearest point of the polyline `line`.
inline auto DistanceToPolyline(ImVec2 point, const std::vector<ImVec2>& line) -> float
{
    float best = std::numeric_limits<float>::max();
    for (size_t i = 0; i + 1 < line.size(); ++i)
    {
        const ImVec2 a = line[i];
        const ImVec2 b = line[i + 1];
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float length = dx * dx + dy * dy;
        const float t = length > 0.f ? std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / length, 0.f, 1.f) : 0.f;
        const float ex = a.x + t * dx - point.x;
        const float ey = a.y + t * dy - point.y;
        best = std::min(best, std::sqrt(ex * ex + ey * ey));
    }
    return best;
}

} // namespace gv::app
