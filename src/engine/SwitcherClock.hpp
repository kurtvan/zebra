#pragma once

#include "engine/BeatGrid.hpp"
#include "model/Graph.hpp"

#include <chrono>
#include <random>
#include <vector>

namespace gv::engine {

/// Times one switcher: the input passing now, the one coming next, and when it moves on.
///
/// Inputs are the switcher's input numbers, which never change when other inputs are wired or unwired.
/// - Order: the next wired input after the current one, around; shuffled, any other one, never the same
///   twice in a row.
/// - Seconds count from the last change, so an edited interval applies straight away.
/// - Beats and bars follow the MIDI clock: they anchor to its bar lines when it starts or jumps (Start, a
///   new song position), hold while it's stopped, and change only on bar lines.
/// - A change waits for the next input to be ready (a preset still preparing). On the beat grid, one that
///   misses its bar line waits for the next bar line rather than changing mid-bar.
/// Pure: no GL, no threads. The engine runs one per switcher of the live scene.
class SwitcherClock
{
public:
    using Clock = std::chrono::steady_clock;

    enum class Step
    {
        Hold,          //!< Nothing to do.
        Switch,        //!< Moved on: Current() is the new input.
        MissedBarLine, //!< Due, but the next input wasn't ready in time: now waiting for the next bar line.
    };

    /// Starts over on `input`: the scene was entered, the switcher jumped, or it was edited.
    void Start(int input, Clock::time_point now);

    /// The input passing now (-1 before Start).
    auto Current() const -> int
    {
        return m_current;
    }

    /// The input planned to come next, or -1.
    auto Next() const -> int
    {
        return m_next;
    }

    /// Plans the next input among `inputs` (the ones it may switch to, ascending), keeping the one already
    /// planned while it's still among them. Returns it, or -1 if there's no other input.
    auto PlanNext(const model::SwitcherParams& params, const std::vector<int>& inputs, std::mt19937& random) -> int;

    /// Once per frame, after PlanNext. `nextReady`: the planned input can be shown now.
    auto Tick(const model::SwitcherParams& params, bool nextReady, Clock::time_point now, const ClockInput& clock) -> Step;

    /// Counting seconds: the time left until the next change (0 when due).
    auto SecondsToNext(const model::SwitcherParams& params, Clock::time_point now) const -> double;

    /// Counting beats or bars: the beats left until the next change, or -1 until the clock has run.
    auto BeatsToNext() const -> double;

private:
    int m_current{-1};
    int m_next{-1};
    Clock::time_point m_lastChange{};
    long long m_switchPulse{-1}; //!< Beats/bars: the pulse to change at; -1 until anchored.
    long long m_lastPulse{-1};
    bool m_songGrid{false};      //!< Anchored on Ableton's song position (else pulses since the clock appeared).
};

} // namespace gv::engine
