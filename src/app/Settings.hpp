#pragma once

#include "app/PaneLayout.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace gv::app {

/// User settings, stored as settings.json in Application Support.
struct Settings
{
    std::filesystem::path libraryRoot;
    std::vector<std::filesystem::path> texturePaths;
    std::string audioDevice; //!< Empty: system default input.
    float gain{1.0f};
    int midiChannel{0};      //!< 0-15 (shown as 1-16).
    bool namesOnly{false};
    bool graphPreview{false};    //!< The Graph panel shows the live output behind the nodes.
    float graphPreviewDim{0.5f}; //!< 0..0.9: how much that preview is darkened, so the nodes stay readable.
    std::filesystem::path lastPerformance;
    std::vector<std::filesystem::path> recentPerformances; //!< Most recent first.
    PaneLayout panes; //!< Which editor areas are collapsed (default: none).

    /// Loads from `file`; missing or invalid fields fall back to defaults. Never throws.
    static auto Load(const std::filesystem::path& file) -> Settings;

    /// Writes atomically. Returns an error message, or empty on success.
    auto Save(const std::filesystem::path& file) const -> std::string;

    /// Defaults for first launch: suggested library and texture folders that exist on this machine.
    static auto Defaults() -> Settings;
};

} // namespace gv::app
