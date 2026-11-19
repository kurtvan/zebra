#pragma once

#include <imgui.h>

#include <filesystem>
#include <string>
#include <vector>

namespace gv::app {

// Colours and small widgets shared by the editor panels and the node canvas.

inline const ImVec4 kAccent{0.29f, 0.72f, 0.70f, 1.f};
inline const ImVec4 kGreen{0.36f, 0.82f, 0.48f, 1.f};
inline const ImVec4 kAmber{0.96f, 0.72f, 0.30f, 1.f};
inline const ImVec4 kRed{0.96f, 0.42f, 0.42f, 1.f};
inline const ImVec4 kDim{0.58f, 0.60f, 0.64f, 1.f};

inline constexpr const char* kPresetDragPayload = "GV_PRESET"; //!< Payload: a preset's absolute path (NUL-terminated).
inline constexpr const char* kVideoDragPayload = "GV_VIDEO";   //!< Payload: a pool video's id (NUL-terminated).

inline const std::vector<std::string> kVideoExtensions = {"mp4", "mov", "m4v"};

auto Lower(std::string text) -> std::string;
auto IsVideoFile(const std::filesystem::path& file) -> bool;

/// A small star toggle drawn with the draw list (the UI font has no star glyph).
auto StarButton(const char* id, bool on, float size = 15.f) -> bool;

/// Starts dragging a preset (to drop it on a scene, the preview or the node canvas).
void PresetDragSource(const std::filesystem::path& preset, const std::string& label);

/// A one-line help marker.
void Hint(const char* text);

} // namespace gv::app
