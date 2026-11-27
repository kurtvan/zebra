#include "app_info.hpp"
#include "common/FolderMigration.hpp"

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using gv::paths::FolderMigration;
using gv::paths::MigrateFolder;

namespace {

/// A temp folder named with the process id, standing in for ~/Library/Application Support; removed afterwards.
struct TempRoot
{
    fs::path dir = fs::temp_directory_path() / ("zebra-migration-test-" + std::to_string(getpid()));
    TempRoot()
    {
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~TempRoot() { fs::remove_all(dir); }
};

void WriteFile(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

auto ReadFile(const fs::path& path) -> std::string
{
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

TEST_CASE("an old settings folder is copied to the new name, and the old one is left in place")
{
    TempRoot root;
    const auto oldDir = root.dir / gv::app_info::kLegacyName;
    const auto newDir = root.dir / gv::app_info::kName;
    WriteFile(oldDir / "settings.json", R"({"recent":["/a/show.json"]})");
    WriteFile(oldDir / "library.json", "library");
    WriteFile(oldDir / "favourites.json", "favourites");
    WriteFile(oldDir / "layout-v2.ini", "layout");
    WriteFile(oldDir / "recovery.json", "recovery");
    WriteFile(oldDir / "thumbnails" / "abc.png", "png");

    const auto result = MigrateFolder(oldDir, newDir);

    CHECK(result.outcome == FolderMigration::Copied);
    CHECK(result.error.empty());
    for (const char* name : {"settings.json", "library.json", "favourites.json", "layout-v2.ini", "recovery.json", "thumbnails/abc.png"})
    {
        INFO(name);
        CHECK(ReadFile(newDir / name) == ReadFile(oldDir / name));
    }
    CHECK(ReadFile(oldDir / "settings.json") == R"({"recent":["/a/show.json"]})");
    CHECK_FALSE(fs::exists(root.dir / (std::string(gv::app_info::kName) + ".migrating")));
}

TEST_CASE("when both folders exist, nothing is copied")
{
    TempRoot root;
    const auto oldDir = root.dir / "old";
    const auto newDir = root.dir / "new";
    WriteFile(oldDir / "settings.json", "old");
    WriteFile(newDir / "settings.json", "new");

    CHECK(MigrateFolder(oldDir, newDir).outcome == FolderMigration::NewExists);
    CHECK(ReadFile(newDir / "settings.json") == "new");
    CHECK(ReadFile(oldDir / "settings.json") == "old");
}

TEST_CASE("when neither folder exists, the app starts fresh")
{
    TempRoot root;
    const auto newDir = root.dir / "new";

    CHECK(MigrateFolder(root.dir / "old", newDir).outcome == FolderMigration::NoOldFolder);
    CHECK_FALSE(fs::exists(newDir));
}

TEST_CASE("a copy left half done by an earlier launch is replaced, not kept")
{
    TempRoot root;
    const auto oldDir = root.dir / "old";
    const auto newDir = root.dir / "new";
    WriteFile(oldDir / "settings.json", "old");
    WriteFile(root.dir / "new.migrating" / "stale.json", "stale");

    CHECK(MigrateFolder(oldDir, newDir).outcome == FolderMigration::Copied);
    CHECK(ReadFile(newDir / "settings.json") == "old");
    CHECK_FALSE(fs::exists(newDir / "stale.json"));
}

TEST_CASE("the app is called Zebra, and so is its executable")
{
    CHECK(std::string(gv::app_info::kName) == "Zebra");
    CHECK(std::string(gv::app_info::kLegacyName) == "Govtech Visualizer");
    CHECK(std::string(GV_APP_EXECUTABLE) == "zebra");
}
