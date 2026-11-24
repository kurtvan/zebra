#pragma once

#include <imgui.h>

#include <algorithm>

namespace gv::app {

constexpr float kCanvasMinZoom = 0.25f; //!< Smallest canvas zoom (25%).
constexpr float kCanvasMaxZoom = 2.0f;  //!< Largest canvas zoom (200%).

/// Where the canvas is looking: its zoom and the canvas point at the view's top left corner.
struct CanvasView
{
    float scale{1.f}; //!< Screen pixels per canvas unit.
    ImVec2 origin;    //!< Canvas point at the view's top left.
};

/// Zooms the view by `factor` (above 1 zooms in) around `pointer`, in screen pixels from the view's top left,
/// so the canvas point under the pointer stays under it. The zoom is clamped to 25%–200%.
inline auto ZoomAt(float scale, ImVec2 origin, ImVec2 pointer, float factor) -> CanvasView
{
    const float newScale = std::clamp(scale * factor, kCanvasMinZoom, kCanvasMaxZoom);
    const ImVec2 under(origin.x + pointer.x / scale, origin.y + pointer.y / scale);
    return {newScale, ImVec2(under.x - pointer.x / newScale, under.y - pointer.y / newScale)};
}

} // namespace gv::app
