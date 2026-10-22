#include "common/log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <regex>

TEST_CASE("log lines carry a timestamp, level and category")
{
    const auto line = gv::log::FormatLine(std::chrono::system_clock::now(), gv::log::Level::Warn, "midi", "device removed");

    const std::regex expected(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} WARN  \[midi\] device removed$)");
    CHECK(std::regex_match(line, expected));
}

TEST_CASE("log levels are padded to the same width")
{
    const auto now = std::chrono::system_clock::now();
    const auto info = gv::log::FormatLine(now, gv::log::Level::Info, "a", "x");
    const auto error = gv::log::FormatLine(now, gv::log::Level::Error, "a", "x");
    CHECK(info.size() == error.size());
}
