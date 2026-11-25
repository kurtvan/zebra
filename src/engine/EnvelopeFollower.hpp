#pragma once

// An Envelope node's level: how loud the audio it follows is, from 0 to 1. Pure (no audio device, no GL), so
// it's tested on made-up signals; the engine feeds it each tick's samples (the whole range from Audio In
// today, a Band's filtered audio later).

#include "model/Graph.hpp"

#include <span>

namespace gv::engine {

constexpr float kAutoLevelSeconds = 3.0f;    //!< Auto-level: how fast the loudest level it remembers fades.
constexpr float kAutoLevelFloor = 0.01f;     //!< Auto-level: audio quieter than this (about -40 dB) isn't raised to 1.

class EnvelopeFollower
{
public:
    /// The node's settings: attack, release, gain, threshold and auto-level. Takes effect from the next Process.
    void SetParams(const model::EnvelopeParams& params)
    {
        m_params = params;
    }

    /// Follows `stereo` (interleaved left, right samples at `sampleRate`) and returns the level after it:
    /// 0..1. Empty: the level stays as it was.
    auto Process(std::span<const float> stereo, int sampleRate) -> float;

    /// The level after the last Process.
    auto Level() const -> float
    {
        return m_level;
    }

    /// Back to silence, forgetting the loudest level heard.
    void Reset();

private:
    model::EnvelopeParams m_params;
    float m_envelope{0.f};                //!< The audio's amplitude, smoothed by attack and release.
    float m_reference{kAutoLevelFloor};   //!< Auto-level: the loudest envelope of the last few seconds.
    float m_level{0.f};
};

} // namespace gv::engine
