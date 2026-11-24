#include "engine/SwitcherClock.hpp"

#include <algorithm>

namespace gv::engine {

namespace {

auto Duration(double seconds) -> SwitcherClock::Clock::duration
{
    return std::chrono::duration_cast<SwitcherClock::Clock::duration>(std::chrono::duration<double>(seconds));
}

} // namespace

void SwitcherClock::Start(int input, Clock::time_point now)
{
    *this = {};
    m_current = input;
    m_lastChange = now;
}

auto SwitcherClock::PlanNext(const model::SwitcherParams& params, const std::vector<int>& inputs, std::mt19937& random) -> int
{
    const auto among = [&](int input) { return std::find(inputs.begin(), inputs.end(), input) != inputs.end(); };
    if (m_next >= 0 && m_next != m_current && among(m_next))
        return m_next;
    std::vector<int> others;
    for (int input : inputs)
    {
        if (input != m_current)
            others.push_back(input);
    }
    if (others.empty())
        m_next = -1;
    else if (params.shuffle)
        m_next = others[std::uniform_int_distribution<size_t>(0, others.size() - 1)(random)];
    else
    {
        const auto after = std::find_if(others.begin(), others.end(), [&](int input) { return input > m_current; });
        m_next = after != others.end() ? *after : others.front();
    }
    return m_next;
}

auto SwitcherClock::Tick(const model::SwitcherParams& params, bool nextReady, Clock::time_point now, const ClockInput& clock) -> Step
{
    const bool countsBeats = params.unit != model::IntervalUnit::Seconds;
    const long long grid = params.unit == model::IntervalUnit::Bars ? kPulsesPerBar : kPulsesPerBeat;
    const long long intervalPulses = grid * std::max(1, params.intervalCount);
    long long pulses = 0;
    bool due = false;
    if (countsBeats)
    {
        if (!(clock.receiving && clock.running))
            return Step::Hold;
        // Ableton's song position when we have it (changes land on its bar lines), else pulses since the clock appeared.
        const bool songGrid = clock.positionKnown;
        pulses = songGrid ? clock.songPulses : clock.totalPulses;
        if (m_switchPulse < 0 || m_songGrid != songGrid || pulses < m_lastPulse || pulses > NextSwitch(m_switchPulse, intervalPulses))
        {
            m_songGrid = songGrid;
            m_switchPulse = NextSwitch(CycleStart(pulses, kPulsesPerBar), intervalPulses);
        }
        m_lastPulse = pulses;
        due = pulses >= m_switchPulse;
    }
    else
    {
        due = now >= m_lastChange + Duration(params.intervalSeconds);
    }
    if (!due || m_next < 0)
        return Step::Hold;
    if (!nextReady)
    {
        if (countsBeats && pulses > m_switchPulse + kLateSwitchPulses)
        {
            m_switchPulse = NextBarLine(pulses);
            return Step::MissedBarLine;
        }
        return Step::Hold;
    }
    m_current = m_next;
    m_next = -1;
    m_lastChange = now;
    if (countsBeats)
        m_switchPulse = NextSwitch(m_switchPulse, intervalPulses); // stay on the bar lines
    return Step::Switch;
}

auto SwitcherClock::SecondsToNext(const model::SwitcherParams& params, Clock::time_point now) const -> double
{
    return std::max(0.0, std::chrono::duration<double>(m_lastChange + Duration(params.intervalSeconds) - now).count());
}

auto SwitcherClock::BeatsToNext() const -> double
{
    if (m_switchPulse < 0)
        return -1.0;
    return std::max(0.0, static_cast<double>(m_switchPulse - m_lastPulse) / static_cast<double>(kPulsesPerBeat));
}

} // namespace gv::engine
