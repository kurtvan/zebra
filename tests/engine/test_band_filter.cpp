#include "engine/BandFilter.hpp"
#include "model/nodes/BandNode.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace gv;
using Catch::Matchers::WithinAbs;

namespace {

constexpr int kRate = 48000;
constexpr size_t kChunk = kRate / 60 * 2; //!< What the engine feeds a tick: a 60th of a second, stereo.

/// `seconds` of a stereo sine at `hz`, peak 1.
auto Sine(double hz, double seconds) -> std::vector<float>
{
    std::vector<float> stereo;
    const auto frames = static_cast<size_t>(seconds * kRate);
    for (size_t i = 0; i < frames; ++i)
    {
        const auto sample = static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kRate));
        stereo.push_back(sample);
        stereo.push_back(sample);
    }
    return stereo;
}

/// `stereo` through a filter for `preset`, a tick at a time.
auto Filter(const std::vector<float>& stereo, const model::BandPreset& preset) -> std::vector<float>
{
    engine::BandFilter filter;
    filter.SetRange(preset.low, preset.high, kRate);
    std::vector<float> out;
    std::vector<float> chunk;
    for (size_t at = 0; at < stereo.size(); at += kChunk)
    {
        filter.Process(std::span<const float>(stereo).subspan(at, std::min(kChunk, stereo.size() - at)), chunk);
        out.insert(out.end(), chunk.begin(), chunk.end());
    }
    return out;
}

/// How much of a sine at `hz` comes through `preset`: its peak once the filter has settled (after 0.5 s).
auto Gain(double hz, const model::BandPreset& preset) -> float
{
    const auto out = Filter(Sine(hz, 1.0), preset);
    float peak = 0.f;
    for (size_t i = out.size() / 2; i < out.size(); ++i)
        peak = std::max(peak, std::abs(out[i]));
    return peak;
}

const auto& kBass = model::kBandPresets[0];
const auto& kMid = model::kBandPresets[1];
const auto& kTreble = model::kBandPresets[2];

} // namespace

TEST_CASE("band filter: each tone passes its own band and is cut by the others", "[band-filter]")
{
    // Passes: at least 0.8 of it comes through. Cut: under a tenth.
    CHECK(Gain(60.0, kBass) > 0.8f);
    CHECK(Gain(60.0, kMid) < 0.1f);
    CHECK(Gain(60.0, kTreble) < 0.01f);

    CHECK(Gain(1000.0, kBass) < 0.1f);
    CHECK(Gain(1000.0, kMid) > 0.8f);
    CHECK(Gain(1000.0, kTreble) < 0.1f);

    CHECK(Gain(8000.0, kBass) < 0.01f);
    CHECK(Gain(8000.0, kMid) < 0.1f);
    CHECK(Gain(8000.0, kTreble) > 0.8f);
}

TEST_CASE("band filter: a Band after a Band narrows the range", "[band-filter]")
{
    // Mid, then a hand-set 250-600 Hz: 1.5 kHz passes mid alone but not both.
    engine::BandFilter mid;
    engine::BandFilter narrow;
    mid.SetRange(kMid.low, kMid.high, kRate);
    narrow.SetRange(250.0, 600.0, kRate);
    const auto tone = Sine(1500.0, 0.5);
    std::vector<float> once;
    std::vector<float> twice;
    mid.Process(tone, once);
    narrow.Process(once, twice);
    const auto peak = [](const std::vector<float>& v) {
        float p = 0.f;
        for (size_t i = v.size() / 2; i < v.size(); ++i)
            p = std::max(p, std::abs(v[i]));
        return p;
    };
    CHECK(peak(once) > 0.8f);
    CHECK(peak(twice) < 0.1f);
}

TEST_CASE("band filter: fed a tick at a time, it's the same as fed in one go, with no jump at the edges", "[band-filter]")
{
    const auto tone = Sine(60.0, 0.5);
    const auto chunked = Filter(tone, kBass);
    engine::BandFilter whole;
    whole.SetRange(kBass.low, kBass.high, kRate);
    std::vector<float> once;
    whole.Process(tone, once);
    REQUIRE(chunked.size() == once.size());
    for (size_t i = 0; i < once.size(); ++i)
        REQUIRE_THAT(chunked[i], WithinAbs(once[i], 1e-6));

    // The biggest step from one sample to the next at a chunk's edge is no bigger than anywhere else.
    float inside = 0.f;
    float edges = 0.f;
    for (size_t i = 2; i < chunked.size(); i += 2)
    {
        const float step = std::abs(chunked[i] - chunked[i - 2]);
        float& biggest = i % kChunk == 0 ? edges : inside;
        biggest = std::max(biggest, step);
    }
    CHECK(edges <= inside);

    // Reset: back to silence.
    whole.Reset();
    std::vector<float> silent;
    whole.Process(std::vector<float>(kChunk, 0.f), silent);
    CHECK(std::all_of(silent.begin(), silent.end(), [](float s) { return s == 0.f; }));
}
