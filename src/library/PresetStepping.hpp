#pragma once

// Trying presets quickly: the previous or next one in a folder, or a random one from the library (A3), and
// the folders to choose from (A6). Pure, so they're tested without the app; the preset node's ◀ ▶ Random
// buttons and its browser call them.

#include "library/Library.hpp"

#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <vector>

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

/// StepPreset among the presets in `folder` and all its subfolders ("" for the whole library): the folder a
/// preset node's browser chose.
auto StepPresetUnder(const Library& library, const std::string& folder, const std::filesystem::path& current, int step)
    -> std::optional<std::filesystem::path>;

/// A browsed folder: "Category", "Category/Subcategory", or "" for the whole library.
auto FolderName(const std::string& category, const std::string& subcategory) -> std::string;
/// The query showing `folder` and its subfolders (its first two levels: the category and subcategory).
auto FolderQuery(const std::string& folder) -> Query;

/// One row of the folder tree.
struct FolderRow
{
    std::string category;    //!< Empty with depth 0: "All presets".
    std::string subcategory; //!< Depth 2 only.
    int depth{0};            //!< 0: the whole library, 1: a category, 2: a subcategory.
    int count{0};            //!< Presets in it and below.

    auto operator==(const FolderRow&) const -> bool = default;
};

/// The folder tree as rows: "All presets", then every category, with the subcategories of `open` under it
/// (one category is open at a time: the chosen one).
auto FolderRows(const Library& library, const std::string& open) -> std::vector<FolderRow>;

/// A random preset from the whole library, never `current` and never a broken one. Nullopt: there's none.
auto RandomPreset(const Library& library, const std::filesystem::path& current, std::mt19937& random) -> std::optional<std::filesystem::path>;

} // namespace gv::library
