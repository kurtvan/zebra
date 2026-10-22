#include "common/log.hpp"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>

namespace gv::log {

namespace {

std::mutex g_mutex;
std::ofstream g_file;
Level g_minLevel{Level::Info};

auto LevelName(Level level) -> const char*
{
    switch (level)
    {
        case Level::Debug:
            return "DEBUG";
        case Level::Info:
            return "INFO ";
        case Level::Warn:
            return "WARN ";
        case Level::Error:
            return "ERROR";
    }
    return "?????";
}

} // namespace

void Init(const std::filesystem::path& dir, std::uintmax_t maxBytes)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    const auto path = dir / "govtech-visualizer.log";
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && std::filesystem::file_size(path, ec) > maxBytes)
    {
        std::filesystem::rename(path, dir / "govtech-visualizer.1.log", ec);
    }

    g_file.open(path, std::ios::app);
    if (!g_file.is_open())
    {
        std::fprintf(stderr, "log: could not open %s, logging to stderr only\n", path.c_str());
    }
}

void SetMinLevel(Level level)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_minLevel = level;
}

auto FormatLine(std::chrono::system_clock::time_point time, Level level, const char* category, const std::string& message) -> std::string
{
    const auto seconds = std::chrono::system_clock::to_time_t(time);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count() % 1000;

    std::tm local{};
    localtime_r(&seconds, &local);

    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);

    char prefix[96];
    std::snprintf(prefix, sizeof(prefix), "%s.%03d %s [%s] ", stamp, static_cast<int>(millis), LevelName(level), category);
    return prefix + message;
}

void Write(Level level, const char* category, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    va_list argsCopy;
    va_copy(argsCopy, args);
    const int length = std::vsnprintf(nullptr, 0, format, args);
    va_end(args);

    std::string message(length > 0 ? static_cast<size_t>(length) : 0, '\0');
    if (length > 0)
    {
        std::vsnprintf(message.data(), message.size() + 1, format, argsCopy);
    }
    va_end(argsCopy);

    const auto line = FormatLine(std::chrono::system_clock::now(), level, category, message);

    std::lock_guard<std::mutex> lock(g_mutex);
    if (level < g_minLevel)
    {
        return;
    }
    std::fprintf(stderr, "%s\n", line.c_str());
    if (g_file.is_open())
    {
        g_file << line << '\n';
        g_file.flush();
    }
}

} // namespace gv::log
