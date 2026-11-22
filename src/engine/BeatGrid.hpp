#pragma once

namespace gv::engine {

/// The incoming MIDI clock, as the engine needs it (set by the app each frame).
struct ClockInput
{
    bool receiving{false};
    bool running{false};
    bool positionKnown{false}; //!< songPulses follows Ableton's song position.
    long long songPulses{0};   //!< Since Start / song position, 24 per beat.
    long long totalPulses{0};  //!< Every pulse received; used when the song position is unknown.
};

constexpr long long kPulsesPerBeat = 24; //!< MIDI clock resolution.
constexpr long long kPulsesPerBar = 96;  //!< 4/4.

/// The grid line a beat- or bar-based interval counts from, for a cycle starting at `pulses`.
///
/// Within a 16th note after a line counts as on it (Ableton launches clips on the line, and the trigger
/// arrives a moment later); further into the beat or bar, the cycle starts at the next line.
constexpr auto CycleStart(long long pulses, long long grid) -> long long
{
    const long long tolerance = kPulsesPerBeat / 4;
    const long long line = (pulses / grid) * grid;
    return pulses - line <= tolerance ? line : line + grid;
}

/// How late a beat/bar switcher may still switch after its bar line (the next entry wasn't ready on
/// the line). Later than this, it waits for the next bar line.
constexpr long long kLateSwitchPulses = kPulsesPerBeat / 4;

/// The first bar line at or after `pulses`.
constexpr auto NextBarLine(long long pulses) -> long long
{
    return (pulses + kPulsesPerBar - 1) / kPulsesPerBar * kPulsesPerBar;
}

/// When a beat/bar switcher switches next: `interval` pulses after `line`, rounded up to a bar line, so
/// presets only change on bars (every 6 beats becomes every 2 bars).
constexpr auto NextSwitch(long long line, long long interval) -> long long
{
    return NextBarLine(line + interval);
}

} // namespace gv::engine
