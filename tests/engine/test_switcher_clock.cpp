#include "engine/RenderPlan.hpp"
#include "engine/SwitcherClock.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>

using gv::engine::ClockInput;
using gv::engine::SwitcherClock;
using gv::model::IntervalUnit;
using gv::model::SwitcherParams;
using Step = SwitcherClock::Step;

namespace {

auto At(double seconds) -> SwitcherClock::Clock::time_point
{
    return SwitcherClock::Clock::time_point{} + std::chrono::duration_cast<SwitcherClock::Clock::duration>(std::chrono::duration<double>(seconds));
}

auto Running(long long songPulses) -> ClockInput
{
    return {true, true, true, songPulses, songPulses};
}

auto Seconds(double seconds, bool shuffle = false) -> SwitcherParams
{
    SwitcherParams params;
    params.unit = IntervalUnit::Seconds;
    params.intervalSeconds = seconds;
    params.shuffle = shuffle;
    return params;
}

auto Counting(IntervalUnit unit, int count) -> SwitcherParams
{
    SwitcherParams params;
    params.unit = unit;
    params.intervalCount = count;
    return params;
}

} // namespace

TEST_CASE("switcher clock: goes through its inputs in order, around, skipping ones it can't use")
{
    std::mt19937 random(1);
    SwitcherClock clock;
    const auto params = Seconds(10);
    clock.Start(0, At(0));
    CHECK(clock.Current() == 0);
    // Inputs 1 and 3 are free (unwired), so they aren't offered.
    const std::vector<int> inputs{0, 2, 4};
    CHECK(clock.PlanNext(params, inputs, random) == 2);
    CHECK(clock.Tick(params, true, At(10), {}) == Step::Switch);
    CHECK(clock.Current() == 2);
    CHECK(clock.Next() == -1);
    CHECK(clock.PlanNext(params, inputs, random) == 4);
    CHECK(clock.Tick(params, true, At(20), {}) == Step::Switch);
    CHECK(clock.PlanNext(params, inputs, random) == 0); // around
}

TEST_CASE("switcher clock: one input, or none, never switches")
{
    std::mt19937 random(1);
    SwitcherClock clock;
    clock.Start(0, At(0));
    CHECK(clock.PlanNext(Seconds(1), {0}, random) == -1);
    CHECK(clock.Tick(Seconds(1), true, At(100), {}) == Step::Hold);
    CHECK(clock.PlanNext(Seconds(1), {}, random) == -1);
    CHECK(clock.Current() == 0);
}

TEST_CASE("switcher clock: shuffled, it never plays the same input twice in a row, and reaches every other")
{
    std::mt19937 random(7);
    SwitcherClock clock;
    const auto params = Seconds(1, true);
    const std::vector<int> inputs{0, 1, 2, 3};
    clock.Start(1, At(0));
    std::set<int> seen;
    for (int i = 1; i <= 200; ++i)
    {
        const int before = clock.Current();
        const int next = clock.PlanNext(params, inputs, random);
        CHECK(next != before);
        REQUIRE(clock.Tick(params, true, At(i), {}) == Step::Switch);
        seen.insert(clock.Current());
    }
    CHECK(seen == std::set<int>{0, 1, 2, 3});
}

TEST_CASE("switcher clock: the planned input stays planned until it's unwired")
{
    std::mt19937 random(3);
    SwitcherClock clock;
    clock.Start(0, At(0));
    const int planned = clock.PlanNext(Seconds(5, true), {0, 1, 2, 3}, random);
    for (int i = 0; i < 20; ++i)
        CHECK(clock.PlanNext(Seconds(5, true), {0, 1, 2, 3}, random) == planned);
    // Unwired: planned again among what's left.
    std::vector<int> left{0, 1, 2, 3};
    std::erase(left, planned);
    const int replanned = clock.PlanNext(Seconds(5, true), left, random);
    CHECK(replanned != planned);
    CHECK(replanned != 0);
}

TEST_CASE("switcher clock: seconds count from the last change, and an edited interval applies straight away")
{
    std::mt19937 random(1);
    SwitcherClock clock;
    const std::vector<int> inputs{0, 1};
    clock.Start(0, At(100));
    clock.PlanNext(Seconds(10), inputs, random);
    CHECK(clock.SecondsToNext(Seconds(10), At(104)) == 6.0);
    CHECK(clock.Tick(Seconds(10), true, At(105), {}) == Step::Hold);
    CHECK(clock.Tick(Seconds(10), true, At(110.02), {}) == Step::Switch);
    clock.PlanNext(Seconds(10), inputs, random);
    CHECK(clock.Tick(Seconds(10), true, At(119), {}) == Step::Hold);
    CHECK(clock.Tick(Seconds(2), true, At(119), {}) == Step::Switch); // shortened: due already
}

TEST_CASE("switcher clock: waits for the next input to be ready")
{
    std::mt19937 random(1);
    SwitcherClock clock;
    const std::vector<int> inputs{0, 1};
    clock.Start(0, At(0));
    clock.PlanNext(Seconds(10), inputs, random);
    CHECK(clock.Tick(Seconds(10), false, At(10), {}) == Step::Hold);
    CHECK(clock.Tick(Seconds(10), false, At(15), {}) == Step::Hold);
    CHECK(clock.Current() == 0);
    CHECK(clock.Tick(Seconds(10), true, At(16), {}) == Step::Switch);
    // The next interval counts from the change itself.
    clock.PlanNext(Seconds(10), inputs, random);
    CHECK(clock.Tick(Seconds(10), true, At(25), {}) == Step::Hold);
    CHECK(clock.Tick(Seconds(10), true, At(26), {}) == Step::Switch);
}

TEST_CASE("switcher clock: beats and bars change on bar lines and hold while the clock is stopped")
{
    std::mt19937 random(1);
    SwitcherClock clock;
    const std::vector<int> inputs{0, 1};
    clock.Start(0, At(0));
    const auto sixBeats = Counting(IntervalUnit::Beats, 6);
    clock.PlanNext(sixBeats, inputs, random);
    // Entered a moment after bar 2 (pulse 96 + 3): counts from that bar line; 6 beats round up to 2 bars.
    CHECK(clock.Tick(sixBeats, true, At(0), Running(99)) == Step::Hold);
    CHECK(clock.BeatsToNext() == (96.0 * 3 - 99) / 24);
    CHECK(clock.Tick(sixBeats, true, At(0), Running(96 * 3 - 1)) == Step::Hold);
    CHECK(clock.Tick(sixBeats, true, At(0), Running(96 * 3)) == Step::Switch);
    clock.PlanNext(sixBeats, inputs, random);
    CHECK(clock.Tick(sixBeats, true, At(0), Running(96 * 4)) == Step::Hold);
    // Stopped: nothing happens, even well past the next change.
    CHECK(clock.Tick(sixBeats, true, At(0), {true, false, true, 96 * 6, 96 * 6}) == Step::Hold);
    CHECK(clock.Tick(sixBeats, true, At(0), {}) == Step::Hold);
    CHECK(clock.Tick(sixBeats, true, At(0), Running(96 * 5)) == Step::Switch);

    // Ableton restarts from the top: the cycle anchors again; one bar changes on the next bar line.
    clock.PlanNext(Counting(IntervalUnit::Bars, 1), inputs, random);
    CHECK(clock.Tick(Counting(IntervalUnit::Bars, 1), true, At(0), Running(0)) == Step::Hold);
    CHECK(clock.Tick(Counting(IntervalUnit::Bars, 1), true, At(0), Running(96)) == Step::Switch);
}

TEST_CASE("switcher clock: before the clock runs, beats and bars have nothing to count")
{
    SwitcherClock clock;
    clock.Start(0, At(0));
    CHECK(clock.BeatsToNext() == -1.0);
    std::mt19937 random(1);
    clock.PlanNext(Counting(IntervalUnit::Bars, 1), {0, 1}, random);
    CHECK(clock.Tick(Counting(IntervalUnit::Bars, 1), true, At(1000), {}) == Step::Hold);
    CHECK(clock.Current() == 0);
}

TEST_CASE("switcher clock: a next input that misses its bar line waits for the next bar line")
{
    std::mt19937 random(1);
    SwitcherClock clock;
    const auto bar = Counting(IntervalUnit::Bars, 1);
    clock.Start(0, At(0));
    clock.PlanNext(bar, {0, 1}, random);
    CHECK(clock.Tick(bar, false, At(0), Running(0)) == Step::Hold);
    CHECK(clock.Tick(bar, false, At(0), Running(96)) == Step::Hold);     // due, not ready, still on the line
    CHECK(clock.Tick(bar, false, At(0), Running(96 + 7)) == Step::MissedBarLine);
    CHECK(clock.Tick(bar, true, At(0), Running(96 + 30)) == Step::Hold); // ready now, but mid-bar
    CHECK(clock.Tick(bar, true, At(0), Running(192)) == Step::Switch);
    CHECK(clock.Current() == 1);
}

TEST_CASE("switcher clock: a jump starts over from the input jumped to, with a whole interval")
{
    std::mt19937 random(1);
    SwitcherClock clock;
    const std::vector<int> inputs{0, 1, 2};
    clock.Start(0, At(0));
    clock.PlanNext(Seconds(10), inputs, random);
    clock.Start(2, At(8)); // jumped to input 2 at 8 s
    CHECK(clock.Current() == 2);
    CHECK(clock.Next() == -1);
    CHECK(clock.PlanNext(Seconds(10), inputs, random) == 0);
    CHECK(clock.Tick(Seconds(10), true, At(12), {}) == Step::Hold);
    CHECK(clock.Tick(Seconds(10), true, At(18), {}) == Step::Switch);
}

TEST_CASE("switcher clock: two switchers in series each keep their own time, and the plan follows both")
{
    using namespace gv;
    using model::NodeType;
    // Two videos through a video switcher, into each of two presets, through a preset switcher.
    model::Graph graph;
    const auto videos = model::AddNode(graph, NodeType::Switcher, 0, 0);
    const auto frames = model::AddNode(graph, NodeType::Switcher, 0, 0);
    std::vector<std::string> sources;
    for (const char* id : {"a", "b"})
    {
        sources.push_back(model::AddNode(graph, NodeType::VideoSource, 0, 0));
        model::ParamsOf<model::VideoSourceParams>(*graph.Find(sources.back())).source.videoId = id;
        model::Connect(graph, sources.back(), videos, static_cast<int>(sources.size()) - 1);
    }
    for (int i = 0; i < 2; ++i)
    {
        const auto preset = model::AddNode(graph, NodeType::MilkdropPreset, 0, 0);
        model::ParamsOf<model::MilkdropPresetParams>(*graph.Find(preset)).path = "/" + std::to_string(i) + ".milk";
        model::Connect(graph, videos, preset, 0);
        model::Connect(graph, preset, frames, i);
    }
    model::Connect(graph, frames, model::AddNode(graph, NodeType::Viewport, 0, 0), 0);
    REQUIRE(model::ValidateGraph(graph).empty());

    std::mt19937 random(1);
    SwitcherClock frameClock;
    SwitcherClock videoClock;
    frameClock.Start(0, At(0));
    videoClock.Start(0, At(0));
    const auto presetsEvery = Seconds(10);
    const auto videosEvery = Seconds(4);
    auto plan = [&] { return engine::CompilePlan(graph, {{frames, frameClock.Current()}, {videos, videoClock.Current()}}); };
    std::vector<std::pair<std::string, std::string>> seen; // preset, video, at every whole second
    for (int second = 1; second <= 12; ++second)
    {
        frameClock.PlanNext(presetsEvery, {0, 1}, random);
        videoClock.PlanNext(videosEvery, {0, 1}, random);
        frameClock.Tick(presetsEvery, true, At(second), {});
        videoClock.Tick(videosEvery, true, At(second), {});
        seen.emplace_back(plan().presetPath.string(), plan().source.videoId);
    }
    CHECK(seen[2] == std::pair<std::string, std::string>{"/0.milk", "a"});  // 3 s
    CHECK(seen[3] == std::pair<std::string, std::string>{"/0.milk", "b"});  // 4 s: the video moves on
    CHECK(seen[7] == std::pair<std::string, std::string>{"/0.milk", "a"});  // 8 s: and again
    CHECK(seen[9] == std::pair<std::string, std::string>{"/1.milk", "a"});  // 10 s: the preset moves on, the video keeps going
    CHECK(seen[11] == std::pair<std::string, std::string>{"/1.milk", "b"}); // 12 s
}
