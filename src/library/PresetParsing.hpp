#pragma once

#include <set>
#include <string>

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

/// Names of the image textures a preset's shaders sample (lowercase, without the fw_/fc_/pw_/pc_ prefixes),
/// leaving out projectM's built-in ones (main, blur1-3, noise*, noisevol*, rand00-15...).
auto ExtractTextureNames(const std::string& presetText) -> std::set<std::string>;

/// Lowercase copy (ASCII).
auto ToLower(std::string text) -> std::string;

} // namespace gv::library
