#include "engine/EnvelopeFollower.hpp"

#include <algorithm>
#include <cmath>

namespace gv::engine {

namespace {

/// How far a one-pole filter moves toward its target each sample, for time constant `seconds`.
auto Step(float seconds, float sampleRate) -> float
{
    return 1.f - std::exp(-1.f / (std::max(seconds, 1e-4f) * sampleRate));
}

} // namespace

auto EnvelopeFollower::Process(std::span<const float> stereo, int sampleRate) -> float
{
    if (stereo.size() < 2)
        return m_level;
    const float rate = static_cast<float>(sampleRate > 0 ? sampleRate : 48000);
    const float attack = Step(static_cast<float>(m_params.attack), rate);
    const float release = Step(static_cast<float>(m_params.release), rate);
    const float fade = std::exp(-1.f / (kAutoLevelSeconds * rate));
    for (size_t i = 0; i + 1 < stereo.size(); i += 2)
    {
        const float amplitude = std::max(std::abs(stereo[i]), std::abs(stereo[i + 1]));
        m_envelope += (amplitude - m_envelope) * (amplitude > m_envelope ? attack : release);
        m_reference = std::max({m_envelope, m_reference * fade, kAutoLevelFloor});
    }
    float level = std::clamp(m_params.autoLevel ? m_envelope / m_reference : m_envelope * static_cast<float>(m_params.gain), 0.f, 1.f);
    const float threshold = std::clamp(static_cast<float>(m_params.threshold), 0.f, 0.99f);
    level = level < threshold ? 0.f : (level - threshold) / (1.f - threshold);
    m_level = level;
    return m_level;
}

void EnvelopeFollower::Reset()
{
    m_envelope = 0.f;
    m_reference = kAutoLevelFloor;
    m_level = 0.f;
}

} // namespace gv::engine
