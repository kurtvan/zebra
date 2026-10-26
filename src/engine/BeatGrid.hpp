#pragma once

namespace gv::engine {

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

} // namespace gv::engine
