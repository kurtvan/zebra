#pragma once

namespace gv {

/// A rectangle in screen points, from its top-left to its bottom-right corner.
struct ScreenRect
{
    float left{0.f};
    float top{0.f};
    float right{0.f};
    float bottom{0.f};
};

/// Preset pictures narrower than this on screen aren't worth rendering new frames for.
constexpr float kMinThumbnailWidth = 64.f;

/// Whether a preset picture drawn at `rect` is worth keeping fresh: at least `minWidth` wide on screen, and
/// overlapping `canvas` (touching its edge doesn't count).
constexpr auto ThumbnailVisible(const ScreenRect& rect, const ScreenRect& canvas, float minWidth) -> bool
{
    if (rect.right - rect.left < minWidth)
        return false;
    return rect.left < canvas.right && rect.right > canvas.left && rect.top < canvas.bottom && rect.bottom > canvas.top;
}

} // namespace gv
