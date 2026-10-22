#pragma once

#include <filesystem>

namespace gv::paths {

/// ~/Library/Application Support/<app name>/ (created if missing).
auto ApplicationSupportDir() -> std::filesystem::path;

/// ~/Library/Logs/<app name>/ (created if missing).
auto LogsDir() -> std::filesystem::path;

} // namespace gv::paths
