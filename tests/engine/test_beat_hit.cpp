#include "engine/BeatHit.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace gv::engine;
using gv::model::BeatHitEvery;
using gv::model::BeatHitSound;

namespace {

auto Running(long long songPulses) -> ClockInput
{
    return {true, true, true, songPulses, songPulses};
}

auto Peak(const std::vector<float>& samples, size_t from, size_t to) -> float
{
    float peak = 0.f;
    for (size_t i = from; i < to; ++i)
        peak = std::max(peak, std::abs(samples[i]));
    return peak;
}

} // namespace

TEST_CASE("beat hits land on the clock's beat lines while it runs")
{
    BeatHitClock clock;
    const long long beat = BeatHitInterval(BeatHitEvery::Beat);
    CHECK(clock.Advance(Running(0), beat)); // Start: the downbeat
    CHECK_FALSE(clock.Advance(Running(1), beat));
    CHECK_FALSE(clock.Advance(Running(23), beat));
    CHECK(clock.Advance(Running(25), beat)); // passed 24 between two frames
    CHECK_FALSE(clock.Advance(Running(26), beat));

    SECTION("a first reading just after Start still hits the downbeat")
    {
        BeatHitClock late;
        CHECK(late.Advance(Running(1), beat));
        CHECK_FALSE(late.Advance(Running(2), beat));
    }
    SECTION("every bar")
    {
        const long long bar = BeatHitInterval(BeatHitEvery::Bar);
        CHECK_FALSE(clock.Advance(Running(48), bar));
        CHECK(clock.Advance(Running(96), bar));
        CHECK_FALSE(clock.Advance(Running(120), bar));
    }
    SECTION("nothing while stopped, and none for a mid-beat restart or jump")
    {
        ClockInput stopped = Running(30);
        stopped.running = false;
        CHECK_FALSE(clock.Advance(stopped, beat));
        CHECK_FALSE(clock.Advance(Running(30), beat)); // resumed mid-beat
        CHECK(clock.Advance(Running(48), beat));
        CHECK_FALSE(clock.Advance(Running(500), beat)); // jumped, off the grid
        CHECK(clock.Advance(Running(504), beat));
    }
}

TEST_CASE("the beat hit synth adds a decaying transient")
{
    for (auto sound : {BeatHitSound::Kick, BeatHitSound::Snare, BeatHitSound::Burst})
    {
        BeatHitSynth synth;
        CHECK_FALSE(synth.Active());
        synth.Trigger(sound, 1.f);
        std::vector<float> samples(48000 * 2, 0.f); // a second of stereo silence
        synth.MixInto(samples.data(), samples.size() / 2, 48000);
        CHECK(Peak(samples, 0, 2400) > 0.3f);              // loud in the first 25 ms
        CHECK(Peak(samples, 48000, samples.size()) == 0.f); // done after half a second
        CHECK_FALSE(synth.Active());
        CHECK(samples[0] == samples[1]); // the same on both channels
    }

    SECTION("strength scales it, and it's clamped")
    {
        BeatHitSynth quiet;
        quiet.Trigger(BeatHitSound::Kick, 0.1f);
        std::vector<float> samples(4800 * 2, 0.f);
        quiet.MixInto(samples.data(), 4800, 48000);
        CHECK(Peak(samples, 0, samples.size()) <= 0.1f);

        BeatHitSynth loud;
        loud.Trigger(BeatHitSound::Burst, 1.f);
        std::vector<float> full(4800 * 2, 0.95f);
        loud.MixInto(full.data(), 4800, 48000);
        CHECK(Peak(full, 0, full.size()) <= 1.f);
    }
}
