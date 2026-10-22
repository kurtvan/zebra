#include "io/MidiInput.hpp"

#include <catch2/catch_test_macros.hpp>

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
