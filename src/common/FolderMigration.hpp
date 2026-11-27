#pragma once

#include <filesystem>
#include <string>

namespace gv::paths {

/// What MigrateFolder did.
enum class FolderMigration
{
    Copied,      //!< The old folder was copied to the new one; the old one is left in place.
    NewExists,   //!< The new folder is already there: nothing was copied.
    NoOldFolder, //!< There is no old folder: a fresh start.
    Failed,      //!< The copy failed; the new folder was not created.
};

/// The result of MigrateFolder: what happened, and the error in plain words when it failed.
struct FolderMigrationResult
{
    FolderMigration outcome{FolderMigration::NoOldFolder}; //!< What happened.
    std::string error;                                     //!< Why the copy failed, or empty.
};

/// Copies oldDir (and everything in it) to newDir when oldDir exists and newDir doesn't. Never deletes or
/// changes oldDir. The copy goes to a sibling folder first and is renamed into place at the end, so a copy
/// that fails half way doesn't leave a partial newDir that would stop the next launch from trying again.
auto MigrateFolder(const std::filesystem::path& oldDir, const std::filesystem::path& newDir) -> FolderMigrationResult;

} // namespace gv::paths
