#include "engine/BandFilter.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gv::engine {

void BandFilter::SetRange(double low, double high, int sampleRate)
{
    if (sampleRate <= 0)
        sampleRate = 48000;
    if (low == m_low && high == m_high && sampleRate == m_sampleRate)
        return;
    m_low = low;
    m_high = high;
    m_sampleRate = sampleRate;
    const double nyquist = 0.5 * sampleRate;
    // Butterworth biquads (the Audio EQ Cookbook's, Q = 1/sqrt 2).
    auto design = [&](double hz, bool highPass) {
        const double w0 = 2.0 * std::numbers::pi * std::clamp(hz, 1.0, 0.9 * nyquist) / sampleRate;
        const double c = std::cos(w0);
        const double alpha = std::sin(w0) / std::numbers::sqrt2;
        const double a0 = 1.0 + alpha;
        Biquad q;
        q.b0 = (highPass ? (1.0 + c) : (1.0 - c)) / 2.0 / a0;
        q.b1 = (highPass ? -(1.0 + c) : (1.0 - c)) / a0;
        q.b2 = q.b0;
        q.a1 = -2.0 * c / a0;
        q.a2 = (1.0 - alpha) / a0;
        return q;
    };
    const bool lowPass = high < 0.45 * sampleRate;
    for (int i = 0; i < kStages; ++i)
    {
        const bool highPass = i < 2;
        m_stages[static_cast<size_t>(i)] = design(highPass ? low : high, highPass);
        m_on[static_cast<size_t>(i)] = highPass || lowPass;
    }
}

void BandFilter::Process(std::span<const float> stereo, std::vector<float>& out)
{
    out.resize(stereo.size());
    for (size_t i = 0; i < stereo.size(); ++i)
    {
        auto& channel = m_state[i % 2];
        double x = stereo[i];
        for (size_t s = 0; s < kStages; ++s)
        {
            if (!m_on[s])
                continue;
            const Biquad& q = m_stages[s];
            auto& z = channel[s];
            const double y = q.b0 * x + z[0];
            z[0] = q.b1 * x - q.a1 * y + z[1];
            z[1] = q.b2 * x - q.a2 * y;
            x = y;
        }
        out[i] = static_cast<float>(x);
    }
}

void BandFilter::Reset()
{
    m_state = {};
}

} // namespace gv::engine
