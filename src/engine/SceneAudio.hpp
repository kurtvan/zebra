#pragma once

// What one scene's audio nodes measure and its presets hear: each Envelope follows, and each preset hears, the
// audio wired into it, the whole range from Audio In or that audio filtered by every Band on the way. Pure (no audio device, no GL), so a scene's
// wiring is tested on made-up tones; the engine keeps one per scene and feeds it each tick.

#include "engine/BandFilter.hpp"
#include "engine/EnvelopeFollower.hpp"
#include "model/Graph.hpp"
#include "model/nodes/NodeDef.hpp"

#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gv::engine {

class SceneAudio
{
public:
    /// Feeds `stereo` (this tick's interleaved samples at `sampleRate`) through `graph`'s Bands to its
    /// Envelopes and presets. An Envelope with no audio wired in goes back to 0. Nodes no longer in `graph` are
    /// forgotten. `stereo` must outlive the next Process: PresetAudio may point into it.
    /// Bands and Envelopes whose settings Number wires move use them moved by `signals` (the last values).
    void Process(const model::Graph& graph, std::span<const float> stereo, int sampleRate, const model::SignalValues& signals = {});

    /// Each Envelope's level after the last Process, by node id.
    auto Levels() const -> std::map<std::string, float>;

    /// What each preset heard in the last Process, by node id: the input itself, or filtered by the Bands on the
    /// way. Nullopt for a preset with nothing wired into its Audio input: it hears silence. Valid until the next
    /// Process.
    auto PresetAudio() const -> const std::map<std::string, std::optional<std::span<const float>>>&
    {
        return m_presets;
    }

private:
    /// The audio `node` puts out this tick: the input itself for Audio In, filtered for a Band. Nullopt if
    /// nothing reaches it.
    auto AudioOf(const model::Graph& graph, const model::Node* node, int depth) -> std::optional<std::span<const float>>;

    std::map<std::string, EnvelopeFollower> m_followers; //!< By Envelope node id.
    std::map<std::string, BandFilter> m_bands;           //!< By Band node id; each keeps its state across ticks.
    // This tick's.
    std::span<const float> m_input;
    int m_sampleRate{48000};
    const model::SignalValues* m_signals{nullptr};
    std::map<std::string, BandFilter> m_bandsUsed;
    std::map<std::string, std::vector<float>> m_filtered; //!< Each Band's output, filtered once however many follow it.
    std::map<std::string, std::optional<std::span<const float>>> m_presets;
};

} // namespace gv::engine
