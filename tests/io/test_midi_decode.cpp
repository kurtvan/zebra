#include "io/MidiInput.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace gv::io;

namespace {

/// MIDI 1.0 channel voice message as a UMP word (message type 0x2, group 0).
constexpr auto Midi1(uint8_t status, uint8_t data1, uint8_t data2) -> uint32_t
{
    return (0x2u << 28) | (static_cast<uint32_t>(status) << 16) | (static_cast<uint32_t>(data1) << 8) | data2;
}

} // namespace

TEST_CASE("control changes are decoded with channel, controller and value")
{
    const uint32_t words[] = {Midi1(0xB0, 20, 5), Midi1(0xB3, 7, 127)};
    const auto ccs = DecodeControlChanges(words, 2);
    REQUIRE(ccs.size() == 2);
    CHECK(ccs[0].channel == 0);
    CHECK(ccs[0].controller == 20);
    CHECK(ccs[0].value == 5);
    CHECK(ccs[1].channel == 3);
    CHECK(ccs[1].controller == 7);
    CHECK(ccs[1].value == 127);
}

TEST_CASE("non-CC channel voice messages are ignored")
{
    const uint32_t words[] = {Midi1(0x90, 60, 100), Midi1(0x80, 60, 0), Midi1(0xC0, 5, 0), Midi1(0xB0, 20, 9)};
    const auto ccs = DecodeControlChanges(words, 4);
    REQUIRE(ccs.size() == 1);
    CHECK(ccs[0].value == 9);
}

TEST_CASE("other UMP message types are skipped by their size")
{
    const uint32_t systemRealTime = (0x1u << 28) | (0xF8u << 16); // MIDI clock, 1 word
    const uint32_t sysex7[] = {(0x3u << 28) | (0x16u << 16), 0x01020304u}; // 2 words
    const uint32_t midi2[] = {(0x4u << 28) | (0xB0u << 16) | (20u << 8), 0xFFFFFFFFu}; // MIDI 2.0 CC, 2 words: not decoded
    const uint32_t words[] = {systemRealTime, sysex7[0], sysex7[1], midi2[0], midi2[1], Midi1(0xB1, 20, 64)};
    const auto ccs = DecodeControlChanges(words, 6);
    REQUIRE(ccs.size() == 1);
    CHECK(ccs[0].channel == 1);
    CHECK(ccs[0].value == 64);
}

TEST_CASE("UMP sizes follow the message type")
{
    CHECK(UmpWordCount(0x2u << 28) == 1);
    CHECK(UmpWordCount(0x3u << 28) == 2);
    CHECK(UmpWordCount(0x5u << 28) == 4);
    CHECK(UmpWordCount(0xBu << 28) == 3);
    CHECK(UmpWordCount(0xFu << 28) == 4);
}

TEST_CASE("data bytes are masked to 7 bits")
{
    const uint32_t words[] = {Midi1(0xB0, 0xFF, 0xFF)};
    const auto ccs = DecodeControlChanges(words, 1);
    REQUIRE(ccs.size() == 1);
    CHECK(ccs[0].controller == 127);
    CHECK(ccs[0].value == 127);
}

namespace {

constexpr auto System(uint8_t status, uint8_t data1 = 0, uint8_t data2 = 0) -> uint32_t
{
    return (0x1u << 28) | (static_cast<uint32_t>(status) << 16) | (static_cast<uint32_t>(data1) << 8) | data2;
}

} // namespace

TEST_CASE("system messages are decoded beside control changes")
{
    const uint32_t words[] = {System(0xF8), Midi1(0xB0, 20, 3), System(0xFA), System(0xF2, 0x10, 0x01)};
    const auto messages = DecodeSystemMessages(words, 4);
    REQUIRE(messages.size() == 3);
    CHECK(messages[0].status == 0xF8);
    CHECK(messages[1].status == 0xFA);
    CHECK(messages[2].status == 0xF2);
    CHECK(messages[2].data1 == 0x10);
    CHECK(messages[2].data2 == 0x01);
    CHECK(DecodeControlChanges(words, 4).size() == 1);
}

TEST_CASE("the clock tracker follows tempo, transport and position")
{
    ClockTracker tracker;
    double t = 10.0;
    const double pulse = 60.0 / 128.0 / 24.0; // 128 BPM

    CHECK_FALSE(tracker.State(t).receiving);
    tracker.OnMessage({0xFA}, t); // start
    for (int i = 0; i < 24 * 4 * 2 + 24 + 12; ++i) // 2 bars, 1.5 beats
    {
        t += pulse;
        tracker.OnMessage({0xF8}, t);
    }
    auto state = tracker.State(t);
    CHECK(state.receiving);
    CHECK(state.running);
    CHECK(std::abs(state.bpm - 128.0) < 0.01);
    CHECK(state.Bar() == 3);
    CHECK(state.BeatInBar() == 2);
    CHECK(std::abs(state.Beats() - 9.5) < 1e-9);

    tracker.OnMessage({0xFC}, t); // stop: position holds
    t += pulse;
    tracker.OnMessage({0xF8}, t);
    CHECK_FALSE(tracker.State(t).running);
    CHECK(tracker.State(t).pulses == state.pulses);

    tracker.OnMessage({0xF2, 32, 0}, t); // song position: 32 sixteenths = 2 bars
    tracker.OnMessage({0xFB}, t);         // continue
    t += pulse;
    tracker.OnMessage({0xF8}, t);
    CHECK(tracker.State(t).running);
    CHECK(tracker.State(t).Bar() == 3);

    CHECK_FALSE(tracker.State(t + 1.0).receiving); // no pulses for a second
}

TEST_CASE("pulses without a Start mean playing, position unknown")
{
    ClockTracker tracker;
    double t = 0.0;
    for (int i = 0; i < 48; ++i)
    {
        t += 60.0 / 120.0 / 24.0;
        tracker.OnMessage({0xF8}, t);
    }
    const auto state = tracker.State(t);
    CHECK(state.running);
    CHECK_FALSE(state.positionKnown);
    CHECK(state.totalPulses == 48);
    CHECK(std::abs(state.bpm - 120.0) < 0.01);
    tracker.OnMessage({0xFA}, t);
    CHECK(tracker.State(t).positionKnown);
}

TEST_CASE("a pause between pulses doesn't count as tempo")
{
    ClockTracker tracker;
    double t = 0.0;
    const double pulse = 60.0 / 100.0 / 24.0;
    for (int i = 0; i < 30; ++i)
    {
        t += pulse;
        tracker.OnMessage({0xF8}, t);
    }
    t += 3.0; // transport paused
    for (int i = 0; i < 30; ++i)
    {
        t += pulse;
        tracker.OnMessage({0xF8}, t);
    }
    CHECK(std::abs(tracker.State(t).bpm - 100.0) < 0.01);
}

TEST_CASE("the clock follows one source and ignores a second one while the first is active")
{
    ClockTracker tracker;
    double t = 0.0;
    const double pulse = 60.0 / 120.0 / 24.0;
    tracker.OnMessage({0xFA}, t, 1);
    for (int i = 0; i < 96; ++i)
    {
        t += pulse;
        tracker.OnMessage({0xF8}, t, 1);
        tracker.OnMessage({0xF8}, t + pulse * 0.5, 2); // a second source's clock, interleaved
    }
    auto state = tracker.State(t);
    CHECK(state.pulses == 96); // not 192
    CHECK(std::abs(state.bpm - 120.0) < 0.01);
    CHECK(tracker.Source() == 1);

    // Source 1 goes quiet; source 2 takes over after a second.
    t += 1.5;
    tracker.OnMessage({0xF8}, t, 2);
    CHECK(tracker.Source() == 2);
}
