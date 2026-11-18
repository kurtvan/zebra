#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace gv::library {

/// What a preset's path within the library says about it.
struct PathInfo
{
    std::string category;    //!< First folder level, e.g. "Dancer".
    std::string subcategory; //!< Second folder level, e.g. "Glowsticks Mirror".
    std::string author;      //!< Filename part before " - ", e.g. "shifter" (may be empty or several authors).
    std::string name;        //!< The rest of the filename, without ".milk".
};

/// Parses a path relative to the library root, like "Dancer/Glowsticks Mirror/shifter - mandala.milk".
/// Deeper trees use the first two folders; shallower ones leave the missing levels empty.
auto ParsePresetPath(const std::string& relativePath) -> PathInfo;

/// Names of the image textures a preset's shaders sample, as projectM 4.1.8 resolves them: lowercase, without the
/// two-letter wrap/filter prefix (fw_, fc_, pw_, pc_, their reverses, or any other XY_), and leaving out what
/// projectM provides itself (main, blur1-3, the noise and idle textures) and rand00-rand99 (decision D6).
auto ExtractTextureNames(const std::string& presetText) -> std::set<std::string>;

/// ExtractTextureNames for a preset file, sorted. Empty if it can't be read.
auto PresetTextures(const std::filesystem::path& preset) -> std::vector<std::string>;

/// Lowercase copy (ASCII).
auto ToLower(std::string text) -> std::string;

} // namespace gv::library
