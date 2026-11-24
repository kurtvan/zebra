#pragma once

#include <algorithm>

namespace gv {

struct Rect
{
    int x{0};
    int y{0};
    int width{0};
    int height{0};

    auto operator==(const Rect&) const -> bool = default;
};

/// The largest rectangle with the canvas's shape that fits in the target, centred (letterboxed or
/// pillarboxed). Empty when either size is empty.
constexpr auto FitRect(int canvasWidth, int canvasHeight, int targetWidth, int targetHeight) -> Rect
{
    if (canvasWidth <= 0 || canvasHeight <= 0 || targetWidth <= 0 || targetHeight <= 0)
        return {};
    // Compare cross products in 64 bits so the result doesn't depend on float rounding.
    const long long wide = static_cast<long long>(canvasWidth) * targetHeight;
    const long long tall = static_cast<long long>(targetWidth) * canvasHeight;
    int width = targetWidth;
    int height = targetHeight;
    if (wide > tall)
        height = static_cast<int>(static_cast<long long>(targetWidth) * canvasHeight / canvasWidth); // full width, bars top and bottom
    else if (wide < tall)
        width = static_cast<int>(static_cast<long long>(targetHeight) * canvasWidth / canvasHeight); // full height, bars left and right
    width = std::max(width, 1);
    height = std::max(height, 1);
    return {(targetWidth - width) / 2, (targetHeight - height) / 2, width, height};
}

} // namespace gv
