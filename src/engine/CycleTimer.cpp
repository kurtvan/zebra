#include "engine/CycleTimer.hpp"

#include <algorithm>

namespace gv::engine {

namespace {

auto Seconds(double seconds) -> CycleTimer::Clock::duration
{
    return std::chrono::duration_cast<CycleTimer::Clock::duration>(std::chrono::duration<double>(seconds));
}

} // namespace

void CycleTimer::Restart(Clock::time_point now)
{
    *this = {};
    m_started = true;
    m_switchAt = now;
}

auto CycleTimer::Tick(const Interval& interval, Clock::time_point now, const ClockInput& clock) -> bool
{
    if (!m_started)
        Restart(now);
    if (interval.unit == model::IntervalUnit::Seconds)
    {
        // Counted from the last change, so an edited interval applies straight away.
        if (now < m_switchAt + Seconds(interval.seconds))
            return false;
        m_switchAt = now;
        return true;
    }

    const long long grid = interval.unit == model::IntervalUnit::Bars ? kPulsesPerBar : kPulsesPerBeat;
    const long long intervalPulses = grid * std::max(1, interval.count);
    const bool running = clock.receiving && clock.running;
    // Ableton's song position when we have it (changes land on its bar lines), else pulses since the clock appeared.
    const bool songGrid = clock.positionKnown;
    const long long pulses = songGrid ? clock.songPulses : clock.totalPulses;
    if (!running)
        return false;
    if (m_switchPulse < 0 || m_songGrid != songGrid || pulses < m_lastPulse || pulses > NextSwitch(m_switchPulse, intervalPulses))
    {
        m_songGrid = songGrid;
        m_switchPulse = NextSwitch(CycleStart(pulses, kPulsesPerBar), intervalPulses);
    }
    m_lastPulse = pulses;
    if (pulses < m_switchPulse)
        return false;
    m_switchPulse = NextSwitch(m_switchPulse, intervalPulses);
    return true;
}

auto NextEntry(int current, int count, bool shuffle, std::mt19937& random) -> int
{
    if (count < 2)
        return -1;
    if (shuffle)
    {
        // Any entry but the current one.
        const int pick = std::uniform_int_distribution<int>(0, count - 2)(random);
        return pick >= current ? pick + 1 : pick;
    }
    return (current + 1) % count;
}

} // namespace gv::engine
