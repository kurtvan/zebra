#pragma once

#include "engine/BeatGrid.hpp"
#include "model/Performance.hpp"

#include <cstddef>
#include <cstdint>

namespace gv::engine {

/// MIDI clock pulses between beat hits.
auto BeatHitInterval(model::BeatHitEvery every) -> long long;

/// Tells when the MIDI clock passes a beat hit's grid line, from one reading per frame.
///
/// Counts on the song grid when Ableton sends the song position (so "every 2 beats" lands on beats 1 and
/// 3), else from the first pulse received. Only while the clock is running.
class BeatHitClock
{
public:
    /// True if a line `interval` pulses apart was reached since the last call.
    auto Advance(const ClockInput& clock, long long interval) -> bool;
    void Reset();

private:
    long long m_last{-1};
};

/// Synthesises the short transients of beat hits into the audio the visuals hear.
class BeatHitSynth
{
public:
    /// Starts `sound` from its beginning (cutting off one still playing).
    void Trigger(model::BeatHitSound sound, float strength);
    void Stop();
    auto Active() const -> bool
    {
        return m_position >= 0;
    }

    /// Adds the sound to `frames` interleaved stereo samples at `sampleRate`, clamped to -1..1.
    void MixInto(float* stereo, size_t frames, int sampleRate);

private:
    auto Noise() -> float;

    model::BeatHitSound m_sound{model::BeatHitSound::Kick};
    float m_strength{0.f};
    long long m_position{-1}; //!< Samples since the trigger; -1 when silent.
    double m_phase{0.0};
    float m_lowpass{0.f};
    uint32_t m_noise{0x9e3779b9u};
};

} // namespace gv::engine
