#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gv::io {

constexpr int kSceneTriggerController = 20; //!< CC 20 selects the scene.

struct ControlChange
{
    int channel{0};    //!< 0-15
    int controller{0}; //!< 0-127
    int value{0};      //!< 0-127
};

/// A MIDI 1.0 system message (clock, start, stop, song position...) from a UMP word of message type 0x1.
struct SystemMessage
{
    uint8_t status{0}; //!< 0xF8 clock, 0xFA start, 0xFB continue, 0xFC stop, 0xF2 song position, ...
    uint8_t data1{0};
    uint8_t data2{0};
};

/// Decodes the system messages (message type 0x1) in a sequence of Universal MIDI Packet words.
auto DecodeSystemMessages(const uint32_t* words, size_t wordCount) -> std::vector<SystemMessage>;

/// What the incoming MIDI clock says.
struct ClockState
{
    bool receiving{false}; //!< Clock pulses arrived in the last half second.
    bool running{false};   //!< Between Start/Continue and Stop, or pulses arriving with no transport message seen yet.
    bool positionKnown{false}; //!< Start or a song position was received (bars/beats are meaningful).
    double bpm{0.0};       //!< Tempo from the pulse spacing (24 per beat); 0 until known.
    long long pulses{0};   //!< Pulses since Start (or the song position), 24 per beat.
    long long totalPulses{0}; //!< Every pulse received, never reset (counts beats when the song position is unknown).
    std::string sourceName;   //!< The source being followed (set by the MIDI input).

    /// Position in beats since the start (4/4 assumed for bars).
    auto Beats() const -> double
    {
        return static_cast<double>(pulses) / 24.0;
    }
    auto Bar() const -> long long
    {
        return pulses / (24 * 4) + 1;
    }
    auto BeatInBar() const -> int
    {
        return static_cast<int>((pulses / 24) % 4) + 1;
    }
};

/// Follows MIDI clock, start/stop/continue and song position. Not thread-safe by itself.
///
/// Follows one source at a time: the first that sends clock, until it has been quiet for a second.
/// (Two sources' clocks merged into one count would run at double speed.)
class ClockTracker
{
public:
    /// `seconds` is when the message arrived (any monotonic clock); `source` identifies its MIDI source.
    void OnMessage(const SystemMessage& message, double seconds, uintptr_t source = 0);
    auto State(double nowSeconds) const -> ClockState;
    auto Source() const -> uintptr_t
    {
        return m_source;
    }

private:
    static constexpr int kWindow = 96; //!< Pulses averaged for the tempo (one 4/4 bar).
    bool m_running{false};
    bool m_transportSeen{false}; //!< Start/Continue/Stop/song position received at least once.
    uintptr_t m_source{0};
    double m_lastFromSource{-1.0};
    long long m_pulses{0};
    long long m_totalPulses{0};
    double m_lastPulse{-1.0};
    double m_intervals[kWindow]{};
    int m_intervalCount{0};
    int m_intervalNext{0};
};

/// Decodes the Control Change messages in a sequence of Universal MIDI Packet words.
///
/// Only MIDI 1.0 channel voice messages (UMP message type 0x2) are decoded; everything else is skipped
/// using the UMP size of its message type. Inputs are opened with the MIDI 1.0 protocol, so CoreMIDI has
/// already resolved running status and interleaved real-time bytes.
auto DecodeControlChanges(const uint32_t* words, size_t wordCount) -> std::vector<ControlChange>;

/// Number of 32-bit words in a UMP whose first word is `firstWord`.
auto UmpWordCount(uint32_t firstWord) -> size_t;

/// Scene triggers from MIDI: CC 20 on one channel.
class MidiInput
{
public:
    virtual ~MidiInput() = default;

    /// Channel to listen on, 0-15 (shown to users as 1-16).
    virtual void SetChannel(int channel) = 0;

    /// The CC 20 value received since the last call, if any (most recent wins). Called from the render loop.
    virtual auto TakeSceneTrigger() -> std::optional<int> = 0;

    /// Names of the currently connected sources.
    virtual auto SourceNames() const -> std::vector<std::string> = 0;

    /// Most recent Control Change on any channel/controller, for diagnostics.
    virtual auto LastControlChange() const -> std::optional<ControlChange> = 0;

    /// The incoming MIDI clock (from any source).
    virtual auto Clock() const -> ClockState = 0;
};

/// CoreMIDI implementation: listens to every source and follows sources being added and removed.
auto CreateMidiInput() -> std::unique_ptr<MidiInput>;

} // namespace gv::io
