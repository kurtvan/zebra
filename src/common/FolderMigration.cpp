#include "common/FolderMigration.hpp"

namespace gv::paths {

auto MigrateFolder(const std::filesystem::path& oldDir, const std::filesystem::path& newDir) -> FolderMigrationResult
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(newDir, ec))
        return {FolderMigration::NewExists, {}};
    if (!fs::is_directory(oldDir, ec))
        return {FolderMigration::NoOldFolder, {}};

    // Copy next to newDir, then rename into place: the rename is the only step that creates newDir.
    auto staging = newDir;
    staging += ".migrating";
    fs::remove_all(staging, ec); // left over from a copy that failed; it is ours, never the old folder
    ec.clear();
    fs::create_directories(newDir.parent_path(), ec);
    ec.clear();
    fs::copy(oldDir, staging, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    if (!ec)
        fs::rename(staging, newDir, ec);
    if (ec)
    {
        std::error_code ignored;
        fs::remove_all(staging, ignored);
        return {FolderMigration::Failed, "could not copy " + oldDir.string() + " to " + newDir.string() + ": " + ec.message()};
    }
    return {FolderMigration::Copied, {}};
}

} // namespace gv::paths
