#pragma once

#include "engine/BeatGrid.hpp"
#include "model/Performance.hpp"

#include <chrono>
#include <random>

namespace gv::engine {

/// When a playlist moves on: every N seconds, or every N beats or bars of the MIDI clock, on bar lines.
///
/// Beats/bars anchor to the bar lines when the clock starts or jumps (Start, song position), and hold
/// while it's stopped. The video playlists use it; the preset playlists' timing (Engine::UpdatePlaylist)
/// follows the same rules but also waits for the next preset to be ready.
class CycleTimer
{
public:
    using Clock = std::chrono::steady_clock;

    struct Interval
    {
        model::IntervalUnit unit{model::IntervalUnit::Seconds};
        double seconds{30.0};
        int count{4};
    };

    /// Starts the cycle over (the playlist was entered or edited).
    void Restart(Clock::time_point now);

    /// True when it's time to move on; call once per frame. The next interval starts from there.
    auto Tick(const Interval& interval, Clock::time_point now, const ClockInput& clock) -> bool;

private:
    Clock::time_point m_switchAt{}; //!< Seconds: the last change (or the restart).
    long long m_switchPulse{-1}; //!< Beats/bars: when to switch, in the clock's pulses (-1: not anchored yet).
    long long m_lastPulse{-1};
    bool m_songGrid{false};
    bool m_started{false};
};

/// The entry after `current` in a playlist of `count`: the next one around, or a random other one when
/// shuffling. -1 if there is no other entry.
auto NextEntry(int current, int count, bool shuffle, std::mt19937& random) -> int;

} // namespace gv::engine
