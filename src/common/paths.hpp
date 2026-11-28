#pragma once

#include <filesystem>
#include <string>

namespace gv::paths {

/// ~/Library/Application Support/<app name>/ (created if missing).
auto ApplicationSupportDir() -> std::filesystem::path;

/// ~/Library/Logs/<app name>/ (created if missing).
auto LogsDir() -> std::filesystem::path;

/// Copies the settings and logs folders from the app's old name (Govtech Visualizer) to Zebra's, once: only
/// a folder whose old copy exists and whose new one doesn't. The old folders are never deleted. Call it before
/// ApplicationSupportDir() or LogsDir(), which create the new folders. Returns one line for the log saying what
/// was copied or what failed, or an empty string when there was nothing to do.
auto MigrateLegacyFolders() -> std::string;

} // namespace gv::paths
