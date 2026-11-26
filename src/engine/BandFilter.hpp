#pragma once

// A Band node's filter: lets one frequency range of stereo audio through. Pure (no audio device, no GL), so
// it's tested on made-up tones. A high-pass at the low end, then a low-pass at the high end, each two
// Butterworth biquads in a row (24 dB per octave). It keeps its state between calls, so audio fed a tick at a
// time comes out the same as fed in one go.

#include <array>
#include <span>
#include <vector>

namespace gv::engine {

class BandFilter
{
public:
    /// Lets `low`..`high` Hz through, at `sampleRate`. Keeps the state, so a range can change while playing.
    void SetRange(double low, double high, int sampleRate);

    /// Filters `stereo` (interleaved left, right samples) into `out`, resized to match.
    void Process(std::span<const float> stereo, std::vector<float>& out);

    /// Back to silence.
    void Reset();

private:
    struct Biquad
    {
        double b0{1.0}, b1{0.0}, b2{0.0}, a1{0.0}, a2{0.0};
    };
    static constexpr int kStages = 4; //!< High-pass twice, then low-pass twice.

    double m_low{-1.0};
    double m_high{-1.0};
    int m_sampleRate{0};
    std::array<Biquad, kStages> m_stages{};
    std::array<bool, kStages> m_on{};                         //!< A low-pass near the top of hearing does nothing.
    std::array<std::array<std::array<double, 2>, kStages>, 2> m_state{}; //!< By channel, stage: z1, z2.
};

} // namespace gv::engine
