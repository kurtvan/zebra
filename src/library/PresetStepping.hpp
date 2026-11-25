#pragma once

// Trying presets quickly: the previous or next one in a folder, or a random one from the library (A3).
// Pure, so they're tested without the app; the preset node's ◀ ▶ Random buttons call them.

#include "library/Library.hpp"

#include <filesystem>
#include <optional>
#include <random>
#include <string>

namespace gv::library {

/// The folder of `preset` in `library`: its id's parent ("" for the root). Nullopt: not in the library.
auto FolderOf(const Library& library, const std::filesystem::path& preset) -> std::optional<std::string>;

/// The preset `step` places from `current` among the presets directly in `folder` (an id prefix, "" for the
/// root; not its subfolders), in library order, wrapping at both ends and skipping broken ones. If `current`
/// isn't in the folder, a step forward gives the first and a step back the last. Nullopt: nothing else to
/// step to (an empty folder, or one holding only `current`).
auto StepPreset(const Library& library, const std::string& folder, const std::filesystem::path& current, int step)
    -> std::optional<std::filesystem::path>;

/// StepPreset in `current`'s own folder. With no preset chosen (`current` empty), steps through the whole
/// library. Nullopt also when `current` isn't in the library.
auto StepPreset(const Library& library, const std::filesystem::path& current, int step) -> std::optional<std::filesystem::path>;

/// A random preset from the whole library, never `current` and never a broken one. Nullopt: there's none.
auto RandomPreset(const Library& library, const std::filesystem::path& current, std::mt19937& random) -> std::optional<std::filesystem::path>;

} // namespace gv::library
