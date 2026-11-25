#pragma once

#include "app/CanvasZoom.hpp"

#include <imgui.h>

#include <algorithm>
#include <optional>
#include <vector>

namespace gv::app {

constexpr float kMiniMapWidth = 180.f;     //!< The mini-map's width on screen, for a graph wider than it is tall.
constexpr float kMiniMapMaxHeight = 120.f; //!< Its tallest; a tall graph gets a narrower map.
constexpr float kMiniMapMinSide = 40.f;    //!< Its narrowest side, so a long thin graph still has a map to click.
constexpr float kMiniMapInset = 10.f;      //!< Gap to the canvas's bottom-right corner.
constexpr float kMiniMapMargin = 40.f;     //!< Canvas units of space around the graph inside the map.

/// Where the mini-map sits on screen, and which part of the canvas it shows.
struct MiniMap
{
    ImVec2 min;         //!< The map's top left on screen.
    ImVec2 max;         //!< The map's bottom right on screen.
    ImVec2 origin;      //!< The canvas point at the map's top left.
    float scale{1.f};   //!< Map pixels per canvas unit.
};

/// The canvas area around every node in `nodes`, with `margin` canvas units to spare; empty when there are none.
inline auto GraphBounds(const std::vector<CanvasRect>& nodes, float margin) -> std::optional<CanvasRect>
{
    if (nodes.empty())
        return std::nullopt;
    CanvasRect bounds = nodes.front();
    for (const CanvasRect& node : nodes)
    {
        bounds.min = ImVec2(std::min(bounds.min.x, node.min.x), std::min(bounds.min.y, node.min.y));
        bounds.max = ImVec2(std::max(bounds.max.x, node.max.x), std::max(bounds.max.y, node.max.y));
    }
    return CanvasRect{ImVec2(bounds.min.x - margin, bounds.min.y - margin), ImVec2(bounds.max.x + margin, bounds.max.y + margin)};
}

/// Places a map of the canvas area `world` in the bottom-right corner of the canvas `canvasMin`–`canvasMax` (on
/// screen), keeping the area's aspect ratio. Empty when the canvas is too small to hold it.
inline auto PlaceMiniMap(const CanvasRect& world, ImVec2 canvasMin, ImVec2 canvasMax) -> std::optional<MiniMap>
{
    const float width = std::max(world.max.x - world.min.x, 1.f);
    const float height = std::max(world.max.y - world.min.y, 1.f);
    const float scale = std::min(kMiniMapWidth / width, kMiniMapMaxHeight / height);
    const ImVec2 size(std::max(width * scale, kMiniMapMinSide), std::max(height * scale, kMiniMapMinSide));
    if (canvasMax.x - canvasMin.x < size.x + kMiniMapInset * 4.f || canvasMax.y - canvasMin.y < size.y + kMiniMapInset * 4.f)
        return std::nullopt;
    MiniMap map;
    map.max = ImVec2(canvasMax.x - kMiniMapInset, canvasMax.y - kMiniMapInset);
    map.min = ImVec2(map.max.x - size.x, map.max.y - size.y);
    map.scale = scale;
    // A long thin graph is centred across the map's narrow way.
    map.origin = ImVec2((world.min.x + world.max.x) * 0.5f - size.x * 0.5f / scale, (world.min.y + world.max.y) * 0.5f - size.y * 0.5f / scale);
    return map;
}

/// Where the canvas point `canvas` is drawn on the map, on screen.
inline auto CanvasToMap(const MiniMap& map, ImVec2 canvas) -> ImVec2
{
    return {map.min.x + (canvas.x - map.origin.x) * map.scale, map.min.y + (canvas.y - map.origin.y) * map.scale};
}

/// The canvas point the map shows at the screen point `screen`.
inline auto MapToCanvas(const MiniMap& map, ImVec2 screen) -> ImVec2
{
    return {map.origin.x + (screen.x - map.min.x) / map.scale, map.origin.y + (screen.y - map.min.y) / map.scale};
}

/// Whether the screen point `screen` is on the map.
inline auto OnMap(const MiniMap& map, ImVec2 screen) -> bool
{
    return screen.x >= map.min.x && screen.y >= map.min.y && screen.x < map.max.x && screen.y < map.max.y;
}

/// The canvas point to centre the view on for a click at `screen`; empty when the click is off the map.
inline auto MiniMapClick(const MiniMap& map, ImVec2 screen) -> std::optional<ImVec2>
{
    if (!OnMap(map, screen))
        return std::nullopt;
    return MapToCanvas(map, screen);
}

/// The canvas point to centre the view on while a drag that started on the map is at `screen`: dragging past
/// the map's edge stops at the edge.
inline auto MiniMapDrag(const MiniMap& map, ImVec2 screen) -> ImVec2
{
    return MapToCanvas(map, ImVec2(std::clamp(screen.x, map.min.x, map.max.x), std::clamp(screen.y, map.min.y, map.max.y)));
}

/// `rect` (on screen) cut to the map, so a view bigger than the graph is outlined at the map's edge.
inline auto ClipToMap(const MiniMap& map, CanvasRect rect) -> CanvasRect
{
    return {ImVec2(std::clamp(rect.min.x, map.min.x, map.max.x), std::clamp(rect.min.y, map.min.y, map.max.y)),
            ImVec2(std::clamp(rect.max.x, map.min.x, map.max.x), std::clamp(rect.max.y, map.min.y, map.max.y))};
}

} // namespace gv::app
