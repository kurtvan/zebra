#pragma once

#include <imgui.h>

#include <algorithm>

namespace gv::app {

constexpr float kCanvasMinZoom = 0.25f; //!< Smallest canvas zoom (25%).
constexpr float kCanvasMaxZoom = 2.0f;  //!< Largest canvas zoom (200%).

/// The part of the canvas the view shows, in canvas units.
struct CanvasRect
{
    ImVec2 min; //!< Canvas point at the view's top left.
    ImVec2 max; //!< Canvas point at the view's bottom right.
};

/// Zooms `view`, now at `zoom` screen pixels per canvas unit, by `factor` (above 1 zooms in) around the canvas
/// point `under`, which stays where it is on screen. The zoom is clamped to 25%–200%.
///
/// Works in canvas units only, so the rect keeps its exact aspect ratio: the node editor fits the rect it's given
/// to its (whole-pixel) canvas and centres it, and a rect built from a fractional widget size would shift a
/// little every frame of a pinch.
inline auto ZoomAt(CanvasRect view, float zoom, ImVec2 under, float factor) -> CanvasRect
{
    const float f = std::clamp(zoom * factor, kCanvasMinZoom, kCanvasMaxZoom) / zoom;
    return {ImVec2(under.x - (under.x - view.min.x) / f, under.y - (under.y - view.min.y) / f),
            ImVec2(under.x + (view.max.x - under.x) / f, under.y + (view.max.y - under.y) / f)};
}

/// `view` moved so its centre is the canvas point `centre`, keeping its size (and so the zoom).
inline auto CentredOn(CanvasRect view, ImVec2 centre) -> CanvasRect
{
    const ImVec2 half((view.max.x - view.min.x) * 0.5f, (view.max.y - view.min.y) * 0.5f);
    return {ImVec2(centre.x - half.x, centre.y - half.y), ImVec2(centre.x + half.x, centre.y + half.y)};
}

} // namespace gv::app
