#include "thumbnails/SlotPlan.hpp"

#include <catch2/catch_test_macros.hpp>

using gv::thumbnails::PlanSlots;
using Ids = std::vector<std::string>;

TEST_CASE("PlanSlots: a tile no list wants is destroyed")
{
    const auto plan = PlanSlots({{"a", true}, {"b", false}}, {"a"}, {});
    CHECK(plan.destroy == Ids{"b"});
    CHECK(plan.stop.empty());
    CHECK(plan.create.empty());
}

TEST_CASE("PlanSlots: a paused tile keeps its slot but stops rendering")
{
    const auto plan = PlanSlots({{"a", true}}, {}, {"a"});
    CHECK(plan.destroy.empty());
    CHECK(plan.stop == Ids{"a"});
}

TEST_CASE("PlanSlots: a paused tile that isn't running is left as it is")
{
    const auto plan = PlanSlots({{"a", false}}, {}, {"a"});
    CHECK(plan.destroy.empty());
    CHECK(plan.stop.empty());
    CHECK(plan.create.empty());
}

TEST_CASE("PlanSlots: a paused tile shown again keeps its slot, so it can start rendering again")
{
    const auto plan = PlanSlots({{"a", false}}, {"a"}, {});
    CHECK(plan.destroy.empty());
    CHECK(plan.stop.empty());
    CHECK(plan.create.empty());
}

TEST_CASE("PlanSlots: a tile both live and kept stays live")
{
    const auto plan = PlanSlots({{"a", true}}, {"a"}, {"a"});
    CHECK(plan.destroy.empty());
    CHECK(plan.stop.empty());
}

TEST_CASE("PlanSlots: new tiles get slots, live first, once each")
{
    const auto plan = PlanSlots({{"a", true}}, {"a", "b", "c"}, {"d", "b"});
    CHECK(plan.create == Ids{"b", "c", "d"});
}
