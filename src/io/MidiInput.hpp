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
};

/// CoreMIDI implementation: listens to every source and follows sources being added and removed.
auto CreateMidiInput() -> std::unique_ptr<MidiInput>;

} // namespace gv::io
