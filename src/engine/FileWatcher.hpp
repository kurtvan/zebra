#pragma once

// Notices files that were saved again, by polling their modification times (no FSEvents).

#include <filesystem>
#include <map>
#include <set>
#include <vector>

namespace gv::engine {

/// Remembers when each watched file was last modified, and reports the ones that changed since.
class FileWatcher
{
public:
    /// The files of `paths` whose modification time differs from the last Poll's. A file seen for the first
    /// time isn't a change; a file that's gone isn't either (an editor saving it may be halfway), but when it
    /// comes back with a new time it is. Files not in `paths` are forgotten.
    auto Poll(const std::set<std::filesystem::path>& paths) -> std::vector<std::filesystem::path>;

private:
    std::map<std::filesystem::path, std::filesystem::file_time_type> m_modified;
};

} // namespace gv::engine
