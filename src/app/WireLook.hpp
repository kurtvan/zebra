#pragma once

// How wires and pins look, by what they carry: in one place, so the look can be tried out and changed here.
//
// Picture wires are blue, with round pins in the node's input column. Number wires (modulation) are orange,
// with diamond pins; a modulation input's pin sits at the start of the slider it moves (Control::param), so
// the wire visibly plugs into that setting. Audio wires are green, with small square pins.

#include "app/PinPill.hpp"
#include "model/Graph.hpp"

#include <imgui.h>

namespace gv::app {

inline const ImVec4 kPictureWire{0.42f, 0.64f, 0.98f, 1.f}; //!< Blue.
inline const ImVec4 kNumberWire{1.00f, 0.56f, 0.24f, 1.f};  //!< Orange.
inline const ImVec4 kAudioWire{0.36f, 0.82f, 0.45f, 1.f};   //!< Green.
/// A wire into a setting its node no longer has (a shader input the file lost): red, and faint.
inline const ImVec4 kBrokenWire{0.96f, 0.42f, 0.42f, 0.55f};

inline auto WireColor(model::PortKind kind) -> ImVec4
{
    switch (kind)
    {
        case model::PortKind::Number: return kNumberWire;
        case model::PortKind::Audio: return kAudioWire;
        case model::PortKind::Picture: break;
    }
    return kPictureWire;
}

/// A pin at `center` with `wires` wires: a dot (a diamond for numbers, a small square for audio), filled when
/// wired, an outline when not; `pill` for two or more (PinPill).
inline void DrawPinShape(ImDrawList* draw, ImVec2 center, model::PortKind kind, int wires, const PinShape& pill, ImU32 color)
{
    if (wires >= 2)
    {
        draw->AddRectFilled(pill.min, pill.max, color, kind == model::PortKind::Picture ? pill.rounding : 1.f);
        return;
    }
    if (kind == model::PortKind::Audio)
    {
        const float r = kPinDotRadius - 1.f;
        const ImVec2 min(center.x - r, center.y - r), max(center.x + r, center.y + r);
        if (wires == 1)
            draw->AddRectFilled(min, max, color, 1.f);
        else
            draw->AddRect(min, max, color, 1.f, 0, 1.6f);
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
