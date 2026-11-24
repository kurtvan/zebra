#include "app/CanvasZoom.hpp"
#include "app/EditorView.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using Catch::Approx;
using gv::app::CanvasRect;
using gv::app::ZoomAt;
namespace ed = ax::NodeEditor;

namespace {

/// Screen pixels per canvas unit when `view` fills a canvas `width` pixels wide.
auto ZoomOf(const CanvasRect& view, float width) -> float
{
    return width / (view.max.x - view.min.x);
}

/// Where `point` sits inside `view`, from 0 (top left) to 1 (bottom right) on each axis.
auto Fraction(const CanvasRect& view, ImVec2 point) -> ImVec2
{
    return {(point.x - view.min.x) / (view.max.x - view.min.x), (point.y - view.min.y) / (view.max.y - view.min.y)};
}

auto Aspect(const CanvasRect& view) -> float
{
    return (view.max.x - view.min.x) / (view.max.y - view.min.y);
}

} // namespace

TEST_CASE("Zooming keeps the canvas point under the pointer still")
{
    const CanvasRect before{ImVec2(-120.f, 45.f), ImVec2(880.f, 795.f)}; // 800x600 pixels at 80%
    const ImVec2 under(267.5f, 320.f);
    for (float factor : {1.3f, 0.7f, 1.05f, 0.95f})
    {
        const CanvasRect after = ZoomAt(before, 0.8f, under, factor);
        CHECK(ZoomOf(after, 800.f) == Approx(0.8f * factor));
        CHECK(Fraction(after, under).x == Approx(Fraction(before, under).x));
        CHECK(Fraction(after, under).y == Approx(Fraction(before, under).y));
        CHECK(Aspect(after) == Approx(Aspect(before)));
    }
}

TEST_CASE("Zooming clamps at 25% and 200%, still around the pointer")
{
    const CanvasRect view{ImVec2(10.f, 20.f), ImVec2(410.f, 320.f)}; // 720x540 pixels at 180%
    const ImVec2 under(100.f, 50.f);

    const CanvasRect in = ZoomAt(view, 1.8f, under, 4.f);
    CHECK(ZoomOf(in, 720.f) == Approx(2.f));
    CHECK(Fraction(in, under).x == Approx(Fraction(view, under).x));
    CHECK(Fraction(in, under).y == Approx(Fraction(view, under).y));

    const CanvasRect out = ZoomAt(view, 1.8f, under, 0.01f);
    CHECK(ZoomOf(out, 720.f) == Approx(0.25f));
    CHECK(Fraction(out, under).x == Approx(Fraction(view, under).x));

    // Already at a limit: zooming further changes nothing.
    const CanvasRect stuck = ZoomAt(view, 2.f, under, 1.5f);
    CHECK(stuck.min.x == Approx(view.min.x));
    CHECK(stuck.min.y == Approx(view.min.y));
    CHECK(stuck.max.x == Approx(view.max.x));
    CHECK(stuck.max.y == Approx(view.max.y));
}

TEST_CASE("Zooming by a factor of 1 changes nothing")
{
    const CanvasRect view{ImVec2(-300.f, 75.f), ImVec2(1406.67f, 1355.f)};
    const CanvasRect same = ZoomAt(view, 0.6f, ImVec2(512.f, 384.f), 1.f);
    CHECK(same.min.x == Approx(view.min.x));
    CHECK(same.min.y == Approx(view.min.y));
    CHECK(same.max.x == Approx(view.max.x));
    CHECK(same.max.y == Approx(view.max.y));
}

TEST_CASE("A long pinch in and back out doesn't drift or stretch the view")
{
    // A pinch zooms a little every frame; any error per step builds up.
    const CanvasRect start{ImVec2(-37.25f, 12.5f), ImVec2(1563.75f, 1213.7f)}; // a fractional 800.5x600.6 at 50%
    const ImVec2 under(1400.f, 900.f); // near the right edge
    CanvasRect view = start;
    float zoom = 0.5f;
    for (int i = 0; i < 240; ++i)
    {
        const float factor = i < 120 ? 1.01f : 1.f / 1.01f;
        view = ZoomAt(view, zoom, under, factor);
        zoom *= factor;
        CHECK(Fraction(view, under).x == Approx(Fraction(start, under).x).margin(1e-4));
        CHECK(Fraction(view, under).y == Approx(Fraction(start, under).y).margin(1e-4));
        CHECK(Aspect(view) == Approx(Aspect(start)).epsilon(1e-4));
    }
    CHECK(view.min.x == Approx(start.min.x).margin(0.05));
    CHECK(view.max.x == Approx(start.max.x).margin(0.05));
}

TEST_CASE("In the real node editor, a long pinch keeps the point under the pointer still")
{
    // The editor snaps its view to whole pixels and floors its canvas size (the graph pane's is often fractional).
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1200.f, 900.f);
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ed::Config config;
    config.SettingsName = nullptr;
    config.CustomZoomLevels.clear();
    for (float level : {0.25f, 0.5f, 1.0f, 1.5f, 2.0f})
        config.CustomZoomLevels.push_back(level);
    ed::EditorContext* editor = ed::CreateEditor(&config);

    const ImVec2 size(800.6f, 600.3f);
    const ImVec2 mouse(760.f, 400.f); // near the canvas's right edge
    io.MousePos = mouse;
    gv::app::EditorView view;
    ImVec2 under; // the canvas point under the pointer when the pinch starts
    float worst = 0.f;
    ImVec2 last;
    for (int frame = 0; frame < 250; ++frame)
    {
        io.DeltaTime = 1.f / 60.f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("canvas", nullptr, ImGuiWindowFlags_NoDecoration);
        ed::SetCurrentEditor(editor);
        ed::Begin("graph", size);
        if (frame == 5)
            under = ed::ScreenToCanvas(mouse);
        if (frame >= 5)
        {
            // Snapping to whole pixels may move it by less than a pixel, but that mustn't add up.
            last = ed::CanvasToScreen(under);
            worst = std::max({worst, std::abs(last.x - mouse.x), std::abs(last.y - mouse.y)});
        }
        if (frame >= 5 && frame < 245) // 120 frames in, then 120 back out
            view.Zoom(frame < 125 ? 1.005f : 1.f / 1.005f, mouse);
        ed::End();
        ed::SetCurrentEditor(nullptr);
        ImGui::End();
        ImGui::Render();
    }
    CHECK(worst <= 1.f);
    CHECK(last.x == Approx(mouse.x).margin(1.0));
    CHECK(last.y == Approx(mouse.y).margin(1.0));

    ed::DestroyEditor(editor);
    ImGui::DestroyContext(context);
}

TEST_CASE("Inside the node editor, ImGui's mouse is in canvas coordinates, not on screen")
{
    // GraphEditor must read the pointer for zooming before ed::Begin: reading it after zooms at the wrong point.
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1200.f, 900.f);
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ed::Config config;
    config.SettingsName = nullptr;
    ed::EditorContext* editor = ed::CreateEditor(&config);

    const ImVec2 mouse(760.f, 400.f);
    io.MousePos = mouse;
    ImVec2 inside;
    for (int frame = 0; frame < 3; ++frame)
    {
        io.DeltaTime = 1.f / 60.f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(100.f, 80.f)); // the canvas doesn't start at the screen's corner
        ImGui::SetNextWindowSize(ImVec2(1000.f, 800.f));
        ImGui::Begin("canvas", nullptr, ImGuiWindowFlags_NoDecoration);
        ed::SetCurrentEditor(editor);
        ed::Begin("graph", ImVec2(800.6f, 600.3f));
        if (frame == 1)
            ed::SetViewState({ImVec2(-500.f, -300.f), ImVec2(300.6f, 300.3f)});
        inside = io.MousePos;
        ed::End();
        ed::SetCurrentEditor(nullptr);
        ImGui::End();
        ImGui::Render();
    }
    CHECK(io.MousePos.x == mouse.x); // restored after the editor
    CHECK(io.MousePos.y == mouse.y);
    CHECK(std::abs(inside.x - mouse.x) + std::abs(inside.y - mouse.y) > 50.f);

    ed::DestroyEditor(editor);
    ImGui::DestroyContext(context);
}
