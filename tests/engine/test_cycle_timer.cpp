#include "engine/CycleTimer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>

using gv::engine::ClockInput;
using gv::engine::CycleTimer;
using gv::engine::NextEntry;
using gv::model::IntervalUnit;

namespace {

auto At(double seconds) -> CycleTimer::Clock::time_point
{
    return CycleTimer::Clock::time_point{} + std::chrono::duration_cast<CycleTimer::Clock::duration>(std::chrono::duration<double>(seconds));
}

auto Running(long long songPulses) -> ClockInput
{
    return {true, true, true, songPulses, songPulses};
}

} // namespace

TEST_CASE("a seconds cycle changes every interval, counted from the last change")
{
    CycleTimer timer;
    const CycleTimer::Interval interval{IntervalUnit::Seconds, 10.0, 0};
    timer.Restart(At(100));
    CHECK_FALSE(timer.Tick(interval, At(105), {}));
    CHECK(timer.Tick(interval, At(110.02), {}));
    CHECK_FALSE(timer.Tick(interval, At(119), {}));
    CHECK(timer.Tick(interval, At(120.05), {}));
    // A shorter interval applies straight away.
    CHECK(timer.Tick({IntervalUnit::Seconds, 2.0, 0}, At(122.1), {}));
}

TEST_CASE("a beat or bar cycle changes on bar lines and holds while the clock is stopped")
{
    CycleTimer timer;
    timer.Restart(At(0));
    const CycleTimer::Interval sixBeats{IntervalUnit::Beats, 0, 6};
    // Entered a moment after bar 2 (pulse 96 + 3): counts from that bar line; 6 beats round up to 2 bars.
    CHECK_FALSE(timer.Tick(sixBeats, At(0), Running(99)));
    CHECK_FALSE(timer.Tick(sixBeats, At(0), Running(96 * 3 - 1)));
    CHECK(timer.Tick(sixBeats, At(0), Running(96 * 3)));
    CHECK_FALSE(timer.Tick(sixBeats, At(0), Running(96 * 4)));
    // Stopped: nothing happens, even well past the next change.
    CHECK_FALSE(timer.Tick(sixBeats, At(0), {true, false, true, 96 * 6, 96 * 6}));
    CHECK_FALSE(timer.Tick(sixBeats, At(0), {}));
    CHECK(timer.Tick(sixBeats, At(0), Running(96 * 5)));

    // Ableton restarts from the top: the cycle anchors again.
    CHECK_FALSE(timer.Tick({IntervalUnit::Bars, 0, 1}, At(0), Running(0)));
    CHECK(timer.Tick({IntervalUnit::Bars, 0, 1}, At(0), Running(96)));
}

TEST_CASE("the next entry goes around in order, or never repeats when shuffling")
{
    std::mt19937 random(1);
    CHECK(NextEntry(0, 3, false, random) == 1);
    CHECK(NextEntry(2, 3, false, random) == 0);
    CHECK(NextEntry(0, 1, false, random) == -1);
    std::set<int> seen;
    for (int i = 0; i < 200; ++i)
    {
        const int next = NextEntry(1, 4, true, random);
        CHECK(next != 1);
        CHECK(next >= 0);
        CHECK(next < 4);
        seen.insert(next);
    }
    CHECK(seen.size() == 3);
}
