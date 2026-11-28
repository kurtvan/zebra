// A scene's audio nodes together: Audio In -> Band -> Envelope, as the engine feeds them each tick.

#include "engine/SceneAudio.hpp"
#include "model/nodes/BandNode.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace gv;
using model::NodeType;

namespace {

constexpr int kRate = 48000;
constexpr size_t kChunk = kRate / 60 * 2;

/// One second of a stereo sine at `hz`, half as loud as it can be.
auto Sine(double hz) -> std::vector<float>
{
    std::vector<float> stereo;
    for (size_t i = 0; i < static_cast<size_t>(kRate); ++i)
    {
        const auto sample = 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kRate));
        stereo.push_back(sample);
        stereo.push_back(sample);
    }
    return stereo;
}

struct Scene
{
    model::Graph graph;
    std::string audio = model::AddNode(graph, NodeType::AudioIn, 0, 0);

    /// A Band set to `preset`, wired from `from`.
    auto Band(const model::BandPreset& preset, const std::string& from) -> std::string
    {
        const std::string id = model::AddNode(graph, NodeType::Band, 0, 0);
        model::ParamsOf<model::BandParams>(*graph.Find(id)) = {preset.low, preset.high};
        REQUIRE(model::Connect(graph, from, id, 0));
        return id;
    }
    /// An Envelope, wired from `from` (empty: nothing).
    auto Envelope(const std::string& from) -> std::string
    {
        const std::string id = model::AddNode(graph, NodeType::Envelope, 0, 0);
        if (!from.empty())
            REQUIRE(model::Connect(graph, from, id, 0));
        return id;
    }
    /// Feeds `stereo` a tick at a time; returns the levels after it.
    auto Feed(const std::vector<float>& stereo) -> std::map<std::string, float>
    {
        for (size_t at = 0; at < stereo.size(); at += kChunk)
            sceneAudio.Process(graph, std::span<const float>(stereo).subspan(at, std::min(kChunk, stereo.size() - at)), kRate);
        return sceneAudio.Levels();
    }
    engine::SceneAudio sceneAudio;
};

} // namespace

TEST_CASE("scene audio: an Envelope behind a bass Band rises for 60 Hz and not for 8 kHz", "[scene-audio]")
{
    for (const double hz : {60.0, 8000.0})
    {
        INFO(hz << " Hz");
        Scene scene;
        const auto bass = scene.Band(model::kBandPresets[0], scene.audio);
        const auto followBass = scene.Envelope(bass);
        const auto followBassToo = scene.Envelope(bass); // two Envelopes off one Band
        const auto treble = scene.Band(model::kBandPresets[2], scene.audio);
        const auto followTreble = scene.Envelope(treble);
        const auto followAll = scene.Envelope(scene.audio);

        const auto levels = scene.Feed(Sine(hz));
        CHECK(levels.at(followAll) > 0.5f);
        CHECK(levels.at(followBassToo) == levels.at(followBass));
        if (hz < 100.0)
        {
            CHECK(levels.at(followBass) > 0.5f);
            CHECK(levels.at(followTreble) < 0.05f);
        }
        else
        {
            CHECK(levels.at(followBass) < 0.05f);
            CHECK(levels.at(followTreble) > 0.5f);
        }
    }
}

TEST_CASE("scene audio: a Band after a Band narrows it; an Envelope with no audio reaching it stays at 0", "[scene-audio]")
{
    Scene scene;
    const auto mid = scene.Band(model::kBandPresets[1], scene.audio);
    const auto narrow = scene.Band({"", 250.0, 600.0}, mid);
    const auto followMid = scene.Envelope(mid);
    const auto followNarrow = scene.Envelope(narrow);
    // Levels as they are, without auto-level raising the little that's left to 1.
    for (const auto& id : {followMid, followNarrow})
        model::ParamsOf<model::EnvelopeParams>(*scene.graph.Find(id)).autoLevel = false;
    const auto loose = model::AddNode(scene.graph, NodeType::Band, 0, 0); // nothing wired into it
    const auto followLoose = scene.Envelope(loose);
    const auto alone = scene.Envelope("");

    const auto levels = scene.Feed(Sine(1500.0));
    CHECK(levels.at(followMid) > 0.4f); // the tone is 0.5 loud
    CHECK(levels.at(followNarrow) < 0.05f);
    CHECK(levels.at(followLoose) == 0.f);
    CHECK(levels.at(alone) == 0.f);

    // A removed Envelope is forgotten.
    model::RemoveNode(scene.graph, followMid);
    CHECK(scene.Feed(Sine(1500.0)).count(followMid) == 0);
}

TEST_CASE("scene audio: a preset hears only what's wired into its Audio input", "[scene-audio]")
{
    Scene scene;
    const auto wired = model::AddNode(scene.graph, NodeType::MilkdropPreset, 0, 0);
    REQUIRE(model::Connect(scene.graph, scene.audio, wired, model::kPresetAudioInput));
    const auto deaf = model::AddNode(scene.graph, NodeType::MilkdropPreset, 0, 0);
    const auto bass = model::AddNode(scene.graph, NodeType::MilkdropPreset, 0, 0);
    REQUIRE(model::Connect(scene.graph, scene.Band(model::kBandPresets[0], scene.audio), bass, model::kPresetAudioInput));
    REQUIRE(model::ValidateGraph(scene.graph) == std::vector<model::GraphError>{{"", "There's no viewport."}});

    // RMS of what each preset hears over a tick of `hz`, after a second of it (so the filters settle).
    auto heard = [&](double hz) {
        const auto tone = Sine(hz);
        scene.Feed(std::vector<float>(tone.begin(), tone.end() - static_cast<long>(kChunk)));
        const std::span<const float> last = std::span<const float>(tone).last(kChunk);
        scene.sceneAudio.Process(scene.graph, last, kRate);
        const auto& audio = scene.sceneAudio.PresetAudio();
        REQUIRE(audio.size() == 3);
        CHECK_FALSE(audio.at(deaf).has_value()); // nothing wired: silence
        REQUIRE(audio.at(wired).has_value());
        CHECK(audio.at(wired)->data() == last.data()); // the input itself
        REQUIRE(audio.at(bass).has_value());
        REQUIRE(audio.at(bass)->size() == last.size());
        double sum = 0.0;
        for (const float sample : *audio.at(bass))
            sum += static_cast<double>(sample) * sample;
        return std::sqrt(sum / static_cast<double>(last.size()));
    };
    CHECK(heard(60.0) > 0.3);   // the tone's RMS is about 0.35
    CHECK(heard(8000.0) < 0.01); // cut by the bass Band

    // A removed preset is forgotten.
    model::RemoveNode(scene.graph, deaf);
    scene.Feed(Sine(60.0));
    CHECK(scene.sceneAudio.PresetAudio().count(deaf) == 0);
}
