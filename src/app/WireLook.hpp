#pragma once

// How wires and pins look, by what they carry: in one place, so the look can be tried out and changed here.
//
// Picture wires are blue, with round pins in the node's input column. Number wires (modulation) are orange,
// with diamond pins; a modulation input's pin sits at the start of the slider it moves (Control::param), so
// the wire visibly plugs into that setting.

#include "app/PinPill.hpp"
#include "model/Graph.hpp"

#include <imgui.h>

namespace gv::app {

inline const ImVec4 kPictureWire{0.42f, 0.64f, 0.98f, 1.f}; //!< Blue.
inline const ImVec4 kNumberWire{1.00f, 0.56f, 0.24f, 1.f};  //!< Orange.

inline auto WireColor(model::PortKind kind) -> ImVec4
{
    return kind == model::PortKind::Number ? kNumberWire : kPictureWire;
}

/// A pin at `center` with `wires` wires: a dot (a diamond for numbers), filled when wired, an outline when not;
/// `pill` for two or more (PinPill).
inline void DrawPinShape(ImDrawList* draw, ImVec2 center, model::PortKind kind, int wires, const PinShape& pill, ImU32 color)
{
    if (wires >= 2)
    {
        draw->AddRectFilled(pill.min, pill.max, color, kind == model::PortKind::Number ? 1.f : pill.rounding);
        return;
    }
    if (kind == model::PortKind::Number)
    {
        const float r = kPinDotRadius + 0.5f;
        const ImVec2 top(center.x, center.y - r), right(center.x + r, center.y), bottom(center.x, center.y + r), left(center.x - r, center.y);
        if (wires == 1)
            draw->AddQuadFilled(top, right, bottom, left, color);
        else
            draw->AddQuad(top, right, bottom, left, color, 1.6f);
        return;
    }
    if (wires == 1)
        draw->AddCircleFilled(center, kPinDotRadius, color);
    else
        draw->AddCircle(center, 4.5f, color, 0, 1.6f);
}

} // namespace gv::app
