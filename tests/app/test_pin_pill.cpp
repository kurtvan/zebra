#include "app/PinPill.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using Catch::Approx;
using gv::app::AttachPoints;
using gv::app::AttachSlots;
using gv::app::kPinDotRadius;
using gv::app::kPinPointSpacing;
using gv::app::PillAxis;
using gv::app::PinPill;
using gv::app::PinRowHeight;
using gv::app::PinSide;

TEST_CASE("One wire attaches at the pin's centre, and the pin stays a dot")
{
    const ImVec2 c(100.f, 50.f);
    for (const int count : {0, 1})
    {
        INFO(count << " wires");
        const auto points = AttachPoints(c, count, PinSide::Right);
        REQUIRE(points.size() == 1);
        CHECK(points[0].x == c.x);
        CHECK(points[0].y == c.y);
        const auto dot = PinPill(c, count, PinSide::Right);
        CHECK(dot.min.x == c.x - kPinDotRadius);
        CHECK(dot.max.x == c.x + kPinDotRadius);
        CHECK(dot.min.y == c.y - kPinDotRadius);
        CHECK(dot.max.y == c.y + kPinDotRadius);
        CHECK(dot.rounding == kPinDotRadius); // a circle
    }
}

TEST_CASE("Wires at a side pin attach evenly spaced down it, centred on the pin")
{
    const ImVec2 c(100.f, 50.f);
    for (const PinSide side : {PinSide::Left, PinSide::Right})
    {
        for (const int count : {2, 3, 4, 7})
        {
            INFO(count << " wires");
            const auto points = AttachPoints(c, count, side);
            REQUIRE(points.size() == static_cast<size_t>(count));
            float sum = 0.f;
            for (size_t k = 0; k < points.size(); ++k)
            {
                CHECK(points[k].x == c.x);
                if (k > 0)
                    CHECK(points[k].y - points[k - 1].y == Approx(kPinPointSpacing));
                sum += points[k].y;
            }
            CHECK(sum / static_cast<float>(count) == Approx(c.y));
        }
    }
}

TEST_CASE("A shared pin is a pill: tall on a node's side, wide on its top or bottom, round ends round its points")
{
    CHECK(PillAxis(PinSide::Left).y == 1.f);
    CHECK(PillAxis(PinSide::Right).y == 1.f);
    CHECK(PillAxis(PinSide::Top).x == 1.f);
    CHECK(PillAxis(PinSide::Bottom).x == 1.f);

    const ImVec2 c(0.f, 0.f);
    const auto tall = PinPill(c, 3, PinSide::Right);
    CHECK(tall.max.x - tall.min.x == Approx(2.f * kPinDotRadius));
    CHECK(tall.max.y - tall.min.y == Approx(2.f * kPinPointSpacing + 2.f * kPinDotRadius));
    CHECK(tall.rounding == Approx((tall.max.x - tall.min.x) * 0.5f)); // fully round ends: a stadium, not an ellipse
    const auto points = AttachPoints(c, 3, PinSide::Right);
    CHECK(points.front().y - tall.min.y == Approx(kPinDotRadius)); // the end points sit in the round ends' centres
    CHECK(tall.max.y - points.back().y == Approx(kPinDotRadius));

    const auto wide = PinPill(c, 3, PinSide::Top);
    CHECK(wide.max.y - wide.min.y == Approx(2.f * kPinDotRadius));
    CHECK(wide.max.x - wide.min.x == Approx(2.f * kPinPointSpacing + 2.f * kPinDotRadius));
    const auto across = AttachPoints(c, 3, PinSide::Top);
    CHECK(across[0].y == 0.f);
    CHECK(across[1].x - across[0].x == Approx(kPinPointSpacing));
}

TEST_CASE("Wires attach in the order of their other ends: higher ones higher, ties broken the same way every time")
{
    // Other ends at y 300, 100, 200: the wire to y 100 attaches first (top), then 200, then 300.
    const std::vector<ImVec2> others = {ImVec2(500.f, 300.f), ImVec2(500.f, 100.f), ImVec2(500.f, 200.f)};
    CHECK(AttachSlots(others, PinSide::Right) == std::vector<int>{2, 0, 1});

    // Level other ends: the nearer one (lower x) first, then the order given.
    const std::vector<ImVec2> level = {ImVec2(600.f, 100.f), ImVec2(400.f, 100.f), ImVec2(600.f, 100.f)};
    CHECK(AttachSlots(level, PinSide::Left) == std::vector<int>{1, 0, 2});
    CHECK(AttachSlots(level, PinSide::Left) == AttachSlots(level, PinSide::Left));

    // A wide pill orders by x.
    CHECK(AttachSlots({ImVec2(300.f, 0.f), ImVec2(100.f, 50.f)}, PinSide::Bottom) == std::vector<int>{1, 0});
    CHECK(AttachSlots({ImVec2(1.f, 2.f)}, PinSide::Right) == std::vector<int>{0});
}

TEST_CASE("A row grows with its pin's pill, only while several wires meet there")
{
    const float base = 17.f;
    CHECK(PinRowHeight(base, 0, PinSide::Left) == base);
    CHECK(PinRowHeight(base, 1, PinSide::Left) == base);
    CHECK(PinRowHeight(base, 2, PinSide::Left) == Approx(base + kPinPointSpacing));
    CHECK(PinRowHeight(base, 4, PinSide::Right) == Approx(base + 3.f * kPinPointSpacing));
    CHECK(PinRowHeight(base, 4, PinSide::Top) == base); // a wide pill doesn't make the row taller
    // The pill always fits its row, with the dot's room to spare.
    for (const int count : {2, 3, 6})
    {
        const auto pill = PinPill(ImVec2(0.f, 0.f), count, PinSide::Left);
        CHECK(PinRowHeight(base, count, PinSide::Left) - (pill.max.y - pill.min.y) == Approx(base - 2.f * kPinDotRadius));
    }
}
