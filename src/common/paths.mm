#include "common/paths.hpp"

#include "app_info.hpp"
#include "common/FolderMigration.hpp"

#import <Foundation/Foundation.h>

namespace gv::paths {

namespace {

auto UserDirectory(NSSearchPathDirectory directory) -> std::filesystem::path
{
    NSArray<NSURL*>* urls = [[NSFileManager defaultManager] URLsForDirectory:directory inDomains:NSUserDomainMask];
    if (urls.count == 0)
    {
        return std::filesystem::temp_directory_path();
    }
    return std::filesystem::path(urls.firstObject.path.UTF8String);
}

auto EnsureDir(const std::filesystem::path& dir) -> std::filesystem::path
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

auto SupportRoot() -> std::filesystem::path
{
    return UserDirectory(NSApplicationSupportDirectory);
}

auto LogsRoot() -> std::filesystem::path
{
    // There is no NSSearchPathDirectory for ~/Library/Logs.
    return UserDirectory(NSLibraryDirectory) / "Logs";
}

} // namespace

auto ApplicationSupportDir() -> std::filesystem::path
{
    return EnsureDir(SupportRoot() / app_info::kName);
}

auto LogsDir() -> std::filesystem::path
{
    return EnsureDir(LogsRoot() / app_info::kName);
}

auto MigrateLegacyFolders() -> std::string
{
    std::string copied;
    std::string failed;
    for (const auto& root : {SupportRoot(), LogsRoot()})
    {
        const auto oldDir = root / app_info::kLegacyName;
        const auto newDir = root / app_info::kName;
        const auto result = MigrateFolder(oldDir, newDir);
        if (result.outcome == FolderMigration::Copied)
            copied += (copied.empty() ? "" : " and ") + oldDir.string() + " to " + newDir.string();
        else if (result.outcome == FolderMigration::Failed)
            failed += (failed.empty() ? "" : "; ") + result.error;
    }
    std::string line;
    if (!copied.empty())
        line = "copied the " + std::string(app_info::kLegacyName) + " folders: " + copied + " (the old folders are left in place)";
    if (!failed.empty())
        line += (line.empty() ? "" : "; ") + failed;
    return line;
}

} // namespace gv::paths
