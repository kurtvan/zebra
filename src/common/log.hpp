#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace gv::log {

enum class Level
{
    Debug,
    Info,
    Warn,
    Error
};

/// Opens the log file in `dir` (rotating the previous one when it exceeds `maxBytes`).
/// Until this is called, messages go to stderr only.
void Init(const std::filesystem::path& dir, std::uintmax_t maxBytes = 5 * 1024 * 1024);

void SetMinLevel(Level level);

/// printf-style. Thread-safe.
void Write(Level level, const char* category, const char* format, ...) __attribute__((format(printf, 3, 4)));

/// Formats one log line, without a trailing newline. Exposed for tests.
auto FormatLine(std::chrono::system_clock::time_point time, Level level, const char* category, const std::string& message) -> std::string;

} // namespace gv::log

#define GV_LOG_DEBUG(category, ...) ::gv::log::Write(::gv::log::Level::Debug, category, __VA_ARGS__)
#define GV_LOG_INFO(category, ...) ::gv::log::Write(::gv::log::Level::Info, category, __VA_ARGS__)
#define GV_LOG_WARN(category, ...) ::gv::log::Write(::gv::log::Level::Warn, category, __VA_ARGS__)
#define GV_LOG_ERROR(category, ...) ::gv::log::Write(::gv::log::Level::Error, category, __VA_ARGS__)
