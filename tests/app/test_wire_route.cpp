#include "app/PinPill.hpp"
#include "app/WireRoute.hpp"
#include "app/nodes/NodeUi.hpp"
#include "model/PerformanceJson.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using Catch::Approx;
using gv::app::AssignLanes;
using gv::app::DistanceToPolyline;
using gv::app::kWireNodeMargin;
using gv::app::kWireStub;
using gv::app::RouteWire;
using gv::app::RouteWires;
using gv::app::WireBox;
using gv::app::WireEnds;

namespace {

/// Every run is horizontal or vertical, and none has zero length.
auto RightAngles(const std::vector<ImVec2>& line) -> bool
{
    for (size_t i = 0; i + 1 < line.size(); ++i)
    {
        const ImVec2 a = line[i];
        const ImVec2 b = line[i + 1];
        const bool horizontal = a.y == b.y && a.x != b.x;
        const bool vertical = a.x == b.x && a.y != b.y;
        if (!horizontal && !vertical)
            return false;
    }
    return true;
}

/// Starts at `from`, ends at `to`, leaves to the right and enters from the left.
void CheckEnds(const std::vector<ImVec2>& line, ImVec2 from, ImVec2 to)
{
    REQUIRE(line.size() >= 2);
    CHECK(line.front().x == from.x);
    CHECK(line.front().y == from.y);
    CHECK(line.back().x == to.x);
    CHECK(line.back().y == to.y);
    CHECK(line[1].y == from.y);
    CHECK(line[1].x > from.x);
    CHECK(line[line.size() - 2].y == to.y);
    CHECK(line[line.size() - 2].x < to.x);
}

auto Same(const std::vector<ImVec2>& a, const std::vector<ImVec2>& b) -> bool
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].x != b[i].x || a[i].y != b[i].y)
            return false;
    }
    return true;
}

constexpr float kNodeW = 220.f; //!< The node size the routing tests use.
constexpr float kNodeH = 120.f;

/// A node at `x`, `y`, its output pin near its right edge and its inputs down its left.
auto Node(float x, float y, float width = kNodeW) -> WireBox { return {ImVec2(x, y), ImVec2(x + width, y + kNodeH)}; }
auto Output(const WireBox& box) -> ImVec2 { return {box.max.x - 6.f, box.min.y + 30.f}; }
auto Input(const WireBox& box, int index = 0) -> ImVec2 { return {box.min.x + 6.f, box.min.y + 30.f + 14.f * static_cast<float>(index)}; }
auto Wire(const std::vector<WireBox>& boxes, int from, int to, int input = 0) -> WireEnds
{
    return {Output(boxes[static_cast<size_t>(from)]), Input(boxes[static_cast<size_t>(to)], input), from, to};
}

/// The gap between the run from `a` to `b` and `box`.
auto Gap(ImVec2 a, ImVec2 b, const WireBox& box) -> float
{
    const float dx = std::max({0.f, box.min.x - std::max(a.x, b.x), std::min(a.x, b.x) - box.max.x});
    const float dy = std::max({0.f, box.min.y - std::max(a.y, b.y), std::min(a.y, b.y) - box.max.y});
    return std::sqrt(dx * dx + dy * dy);
}

/// No run of `line` comes within the margin of any box: not even its own nodes', apart from the runs out of
/// its output and into its input, up to their nodes' margins.
void CheckClear(const std::vector<ImVec2>& line, const std::vector<WireBox>& boxes, const WireEnds& wire)
{
    REQUIRE(line.size() >= 2);
    for (size_t i = 0; i + 1 < line.size(); ++i)
    {
        for (size_t k = 0; k < boxes.size(); ++k)
        {
            ImVec2 a = line[i];
            ImVec2 b = line[i + 1];
            if (i == 0 && static_cast<int>(k) == wire.fromBox)
                a.x = std::max(a.x, boxes[k].max.x + kWireNodeMargin); // the run out of the output
            if (i + 2 == line.size() && static_cast<int>(k) == wire.toBox)
                b.x = std::min(b.x, boxes[k].min.x - kWireNodeMargin); // the run into the input
            if ((i == 0 && static_cast<int>(k) == wire.fromBox) || (i + 2 == line.size() && static_cast<int>(k) == wire.toBox))
            {
                if (a.y != b.y)
                    FAIL("the run out of an output, or into an input, isn't horizontal");
                if (a.x >= b.x)
                    continue; // all of it inside the node's margin
            }
            INFO("run " << i << " (" << a.x << ", " << a.y << ") to (" << b.x << ", " << b.y << "), node " << k);
            CHECK(Gap(a, b, boxes[k]) >= kWireNodeMargin - 1e-3f);
        }
    }
}

/// Some run of `a` lies along a run of `b`. `sameOutput`: their first runs, out of the output they share, may.
auto Overlap(const std::vector<ImVec2>& a, const std::vector<ImVec2>& b, bool sameOutput) -> bool
{
    auto along = [](float p, float q, float r, float s) { return std::min(std::max(p, q), std::max(r, s)) > std::max(std::min(p, q), std::min(r, s)); };
    for (size_t i = 0; i + 1 < a.size(); ++i)
    {
        for (size_t k = 0; k + 1 < b.size(); ++k)
        {
            if (sameOutput && i == 0 && k == 0)
                continue;
            const ImVec2 p = a[i], q = a[i + 1], r = b[k], s = b[k + 1];
            if (p.y == q.y && r.y == s.y && p.y == r.y && along(p.x, q.x, r.x, s.x))
                return true;
            if (p.x == q.x && r.x == s.x && p.x == r.x && along(p.y, q.y, r.y, s.y))
                return true;
        }
    }
    return false;
}

/// `wires` with each one's output end moved to its own point on its output's pill, as the canvas does.
auto OnPills(std::vector<WireEnds> wires) -> std::vector<WireEnds>
{
    std::map<int, std::vector<size_t>> byOutput;
    for (size_t i = 0; i < wires.size(); ++i)
        byOutput[wires[i].fromBox].push_back(i);
    for (const auto& [box, list] : byOutput)
    {
        std::vector<ImVec2> others;
        for (size_t i : list)
            others.push_back(wires[i].to);
        const auto slots = gv::app::AttachSlots(others, gv::app::PinSide::Right);
        const auto points = gv::app::AttachPoints(wires[list[0]].from, static_cast<int>(list.size()), gv::app::PinSide::Right);
        for (size_t k = 0; k < list.size(); ++k)
            wires[list[k]].from = points[static_cast<size_t>(slots[k])];
    }
    return wires;
}

auto Fixture(const char* name) -> std::string
{
    std::ifstream in(std::string(GV_TEST_FIXTURES_DIR) + "/" + name);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

TEST_CASE("A level wire is one straight run")
{
    const auto line = RouteWire(ImVec2(100.f, 50.f), ImVec2(300.f, 50.f), 0);
    REQUIRE(line.size() == 2);
    CheckEnds(line, ImVec2(100.f, 50.f), ImVec2(300.f, 50.f));
    CHECK(RightAngles(line));
}

TEST_CASE("A wire to an input lower down steps down a lane halfway across")
{
    const ImVec2 from(100.f, 50.f);
    const ImVec2 to(300.f, 200.f);
    const auto line = RouteWire(from, to, 0);
    REQUIRE(line.size() == 4);
    CheckEnds(line, from, to);
    CHECK(RightAngles(line));
    CHECK(line[1].x == Approx(200.f));
    CHECK(line[2].x == line[1].x);
    CHECK(line[2].y == to.y);
}

TEST_CASE("A wire to an input higher up steps up")
{
    const ImVec2 from(100.f, 200.f);
    const ImVec2 to(260.f, 40.f);
    const auto line = RouteWire(from, to, 0);
    REQUIRE(line.size() == 4);
    CheckEnds(line, from, to);
    CHECK(RightAngles(line));
    CHECK(line[2].y < line[1].y);
}

TEST_CASE("A wire to an input behind its output loops round")
{
    for (const ImVec2 to : {ImVec2(-200.f, 300.f), ImVec2(-200.f, -300.f), ImVec2(-200.f, 0.f), ImVec2(110.f, 20.f)})
    {
        const ImVec2 from(100.f, 0.f);
        const auto line = RouteWire(from, to, 0);
        REQUIRE(line.size() == 6);
        CheckEnds(line, from, to);
        CHECK(RightAngles(line));
        CHECK(line[1].x == Approx(from.x + kWireStub));
        CHECK(line[4].x == Approx(to.x - kWireStub));
        // The run back left lies between the two ends, or below both when they're about level.
        const float back = line[2].y;
        CHECK(line[3].y == back);
        if (std::abs(to.y - from.y) >= 2.f * gv::app::kWireLoopClearance)
            CHECK((back - from.y) * (back - to.y) < 0.f);
        else
            CHECK(back > std::max(from.y, to.y));
    }
}

TEST_CASE("Lanes move the vertical run, and each lane is different")
{
    const ImVec2 from(0.f, 0.f);
    const ImVec2 to(400.f, 300.f);
    std::vector<float> xs;
    for (int lane = 0; lane < 5; ++lane)
    {
        const auto line = RouteWire(from, to, lane);
        REQUIRE(line.size() == 4);
        CHECK(RightAngles(line));
        CheckEnds(line, from, to);
        for (float x : xs)
            CHECK(line[1].x != x);
        xs.push_back(line[1].x);
    }
    // Backwards wires move their loop too.
    const auto a = RouteWire(ImVec2(300.f, 0.f), ImVec2(0.f, 300.f), 0);
    const auto b = RouteWire(ImVec2(300.f, 0.f), ImVec2(0.f, 300.f), 1);
    CHECK(a[2].y != b[2].y);
    CHECK(a[1].x != b[1].x);
}

TEST_CASE("Routing is deterministic")
{
    const std::vector<std::pair<ImVec2, ImVec2>> ends = {
        {ImVec2(0.f, 0.f), ImVec2(300.f, 100.f)},     {ImVec2(0.f, 0.f), ImVec2(300.f, 250.f)},
        {ImVec2(10.f, 400.f), ImVec2(300.f, 120.f)},  {ImVec2(500.f, 50.f), ImVec2(-100.f, 60.f)},
        {ImVec2(3.5f, 7.25f), ImVec2(91.75f, 33.1f)},
    };
    const auto lanes = AssignLanes(ends);
    CHECK(lanes == AssignLanes(ends));
    for (size_t i = 0; i < ends.size(); ++i)
    {
        const auto first = RouteWire(ends[i].first, ends[i].second, lanes[i]);
        CHECK(Same(first, RouteWire(ends[i].first, ends[i].second, lanes[i])));
        CHECK(RightAngles(first));
    }
}

TEST_CASE("Wires from one output to one column get their own lanes")
{
    const ImVec2 out(0.f, 0.f);
    const auto lanes = AssignLanes({{out, ImVec2(300.f, 100.f)}, {out, ImVec2(300.f, 200.f)}, {out, ImVec2(300.f, 300.f)}});
    REQUIRE(lanes.size() == 3);
    CHECK(lanes[0] == 0);
    CHECK(lanes[1] != lanes[0]);
    CHECK(lanes[2] != lanes[0]);
    CHECK(lanes[2] != lanes[1]);

    // Wires from different outputs into one column, whose vertical runs would overlap.
    const auto into = AssignLanes({{ImVec2(0.f, 0.f), ImVec2(300.f, 200.f)}, {ImVec2(0.f, 100.f), ImVec2(300.f, 300.f)}});
    CHECK(into[0] != into[1]);

    // Wires that don't share a run keep lane 0.
    const auto apart = AssignLanes({{ImVec2(0.f, 0.f), ImVec2(300.f, 100.f)}, {ImVec2(0.f, 500.f), ImVec2(300.f, 600.f)}});
    CHECK(apart == std::vector<int>{0, 0});
}

TEST_CASE("Distance to a routed wire")
{
    const auto line = RouteWire(ImVec2(0.f, 0.f), ImVec2(200.f, 100.f), 0); // (0,0) (100,0) (100,100) (200,100)
    CHECK(DistanceToPolyline(ImVec2(50.f, 3.f), line) == Approx(3.f));
    CHECK(DistanceToPolyline(ImVec2(104.f, 50.f), line) == Approx(4.f));
    CHECK(DistanceToPolyline(ImVec2(150.f, 100.f), line) == Approx(0.f));
    CHECK(DistanceToPolyline(ImVec2(50.f, 50.f), line) == Approx(50.f)); // the corner the curve used to cut
}

TEST_CASE("A wire goes round a node in its way, keeping the margin")
{
    const std::vector<WireBox> boxes = {Node(0.f, 0.f), Node(320.f, -20.f), Node(640.f, 0.f)};
    const WireEnds wire = Wire(boxes, 0, 2);
    const auto lines = RouteWires(boxes, {wire});
    REQUIRE(lines.size() == 1);
    CheckEnds(lines[0], wire.from, wire.to);
    CHECK(RightAngles(lines[0]));
    CheckClear(lines[0], boxes, wire);
    CHECK_FALSE(Same(lines[0], RouteWire(wire.from, wire.to, 0))); // not the straight run under the middle node
}

TEST_CASE("A wire goes round a column of nodes")
{
    std::vector<WireBox> boxes = {Node(0.f, 260.f), Node(800.f, 300.f)};
    for (int row = 0; row < 5; ++row)
        boxes.push_back(Node(400.f, static_cast<float>(row) * 130.f)); // 10 apart: too close for a wire between
    const WireEnds wire = Wire(boxes, 0, 1);
    const auto line = RouteWires(boxes, {wire})[0];
    CheckEnds(line, wire.from, wire.to);
    CHECK(RightAngles(line));
    CheckClear(line, boxes, wire);
    // Over the top of the column or under it.
    const float top = -kWireNodeMargin;
    const float bottom = 4.f * 130.f + kNodeH + kWireNodeMargin;
    CHECK(std::any_of(line.begin(), line.end(), [&](ImVec2 p) { return p.y <= top || p.y >= bottom; }));
}

TEST_CASE("A wire between two nodes keeps the margin from both")
{
    const std::vector<WireBox> boxes = {Node(0.f, 70.f), Node(300.f, 0.f), Node(300.f, 160.f), Node(700.f, 60.f)}; // a 40 gap
    const WireEnds wire = Wire(boxes, 0, 3);
    const auto line = RouteWires(boxes, {wire})[0];
    CheckEnds(line, wire.from, wire.to);
    CHECK(RightAngles(line));
    CheckClear(line, boxes, wire);
    CHECK(line.size() <= 6); // through the gap, not the long way round
}

TEST_CASE("A backwards wire goes round both its nodes")
{
    for (const float y : {0.f, 60.f, 400.f, -400.f})
    {
        const std::vector<WireBox> boxes = {Node(600.f, 0.f), Node(0.f, y)};
        const WireEnds wire = Wire(boxes, 0, 1);
        const auto line = RouteWires(boxes, {wire})[0];
        INFO("input node at y " << y);
        CheckEnds(line, wire.from, wire.to);
        CHECK(RightAngles(line));
        CheckClear(line, boxes, wire);
    }
}

TEST_CASE("Routing round nodes is deterministic, and wires take their own lanes")
{
    const std::vector<WireBox> boxes = {Node(0.f, 0.f), Node(0.f, 300.f), Node(500.f, 100.f), Node(500.f, 400.f), Node(250.f, 600.f)};
    const std::vector<WireEnds> wires = {Wire(boxes, 0, 2, 0), Wire(boxes, 1, 2, 1), Wire(boxes, 0, 3, 0), Wire(boxes, 1, 3, 1),
                                         Wire(boxes, 3, 4, 0), Wire(boxes, 2, 0, 0)};
    const auto lines = RouteWires(boxes, wires);
    const auto again = RouteWires(boxes, wires);
    REQUIRE(lines.size() == wires.size());
    for (size_t i = 0; i < wires.size(); ++i)
    {
        INFO("wire " << i);
        CHECK(Same(lines[i], again[i]));
        CHECK(RightAngles(lines[i]));
        CheckEnds(lines[i], wires[i].from, wires[i].to);
        CheckClear(lines[i], boxes, wires[i]);
        for (size_t k = 0; k < i; ++k)
        {
            INFO("and wire " << k);
            CHECK_FALSE(Overlap(lines[i], lines[k], wires[i].fromBox == wires[k].fromBox)); // wires from one output share the run out of it
        }
    }
}

TEST_CASE("Wires out of a shared output start at points of their own and never share a run")
{
    // One output feeding three nodes: above, level, and below it, plus one behind it.
    const std::vector<WireBox> boxes = {Node(0.f, 200.f), Node(500.f, 0.f), Node(500.f, 200.f), Node(500.f, 420.f), Node(-400.f, 260.f)};
    for (const bool behind : {false, true})
    {
        INFO((behind ? "and one behind" : "three ahead"));
        std::vector<WireEnds> wires = {Wire(boxes, 0, 2), Wire(boxes, 0, 1), Wire(boxes, 0, 3)};
        if (behind)
            wires.push_back(Wire(boxes, 0, 4));
        wires = OnPills(wires);
        const auto lines = RouteWires(boxes, wires);
        REQUIRE(lines.size() == wires.size());
        for (size_t i = 0; i < wires.size(); ++i)
        {
            INFO("wire " << i);
            CHECK(RightAngles(lines[i]));
            CheckEnds(lines[i], wires[i].from, wires[i].to);
            CheckClear(lines[i], boxes, wires[i]);
            for (size_t k = 0; k < i; ++k)
            {
                INFO("and wire " << k);
                CHECK(lines[i].front().y != lines[k].front().y); // a point each
                CHECK_FALSE(Overlap(lines[i], lines[k], false));
            }
        }
        // The highest input takes the top point.
        CHECK(lines[1].front().y < lines[0].front().y);
        CHECK(lines[0].front().y < lines[2].front().y);
    }
}

TEST_CASE("A wire with no way round gets the simple route")
{
    // The input node's left side is covered by another node: no way into the input.
    const std::vector<WireBox> boxes = {Node(0.f, 0.f), Node(500.f, 0.f), Node(400.f, -40.f)};
    const WireEnds wire = Wire(boxes, 0, 1);
    const auto lines = RouteWires(boxes, {wire, Wire(boxes, 0, 2)});
    CHECK(Same(lines[0], RouteWire(wire.from, wire.to, 0)));
    CheckClear(lines[1], boxes, Wire(boxes, 0, 2)); // the other wire still goes round

    // A dragged wire's free end over a node, too.
    const WireEnds dragged{Output(boxes[0]), ImVec2(600.f, 60.f), 0, -1};
    CHECK(Same(RouteWires(boxes, {dragged})[0], RouteWire(dragged.from, dragged.to, 0)));
}

TEST_CASE("A dragged wire's free end routes round nodes too")
{
    const std::vector<WireBox> boxes = {Node(0.f, 0.f), Node(320.f, -20.f), Node(0.f, 400.f)};
    const WireEnds fromOutput{Output(boxes[0]), ImVec2(700.f, 30.f), 0, -1}; // to the pointer, past a node
    const auto a = RouteWires(boxes, {fromOutput})[0];
    CHECK(RightAngles(a));
    CHECK(a.back().x == 700.f);
    CheckClear(a, boxes, fromOutput);
    const WireEnds fromInput{ImVec2(-200.f, 300.f), Input(boxes[2]), -1, 2}; // from the pointer, into an input
    const auto b = RouteWires(boxes, {fromInput})[0];
    CHECK(RightAngles(b));
    CHECK(b.front().x == -200.f);
    CheckClear(b, boxes, fromInput);
}

TEST_CASE("Every wire of the real show goes round every node")
{
    // The saved positions, with each node type's width and a height of 120.
    const auto result = gv::model::ParsePerformance(Fixture("govtech-nov-6th.json"));
    REQUIRE(result.ok);
    int wires = 0;
    for (const auto& scene : result.performance.scenes)
    {
        INFO("scene " << scene.id);
        std::vector<WireBox> boxes;
        std::map<std::string, int> index;
        for (const auto& node : scene.graph.nodes)
        {
            index[node.id] = static_cast<int>(boxes.size());
            boxes.push_back(Node(node.x, node.y, gv::app::Ui(node.type).width));
        }
        std::vector<WireEnds> ends;
        for (const auto& wire : scene.graph.wires)
            ends.push_back(Wire(boxes, index.at(wire.fromNode), index.at(wire.toNode), std::min(wire.toInput, 5)));
        ends = OnPills(ends);
        const auto lines = RouteWires(boxes, ends);
        REQUIRE(lines.size() == ends.size());
        for (size_t i = 0; i < ends.size(); ++i)
        {
            INFO("wire " << scene.graph.wires[i].fromNode << " to " << scene.graph.wires[i].toNode);
            CHECK(RightAngles(lines[i]));
            CheckEnds(lines[i], ends[i].from, ends[i].to);
            CheckClear(lines[i], boxes, ends[i]);
            for (size_t k = 0; k < i; ++k)
            {
                if (ends[k].fromBox == ends[i].fromBox) // wires out of one pill each keep their own runs
                    CHECK_FALSE(Overlap(lines[i], lines[k], false));
            }
        }
        wires += static_cast<int>(ends.size());
    }
    CHECK(wires > 0);
}
