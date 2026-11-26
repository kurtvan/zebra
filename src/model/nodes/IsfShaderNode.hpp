#pragma once

// The ISF shader node's file: its parsed header, which gives the node its picture inputs and controls.

#include "isf/IsfHeader.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gv::model {

/// An ISF file as the node sees it.
struct IsfFile
{
    /// Null: no file chosen, missing, or broken since it was first read. A file that breaks after it was read
    /// keeps the last version that could be, with `error` set, so a bad edit keeps the old version running.
    std::shared_ptr<const isf::IsfShader> shader;
    bool missing{false}; //!< A path is set but there's no file there.
    std::string error;   //!< Why the file can't be read ("line 3: ..."); empty if it can.
    /// What `shader` uses that can't run yet (isf::UnsupportedFeature), or empty. Such a shader is never
    /// compiled: the video passes through it.
    std::string unsupported;
};

/// The file at `path`, read and parsed again only when its modification time changes. Thread-safe.
auto IsfFileAt(const std::filesystem::path& path) -> IsfFile;

/// The shader's image inputs, in order: the node's picture inputs.
auto IsfImageInputs(const isf::IsfShader& shader) -> std::vector<const isf::IsfInput*>;

/// The value input `input` has on a node with `values`: its own if set, else the input's DEFAULT, else 0.
auto IsfValue(const std::map<std::string, std::vector<float>>& values, const isf::IsfInput& input) -> float;

} // namespace gv::model
