#include "app/WireRoute.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

using Catch::Approx;
using gv::app::AssignLanes;
using gv::app::DistanceToPolyline;
using gv::app::kWireStub;
using gv::app::RouteWire;

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
