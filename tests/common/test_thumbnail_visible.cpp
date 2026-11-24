#include "common/ThumbnailVisible.hpp"

#include <catch2/catch_test_macros.hpp>

using gv::kMinThumbnailWidth;
using gv::ScreenRect;
using gv::ThumbnailVisible;

namespace {

const ScreenRect kCanvas{100.f, 50.f, 900.f, 650.f};

/// A 16:9 picture `width` wide with its top-left corner at (x, y).
auto Picture(float x, float y, float width) -> ScreenRect
{
    return {x, y, x + width, y + width * 9.f / 16.f};
}

} // namespace

TEST_CASE("ThumbnailVisible: a full-size picture inside the canvas is visible")
{
    CHECK(ThumbnailVisible(Picture(200.f, 200.f, 200.f), kCanvas, kMinThumbnailWidth));
}

TEST_CASE("ThumbnailVisible: a picture too small to see is not")
{
    CHECK_FALSE(ThumbnailVisible(Picture(200.f, 200.f, 40.f), kCanvas, kMinThumbnailWidth));
    CHECK_FALSE(ThumbnailVisible(Picture(200.f, 200.f, 0.f), kCanvas, kMinThumbnailWidth));
}

TEST_CASE("ThumbnailVisible: a preset node's picture pauses at 25% zoom, not at 35%")
{
    constexpr float kPicture = 244.f; // the Milkdrop preset node's picture, in canvas units
    CHECK_FALSE(ThumbnailVisible(Picture(200.f, 200.f, kPicture * 0.25f), kCanvas, kMinThumbnailWidth)); // 61
    CHECK(ThumbnailVisible(Picture(200.f, 200.f, kPicture * 0.35f), kCanvas, kMinThumbnailWidth));       // 85
}

TEST_CASE("ThumbnailVisible: exactly the minimum width is visible, just under is not")
{
    CHECK(ThumbnailVisible(Picture(200.f, 200.f, kMinThumbnailWidth), kCanvas, kMinThumbnailWidth));
    CHECK_FALSE(ThumbnailVisible(Picture(200.f, 200.f, kMinThumbnailWidth - 0.5f), kCanvas, kMinThumbnailWidth));
}

TEST_CASE("ThumbnailVisible: a picture off the canvas is not visible")
{
    CHECK_FALSE(ThumbnailVisible(Picture(1000.f, 200.f, 200.f), kCanvas, kMinThumbnailWidth)); // right
    CHECK_FALSE(ThumbnailVisible(Picture(-300.f, 200.f, 200.f), kCanvas, kMinThumbnailWidth)); // left
    CHECK_FALSE(ThumbnailVisible(Picture(200.f, -200.f, 200.f), kCanvas, kMinThumbnailWidth)); // above
    CHECK_FALSE(ThumbnailVisible(Picture(200.f, 700.f, 200.f), kCanvas, kMinThumbnailWidth)); // below
    CHECK_FALSE(ThumbnailVisible(Picture(900.f, 200.f, 200.f), kCanvas, kMinThumbnailWidth)); // touching the edge
}

TEST_CASE("ThumbnailVisible: a picture partly on the canvas is visible")
{
    CHECK(ThumbnailVisible(Picture(850.f, 200.f, 200.f), kCanvas, kMinThumbnailWidth)); // hanging off the right
    CHECK(ThumbnailVisible(Picture(0.f, 200.f, 200.f), kCanvas, kMinThumbnailWidth));   // hanging off the left
    CHECK(ThumbnailVisible(Picture(200.f, 0.f, 200.f), kCanvas, kMinThumbnailWidth));   // hanging off the top
    CHECK(ThumbnailVisible(Picture(200.f, 600.f, 200.f), kCanvas, kMinThumbnailWidth)); // hanging off the bottom
}

TEST_CASE("ThumbnailVisible: the width counts in full even when the picture is cut off by the edge")
{
    // Only 20 points show, but the picture is full size: still worth rendering.
    CHECK(ThumbnailVisible(Picture(880.f, 200.f, 200.f), kCanvas, kMinThumbnailWidth));
}
