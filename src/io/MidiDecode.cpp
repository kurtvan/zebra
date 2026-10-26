#include "io/MidiInput.hpp"

#include <algorithm>

namespace gv::io {

auto UmpWordCount(uint32_t firstWord) -> size_t
{
    // UMP message sizes by message type (high nibble), from the UMP specification.
    static constexpr size_t kWords[16] = {1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4};
    return kWords[(firstWord >> 28) & 0xF];
}

auto DecodeControlChanges(const uint32_t* words, size_t wordCount) -> std::vector<ControlChange>
{
    std::vector<ControlChange> result;
    size_t i = 0;
    while (i < wordCount)
    {
        const uint32_t word = words[i];
        const uint32_t messageType = (word >> 28) & 0xF;
        if (messageType == 0x2)
        {
            const uint32_t status = (word >> 16) & 0xFF;
            if ((status & 0xF0) == 0xB0)
            {
                result.push_back({static_cast<int>(status & 0x0F),
                                  static_cast<int>((word >> 8) & 0x7F),
                                  static_cast<int>(word & 0x7F)});
            }
        }
        i += UmpWordCount(word);
    }
    return result;
}

auto DecodeSystemMessages(const uint32_t* words, size_t wordCount) -> std::vector<SystemMessage>
{
    std::vector<SystemMessage> result;
    size_t i = 0;
    while (i < wordCount)
    {
        const uint32_t word = words[i];
        if (((word >> 28) & 0xF) == 0x1)
        {
            result.push_back({static_cast<uint8_t>((word >> 16) & 0xFF), static_cast<uint8_t>((word >> 8) & 0x7F),
                              static_cast<uint8_t>(word & 0x7F)});
        }
        i += UmpWordCount(word);
    }
    return result;
}

void ClockTracker::OnMessage(const SystemMessage& message, double seconds)
{
    switch (message.status)
    {
        case 0xF8: // timing clock
            if (m_lastPulse >= 0.0)
            {
                const double interval = seconds - m_lastPulse;
                // Ignore gaps (transport stopped, cable replugged): they aren't tempo.
                if (interval > 0.0 && interval < 0.25)
                {
                    m_intervals[m_intervalNext] = interval;
                    m_intervalNext = (m_intervalNext + 1) % kWindow;
                    m_intervalCount = std::min(m_intervalCount + 1, kWindow);
                }
            }
            m_lastPulse = seconds;
            if (m_running)
                m_pulses++;
            break;
        case 0xFA: // start
            m_running = true;
            m_transportSeen = true;
            m_pulses = 0;
            break;
        case 0xFB: // continue
            m_running = true;
            m_transportSeen = true;
            break;
        case 0xFC: // stop
            m_running = false;
            m_transportSeen = true;
            break;
        case 0xF2: // song position pointer, in 16th notes (6 pulses each)
            m_pulses = static_cast<long long>(message.data1 | (message.data2 << 7)) * 6;
            m_transportSeen = true;
            break;
        default:
            break;
    }
}

auto ClockTracker::State(double nowSeconds) const -> ClockState
{
    ClockState state;
    state.receiving = m_lastPulse >= 0.0 && nowSeconds - m_lastPulse < 0.5;
    // Pulses with no Start seen (e.g. the app launched mid-song): playing, position unknown.
    state.running = m_transportSeen ? m_running : state.receiving;
    state.positionKnown = m_transportSeen;
    state.pulses = m_pulses;
    if (m_intervalCount >= 6)
    {
        double sum = 0.0;
        for (int i = 0; i < m_intervalCount; ++i)
            sum += m_intervals[i];
        state.bpm = 60.0 / (sum / m_intervalCount * 24.0);
    }
    return state;
}

} // namespace gv::io
