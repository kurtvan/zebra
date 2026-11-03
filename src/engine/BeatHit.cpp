#include "engine/BeatHit.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gv::engine {

namespace {

/// How late (in pulses, ~20 ms at 120 BPM) the first reading after Start can be and still hit its line.
constexpr long long kLatePulses = 2;

} // namespace

auto BeatHitInterval(model::BeatHitEvery every) -> long long
{
    switch (every)
    {
        case model::BeatHitEvery::Beat:
            return kPulsesPerBeat;
        case model::BeatHitEvery::TwoBeats:
            return 2 * kPulsesPerBeat;
        case model::BeatHitEvery::Bar:
            return kPulsesPerBar;
    }
    return kPulsesPerBeat;
}

auto BeatHitClock::Advance(const ClockInput& clock, long long interval) -> bool
{
    if (!clock.receiving || !clock.running)
    {
        Reset();
        return false;
    }
    const long long pulses = clock.positionKnown ? clock.songPulses : clock.totalPulses;
    bool hit = false;
    if (m_last < 0 || pulses < m_last || pulses - m_last >= interval)
        hit = pulses % interval <= kLatePulses; // started, or jumped (song position): only just after a line
    else
        hit = pulses / interval != m_last / interval;
    m_last = pulses;
    return hit;
}

void BeatHitClock::Reset()
{
    m_last = -1;
}

void BeatHitSynth::Trigger(model::BeatHitSound sound, float strength)
{
    m_sound = sound;
    m_strength = std::clamp(strength, 0.f, 1.f);
    m_position = 0;
    m_phase = 0.0;
    m_lowpass = 0.f;
}

void BeatHitSynth::Stop()
{
    m_position = -1;
}

auto BeatHitSynth::Noise() -> float
{
    // xorshift32: cheap white noise in -1..1.
    m_noise ^= m_noise << 13;
    m_noise ^= m_noise >> 17;
    m_noise ^= m_noise << 5;
    return static_cast<float>(m_noise) / 2147483648.f - 1.f;
}

void BeatHitSynth::MixInto(float* stereo, size_t frames, int sampleRate)
{
    if (m_position < 0 || sampleRate <= 0)
        return;
    constexpr double kTwoPi = 2.0 * std::numbers::pi;
    const double length = m_sound == model::BeatHitSound::Kick ? 0.35 : m_sound == model::BeatHitSound::Snare ? 0.25 : 0.1;
    for (size_t i = 0; i < frames; ++i)
    {
        const double t = static_cast<double>(m_position) / sampleRate;
        if (t >= length)
        {
            m_position = -1;
            return;
        }
        double value = 0.0;
        switch (m_sound)
        {
            case model::BeatHitSound::Kick:
            {
                // A sine falling from 150 to 50 Hz under a fast decay.
                const double frequency = 50.0 + 100.0 * std::exp(-t / 0.03);
                m_phase += kTwoPi * frequency / sampleRate;
                value = std::sin(m_phase) * std::exp(-t / 0.12);
                break;
            }
            case model::BeatHitSound::Snare:
            {
                // High-passed noise plus a short 190 Hz body.
                const float noise = Noise();
                m_lowpass += 0.1f * (noise - m_lowpass);
                value = (noise - m_lowpass) * std::exp(-t / 0.07) + 0.4 * std::sin(kTwoPi * 190.0 * t) * std::exp(-t / 0.05);
                break;
            }
            case model::BeatHitSound::Burst:
                value = Noise() * std::exp(-t / 0.02);
                break;
        }
        const float sample = static_cast<float>(value) * m_strength;
        stereo[i * 2] = std::clamp(stereo[i * 2] + sample, -1.f, 1.f);
        stereo[i * 2 + 1] = std::clamp(stereo[i * 2 + 1] + sample, -1.f, 1.f);
        m_position++;
    }
}

} // namespace gv::engine
