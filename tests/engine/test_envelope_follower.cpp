#include "engine/EnvelopeFollower.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace gv;
using Catch::Matchers::WithinAbs;

namespace {

constexpr int kRate = 48000;

/// `seconds` of stereo audio, `amplitude` loud: a 1 kHz sine, or a square wave (its level never dips, so the
/// envelope's rise and fall are exactly exponential).
auto Tone(double seconds, float amplitude, bool square = false) -> std::vector<float>
{
    std::vector<float> stereo;
    const auto frames = static_cast<size_t>(seconds * kRate);
    for (size_t i = 0; i < frames; ++i)
    {
        const double wave = std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / kRate);
        const float sample = square ? (wave >= 0.0 ? amplitude : -amplitude) : amplitude * static_cast<float>(wave);
        stereo.push_back(sample);
        stereo.push_back(sample);
    }
    return stereo;
}

/// Feeds `stereo` as the engine does, a 60th of a second at a time; returns the level after it.
auto Feed(engine::EnvelopeFollower& follower, const std::vector<float>& stereo) -> float
{
    constexpr size_t kChunk = kRate / 60 * 2;
    for (size_t at = 0; at < stereo.size(); at += kChunk)
        follower.Process(std::span<const float>(stereo).subspan(at, std::min(kChunk, stereo.size() - at)), kRate);
    return follower.Level();
}

auto Manual(double gain, double threshold = 0.0, double attack = 0.01, double release = 0.25) -> model::EnvelopeParams
{
    return {attack, release, gain, threshold, false};
}

} // namespace

TEST_CASE("envelope follower: silence is 0, and after music, silence decays to 0", "[envelope]")
{
    for (bool autoLevel : {false, true})
    {
        INFO("auto-level " << autoLevel);
        engine::EnvelopeFollower follower;
        model::EnvelopeParams params = Manual(1.f);
        params.autoLevel = autoLevel;
        follower.SetParams(params);
        CHECK(Feed(follower, Tone(1.0, 0.f)) == 0.f);
        CHECK(Feed(follower, Tone(0.5, 0.8f)) > 0.7f);
        CHECK(Feed(follower, Tone(3.0, 0.f)) < 0.01f);
    }
    // Nothing to follow: the level stays.
    engine::EnvelopeFollower follower;
    follower.SetParams(Manual(1.f));
    Feed(follower, Tone(0.5, 0.5f));
    const float level = follower.Level();
    CHECK(follower.Process({}, kRate) == level);
    follower.Reset();
    CHECK(follower.Level() == 0.f);
}

TEST_CASE("envelope follower: a burst rises in about the attack time and falls in about the release time", "[envelope]")
{
    engine::EnvelopeFollower follower;
    follower.SetParams(Manual(2.f, 0.f, 0.05f, 0.5f)); // 0.5 loud, times 2: heads for 1
    // One attack in: about 63% of the way (1 - 1/e); five: there.
    CHECK_THAT(Feed(follower, Tone(0.05, 0.5f, true)), WithinAbs(0.632, 0.05));
    CHECK(Feed(follower, Tone(0.20, 0.5f, true)) > 0.98f);
    // One release after it stops: about 37% left (1/e).
    CHECK_THAT(Feed(follower, Tone(0.5, 0.f)), WithinAbs(0.368, 0.05));

    // A longer attack takes longer.
    engine::EnvelopeFollower slow;
    slow.SetParams(Manual(2.f, 0.f, 0.5f, 0.5f));
    CHECK(Feed(slow, Tone(0.05, 0.5f, true)) < 0.15f);
}

TEST_CASE("envelope follower: the threshold gates quiet signals and spreads the rest over 0..1", "[envelope]")
{
    engine::EnvelopeFollower follower;
    follower.SetParams(Manual(1.f, 0.2f));
    CHECK(Feed(follower, Tone(0.5, 0.1f, true)) == 0.f);
    // 0.6 loud: (0.6 - 0.2) / 0.8.
    CHECK_THAT(Feed(follower, Tone(0.5, 0.6f, true)), WithinAbs(0.5, 0.01));
    // Without a threshold, the quiet one shows.
    engine::EnvelopeFollower open;
    open.SetParams(Manual(1.f));
    CHECK_THAT(Feed(open, Tone(0.5, 0.1f, true)), WithinAbs(0.1, 0.01));
}

TEST_CASE("envelope follower: auto-level brings quiet and loud music to a similar range; the gain doesn't", "[envelope]")
{
    model::EnvelopeParams params; // auto-level is on by default
    REQUIRE(params.autoLevel);
    engine::EnvelopeFollower quiet, loud;
    quiet.SetParams(params);
    loud.SetParams(params);
    const float quietLevel = Feed(quiet, Tone(4.0, 0.03f));
    const float loudLevel = Feed(loud, Tone(4.0, 0.8f));
    CHECK(quietLevel > 0.8f);
    CHECK_THAT(quietLevel, WithinAbs(loudLevel, 0.1));

    // A quiet passage right after a loud one is low at first, then rises as the loud one is forgotten.
    const float after = Feed(loud, Tone(1.0, 0.1f));
    CHECK(after < 0.3f);
    CHECK(Feed(loud, Tone(8.0, 0.1f)) > 0.8f);

    // Hiss far below the floor isn't raised to 1.
    engine::EnvelopeFollower hiss;
    hiss.SetParams(params);
    CHECK(Feed(hiss, Tone(4.0, 0.001f)) < 0.15f);

    // Off: the gain sets the level, so the two differ.
    engine::EnvelopeFollower quietManual, loudManual;
    quietManual.SetParams(Manual(1.f));
    loudManual.SetParams(Manual(1.f));
    CHECK(Feed(quietManual, Tone(4.0, 0.03f)) < 0.05f);
    CHECK(Feed(loudManual, Tone(4.0, 0.8f)) > 0.7f);
}
