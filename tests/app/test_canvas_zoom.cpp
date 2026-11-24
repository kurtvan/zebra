#include "app/CanvasZoom.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using gv::app::CanvasView;
using gv::app::ZoomAt;

namespace {

/// The canvas point at `pointer` (screen pixels from the view's top left).
auto Under(const CanvasView& view, ImVec2 pointer) -> ImVec2
{
    return {view.origin.x + pointer.x / view.scale, view.origin.y + pointer.y / view.scale};
}

} // namespace

TEST_CASE("Zooming keeps the canvas point under the pointer still")
{
    const CanvasView before{0.8f, ImVec2(-120.f, 45.f)};
    const ImVec2 pointer(310.f, 220.f);
    for (float factor : {1.3f, 0.7f, 1.05f, 0.95f})
    {
        const CanvasView after = ZoomAt(before.scale, before.origin, pointer, factor);
        CHECK(after.scale == Approx(before.scale * factor));
        CHECK(Under(after, pointer).x == Approx(Under(before, pointer).x));
        CHECK(Under(after, pointer).y == Approx(Under(before, pointer).y));
    }
}

TEST_CASE("Zooming clamps at 25% and 200%, still around the pointer")
{
    const ImVec2 origin(10.f, 20.f);
    const ImVec2 pointer(100.f, 50.f);

    const CanvasView in = ZoomAt(1.8f, origin, pointer, 4.f);
    CHECK(in.scale == Approx(2.f));
    CHECK(Under(in, pointer).x == Approx(origin.x + pointer.x / 1.8f));
    CHECK(Under(in, pointer).y == Approx(origin.y + pointer.y / 1.8f));

    const CanvasView out = ZoomAt(0.3f, origin, pointer, 0.1f);
    CHECK(out.scale == Approx(0.25f));
    CHECK(Under(out, pointer).x == Approx(origin.x + pointer.x / 0.3f));

    // Already at a limit: zooming further changes nothing.
    const CanvasView stuck = ZoomAt(2.f, origin, pointer, 1.5f);
    CHECK(stuck.scale == Approx(2.f));
    CHECK(stuck.origin.x == Approx(origin.x));
    CHECK(stuck.origin.y == Approx(origin.y));
}

TEST_CASE("Zooming by a factor of 1 changes nothing")
{
    const CanvasView view = ZoomAt(0.6f, ImVec2(-300.f, 75.f), ImVec2(512.f, 384.f), 1.f);
    CHECK(view.scale == Approx(0.6f));
    CHECK(view.origin.x == Approx(-300.f));
    CHECK(view.origin.y == Approx(75.f));
}
