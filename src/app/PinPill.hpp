#pragma once

#include <imgui.h>

#include <algorithm>
#include <numeric>
#include <vector>

namespace gv::app {

constexpr float kPinDotRadius = 5.f;    //!< A wired pin's dot; a pill's round ends have this radius too.
constexpr float kPinPointSpacing = 7.f; //!< How far apart the wires at a shared pin attach (canvas units).

/// Which side of its node a pin is on. Every pin today is on the left (inputs) or the right (outputs).
enum class PinSide
{
    Left,
    Right,
    Top,
    Bottom,
};

/// The way a pin's pill runs: across the way its wires leave. Down for pins on a node's left or right side
/// (a tall pill), across for pins on its top or bottom (a wide one). The pill's orientation is decided here only.
inline auto PillAxis(PinSide side) -> ImVec2
{
    return side == PinSide::Left || side == PinSide::Right ? ImVec2(0.f, 1.f) : ImVec2(1.f, 0.f);
}

/// How much longer than a dot a pin's pill is with `count` wires: one spacing per wire after the first.
inline auto PillGrowth(int count) -> float
{
    return count > 1 ? static_cast<float>(count - 1) * kPinPointSpacing : 0.f;
}

/// Where `count` wires attach to the pin centred on `center`: evenly spaced along its pill, centred on the pin,
/// first to last along PillAxis (top to bottom for a side pin). One wire (or none) attaches at the centre.
inline auto AttachPoints(ImVec2 center, int count, PinSide side) -> std::vector<ImVec2>
{
    const ImVec2 axis = PillAxis(side);
    const int n = std::max(count, 1);
    std::vector<ImVec2> points;
    for (int k = 0; k < n; ++k)
    {
        const float along = static_cast<float>(k) * kPinPointSpacing - PillGrowth(n) * 0.5f;
        points.emplace_back(center.x + axis.x * along, center.y + axis.y * along);
    }
    return points;
}

/// A pin's shape: a rounded rect whose rounding is half its short side, so its ends are fully round (a stadium).
/// For one wire it's the dot.
struct PinShape
{
    ImVec2 min{0, 0};
    ImVec2 max{0, 0};
    float rounding{kPinDotRadius};
};

/// The pill of a pin centred on `center` with `count` wires: round ends of the dot's radius, just round its
/// attachment points (AttachPoints).
inline auto PinPill(ImVec2 center, int count, PinSide side) -> PinShape
{
    const ImVec2 axis = PillAxis(side);
    const float half = PillGrowth(count) * 0.5f;
    const ImVec2 extent(kPinDotRadius + axis.x * half, kPinDotRadius + axis.y * half);
    return {ImVec2(center.x - extent.x, center.y - extent.y), ImVec2(center.x + extent.x, center.y + extent.y), kPinDotRadius};
}

/// Which attachment point each wire at a shared pin takes (0 is the first of AttachPoints), given where each
/// wire's other end is: in order along the pill's axis (wires whose other end is higher attach higher, so they
/// don't cross just before the pin), then across it, then in the order given. The same ends give the same slots.
inline auto AttachSlots(const std::vector<ImVec2>& otherEnds, PinSide side) -> std::vector<int>
{
    const ImVec2 axis = PillAxis(side);
    auto along = [&](ImVec2 p) { return p.x * axis.x + p.y * axis.y; };
    auto across = [&](ImVec2 p) { return p.x * axis.y + p.y * axis.x; };
    std::vector<size_t> order(otherEnds.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const ImVec2 p = otherEnds[a];
        const ImVec2 q = otherEnds[b];
        return along(p) < along(q) || (along(p) == along(q) && across(p) < across(q));
    });
    std::vector<int> slots(otherEnds.size());
    for (size_t k = 0; k < order.size(); ++k)
        slots[order[k]] = static_cast<int>(k);
    return slots;
}

/// The height of a node row holding a pin with `count` wires, for a row `base` high with a dot: it grows with
/// a tall pill, so the pill fits; a wide pill (a top or bottom pin) doesn't make it taller.
inline auto PinRowHeight(float base, int count, PinSide side) -> float
{
    return base + PillGrowth(count) * PillAxis(side).y;
}

} // namespace gv::app
