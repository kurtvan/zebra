#pragma once

// The ISF shader node's file: its parsed header, which gives the node its Video inputs and controls.

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
    std::shared_ptr<const isf::IsfShader> shader; //!< Null: no file chosen, missing or broken.
    bool missing{false};                          //!< A path is set but there's no file there.
    std::string error;                            //!< Why the file can't be read ("line 3: ..."); empty if it can.
};

/// The file at `path`, read and parsed again only when its modification time changes. Thread-safe.
auto IsfFileAt(const std::filesystem::path& path) -> IsfFile;

/// The shader's image inputs, in order: the node's Video inputs.
auto IsfImageInputs(const isf::IsfShader& shader) -> std::vector<const isf::IsfInput*>;

/// The value input `input` has on a node with `values`: its own if set, else the input's DEFAULT, else 0.
auto IsfValue(const std::map<std::string, std::vector<float>>& values, const isf::IsfInput& input) -> float;

} // namespace gv::model
