#pragma once

// The ISF shader node's file: its parsed header, which gives the node its picture inputs and controls.

#include "isf/IsfHeader.hpp"
#include "model/Graph.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gv::model {

struct ParamDef;

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

/// The node's params (NodeDef::paramsFor): a slider per float input of its file, a checkbox per bool and a
/// choice per long, then a lost param per wire into an input the file doesn't have (IsfShaderParams::
/// wiredParams). Kept for the session, once per header. Thread-safe.
auto IsfParams(const Node& node) -> std::span<const ParamDef>;

/// What a Resolution divides the canvas size by: 1, 2 or 4.
auto IsfDivisor(IsfResolution resolution) -> int;

/// The value input `input` has on a node with `values`: its own if set, else the input's DEFAULT, else 0.
auto IsfValue(const std::map<std::string, std::vector<float>>& values, const isf::IsfInput& input) -> float;

} // namespace gv::model
