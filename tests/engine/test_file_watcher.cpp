// FileWatcher: the shader files saved again since the last poll (D8, reloading shaders as they're edited).

#include "engine/FileWatcher.hpp"

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <chrono>
#include <fstream>

using gv::engine::FileWatcher;
using Paths = std::vector<std::filesystem::path>;

namespace {

/// A folder of this process's own (ctest runs test cases in parallel), emptied first.
auto TempDir() -> std::filesystem::path
{
    const auto dir = std::filesystem::temp_directory_path() / ("gv_test_file_watcher_" + std::to_string(getpid()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

/// Writes `file` and moves its time a second on, as a save in an editor would (whatever the file system's resolution).
void Save(const std::filesystem::path& file)
{
    const bool existed = std::filesystem::exists(file);
    const auto before = existed ? std::filesystem::last_write_time(file) : std::filesystem::file_time_type::clock::now();
    std::ofstream(file) << "void main() {}\n";
    std::filesystem::last_write_time(file, before + std::chrono::seconds(1));
}

} // namespace

TEST_CASE("FileWatcher: reports the files saved since the last poll, once each", "[file-watcher]")
{
    const auto dir = TempDir();
    const auto a = dir / "a.fs";
    const auto b = dir / "b.fs";
    Save(a);
    Save(b);
    FileWatcher watcher;
    CHECK(watcher.Poll({a, b}).empty()); // first sight: nothing changed yet
    CHECK(watcher.Poll({a, b}).empty());

    Save(b);
    CHECK(watcher.Poll({a, b}) == Paths{b});
    CHECK(watcher.Poll({a, b}).empty());

    Save(a);
    Save(b);
    CHECK(watcher.Poll({a, b}) == Paths{a, b});
    std::filesystem::remove_all(dir);
}

TEST_CASE("FileWatcher: a file that's gone isn't a change until it comes back", "[file-watcher]")
{
    const auto dir = TempDir();
    const auto a = dir / "a.fs";
    Save(a);
    FileWatcher watcher;
    CHECK(watcher.Poll({a, dir / "missing.fs"}).empty());

    std::filesystem::remove(a);
    CHECK(watcher.Poll({a}).empty());
    Save(a);
    CHECK(watcher.Poll({a}) == Paths{a});
    std::filesystem::remove_all(dir);
}

TEST_CASE("FileWatcher: a file no longer watched is forgotten, so watching it again starts over", "[file-watcher]")
{
    const auto dir = TempDir();
    const auto a = dir / "a.fs";
    Save(a);
    FileWatcher watcher;
    CHECK(watcher.Poll({a}).empty());
    CHECK(watcher.Poll({}).empty());
    Save(a);
    CHECK(watcher.Poll({a}).empty()); // seen for the first time again
    std::filesystem::remove_all(dir);
}
