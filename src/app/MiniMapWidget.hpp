#pragma once

#include "app/EditorView.hpp"
#include "app/MiniMap.hpp"

#include <imgui.h>
#include <imgui_node_editor.h>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace gv::app {

/// What the mini-map draws this frame.
struct MiniMapFrame
{
    MiniMap map;
    std::vector<CanvasRect> nodes; //!< In canvas units.
    CanvasRect visible;            //!< The part of the canvas on screen, in canvas units.
};

/// Puts the mini-map over the bottom-right corner of the canvas `canvasMin`–`canvasMax` (on screen) for a graph
/// whose nodes cover `nodes` (canvas units), and pans `view` to where it's clicked or dragged, keeping the zoom.
/// Empty when there's nothing to show.
///
/// Call right after `ed::Begin`, before any node is drawn: the map takes the pointer as an ImGui button, and the
/// first item under the pointer gets it, so neither the nodes' own controls nor the canvas (box select, node
/// drag, its menus) react while the pointer is over the map. Draw it with DrawMiniMap after `ed::End`.
inline auto MiniMapInput(std::vector<CanvasRect> nodes, ImVec2 canvasMin, ImVec2 canvasMax, EditorView& view) -> std::optional<MiniMapFrame>
{
    namespace ed = ax::NodeEditor;
    const std::optional<CanvasRect> bounds = GraphBounds(nodes, kMiniMapMargin);
    if (!bounds)
        return std::nullopt;
    const std::optional<MiniMap> map = PlaceMiniMap(*bounds, canvasMin, canvasMax);
    if (!map)
        return std::nullopt;

    ed::Suspend(); // on screen, outside the zoomed canvas
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(map->min);
    ImGui::InvisibleButton("##minimap", ImVec2(map->max.x - map->min.x, map->max.y - map->min.y));
    std::optional<ImVec2> centre;
    if (ImGui::IsItemActivated())
        centre = MiniMapClick(*map, ImGui::GetMousePos());
    else if (ImGui::IsItemActive())
        centre = MiniMapDrag(*map, ImGui::GetMousePos());
    ImGui::SetCursorScreenPos(cursor);
    ed::Resume();
    if (centre)
        view.CentreOn(*centre); // through EditorView, so a drag doesn't drift by the editor's whole-pixel snapping

    const ed::ViewState shown = ed::GetViewState();
    return MiniMapFrame{*map, std::move(nodes), {shown.VisibleMin, shown.VisibleMax}};
}

/// Draws the mini-map: a translucent panel, a small box per node and an outline around the part on screen.
inline void DrawMiniMap(ImDrawList* draw, const MiniMapFrame& frame)
{
    const MiniMap& map = frame.map;
    draw->PushClipRect(map.min, map.max, true);
    draw->AddRectFilled(map.min, map.max, IM_COL32(20, 22, 26, 200), 3.f);
    for (const CanvasRect& node : frame.nodes)
    {
        const ImVec2 min = CanvasToMap(map, node.min);
        const ImVec2 max = CanvasToMap(map, node.max);
        draw->AddRectFilled(min, ImVec2(std::max(max.x, min.x + 2.f), std::max(max.y, min.y + 2.f)), IM_COL32(150, 160, 175, 200), 1.f);
    }
    const CanvasRect seen = ClipToMap(map, {CanvasToMap(map, frame.visible.min), CanvasToMap(map, frame.visible.max)});
    draw->AddRectFilled(seen.min, seen.max, IM_COL32(255, 255, 255, 18));
    draw->AddRect(seen.min, seen.max, IM_COL32(255, 255, 255, 190), 0.f, 0, 1.5f);
    draw->PopClipRect();
    draw->AddRect(map.min, map.max, IM_COL32(255, 255, 255, 60), 3.f);
}

} // namespace gv::app
