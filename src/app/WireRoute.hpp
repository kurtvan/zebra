#pragma once

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace gv::app {

constexpr float kWireStub = 16.f;        //!< How far a wire runs straight out of an output, or into an input.
constexpr float kWireLaneSpacing = 6.f;  //!< How far apart wires sharing a run are drawn.
constexpr float kWireLoopClearance = 60.f; //!< How far a backwards wire drops below its ends when they're level.

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
