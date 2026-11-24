#pragma once

#include "app/CanvasZoom.hpp"

#include <imgui_node_editor.h>

namespace gv::app {

/// Zooms and pans a node editor's view, one step a frame (a pinch, a scroll).
///
/// The editor snaps the view to whole screen pixels. Starting each step from the snapped view would add up to a
/// pixel of drift a frame, always the same way, so a pinch would slide the canvas sideways. Each step starts
/// instead from the exact view the last one asked for, as long as the editor still shows what that step got.
class EditorView
{
public:
    /// Zooms by `factor` (above 1 zooms in) at the screen point `mouse`, keeping the canvas point under it still.
    /// Call between `ed::Begin` and `ed::End`.
    void Zoom(float factor, ImVec2 mouse)
    {
        namespace ed = ax::NodeEditor;
        const CanvasRect shown = Shown();
        const CanvasRect start = Start(shown);
        const ImVec2 under = ed::ScreenToCanvas(mouse); // snapping only moves the view, so shift the point as well
        const ImVec2 exact(under.x + start.min.x - shown.min.x, under.y + start.min.y - shown.min.y);
        Ask(ZoomAt(start, ed::GetCurrentZoom(), exact, factor));
    }

    /// Moves the view by `pixels` on screen, keeping its zoom. Call between `ed::Begin` and `ed::End`.
    void Pan(ImVec2 pixels)
    {
        const CanvasRect start = Start(Shown());
        const float zoom = ax::NodeEditor::GetCurrentZoom(); // screen pixels per canvas unit
        const ImVec2 shift(pixels.x / zoom, pixels.y / zoom);
        Ask({ImVec2(start.min.x + shift.x, start.min.y + shift.y), ImVec2(start.max.x + shift.x, start.max.y + shift.y)});
    }

private:
    static auto Shown() -> CanvasRect
    {
        const ax::NodeEditor::ViewState view = ax::NodeEditor::GetViewState();
        return {view.VisibleMin, view.VisibleMax};
    }

    static auto Same(const CanvasRect& a, const CanvasRect& b) -> bool
    {
        return a.min.x == b.min.x && a.min.y == b.min.y && a.max.x == b.max.x && a.max.y == b.max.y;
    }

    /// The exact view the last step asked for, unless something else has moved the view since.
    auto Start(const CanvasRect& shown) const -> CanvasRect
    {
        return m_stepped && Same(shown, m_got) ? m_asked : shown;
    }

    void Ask(const CanvasRect& view)
    {
        ax::NodeEditor::SetViewState({view.min, view.max});
        m_asked = view;
        m_got = Shown();
        m_stepped = true;
    }

    CanvasRect m_asked; //!< The view the last step asked for.
    CanvasRect m_got;   //!< The (snapped) view the editor showed for it.
    bool m_stepped{false};
};

} // namespace gv::app
