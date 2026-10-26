#pragma once

#include "model/Performance.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace gv::model {

/// One problem found while loading or validating, located by a JSON-pointer-like path.
struct Issue
{
    enum class Severity
    {
        Warning, //!< Loaded anyway (e.g. a missing file).
        Error    //!< The value was rejected or the file can't be used.
    };

    Severity severity{Severity::Error};
    std::string path; //!< e.g. "/scenes/3/midi/cc20Value"
    std::string message;
};

struct LoadResult
{
    Performance performance;
    std::vector<Issue> issues;
    bool ok{false}; //!< False when the file couldn't be read or parsed at all, or has errors.

    auto HasErrors() const -> bool;
};

/// Parses a performance from JSON text. Never throws.
auto ParsePerformance(const std::string& text) -> LoadResult;

/// Reads, parses, validates and checks referenced files. Never throws.
auto LoadPerformance(const std::filesystem::path& file) -> LoadResult;

/// Serializes to pretty-printed JSON (format version kCurrentFormatVersion).
auto SerializePerformance(const Performance& performance) -> std::string;

/// Writes atomically (temp file + rename). Returns an error message, or empty on success.
auto SavePerformance(const Performance& performance, const std::filesystem::path& file) -> std::string;

/// Structural rules: scene count, unique ids, CC 20 range/uniqueness, mix/luma range.
auto Validate(const Performance& performance) -> std::vector<Issue>;

/// Sets presetMissing / videoMissing from the filesystem and returns a warning per missing file.
auto CheckFiles(Performance& performance) -> std::vector<Issue>;

} // namespace gv::model
