#include "engine/FileWatcher.hpp"

namespace gv::engine {

auto FileWatcher::Poll(const std::set<std::filesystem::path>& paths) -> std::vector<std::filesystem::path>
{
    std::erase_if(m_modified, [&](const auto& entry) { return !paths.contains(entry.first); });
    std::vector<std::filesystem::path> changed;
    for (const auto& path : paths)
    {
        std::error_code error;
        const auto modified = std::filesystem::last_write_time(path, error);
        if (error)
            continue;
        auto [it, added] = m_modified.try_emplace(path, modified);
        if (!added && it->second != modified)
        {
            it->second = modified;
            changed.push_back(path);
        }
    }
    return changed;
}

} // namespace gv::engine
