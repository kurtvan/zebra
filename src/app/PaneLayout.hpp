#pragma once

#include <nlohmann/json_fwd.hpp>

#include <array>

namespace gv::app {

/// The editor's collapsible areas around the Preview and Graph.
enum class PaneArea
{
    Left,   //!< Scenes.
    Right,  //!< Scene, Setup.
    Bottom, //!< Presets, Videos, Status, Messages.
};
inline constexpr int kPaneAreaCount = 3;

/// Which areas are collapsed, and focus mode. Pure bookkeeping: the Shell does the docking. A collapsed area's
/// windows aren't submitted, and ImGui keeps their empty dock node (with its size and tab order) until they return.
struct PaneLayout
{
    std::array<bool, kPaneAreaCount> collapsed{};   //!< By PaneArea. All false (everything shown) is the default.
    bool focus{false};                              //!< Focus mode collapsed every area.
    std::array<bool, kPaneAreaCount> beforeFocus{}; //!< What was collapsed when focus mode started.

    auto IsCollapsed(PaneArea area) const -> bool;

    /// Collapses or restores one area. Ends focus mode and leaves the other areas as they are.
    void Toggle(PaneArea area);

    /// Focus mode on: collapses every area. Off: brings back exactly the areas that were open before.
    /// If every area was already collapsed by hand, it shows them all instead (so the shortcut always does something).
    void ToggleFocus();

    auto ToJson() const -> nlohmann::json;

    /// Missing or invalid fields fall back to defaults. Never throws.
    static auto FromJson(const nlohmann::json& json) -> PaneLayout;

    auto operator==(const PaneLayout&) const -> bool = default;
};

} // namespace gv::app
