#include "app/MiniMap.hpp"
#include "app/MiniMapWidget.hpp"
#include "app/Settings.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

using Catch::Approx;
using gv::app::CanvasRect;
using gv::app::MiniMap;
namespace ed = ax::NodeEditor;

namespace {

/// A map of the canvas area (0,0)–(1000,500) in an 800x600 canvas at the screen's top left.
auto WideMap() -> MiniMap
{
    return *gv::app::PlaceMiniMap({ImVec2(0.f, 0.f), ImVec2(1000.f, 500.f)}, ImVec2(0.f, 0.f), ImVec2(800.f, 600.f));
}

} // namespace

TEST_CASE("The mini-map covers every node, with a margin")
{
    CHECK_FALSE(gv::app::GraphBounds({}, 40.f));
    const auto bounds = gv::app::GraphBounds({{ImVec2(100.f, 50.f), ImVec2(300.f, 150.f)}, {ImVec2(-20.f, 400.f), ImVec2(80.f, 470.f)}}, 40.f);
    REQUIRE(bounds);
    CHECK(bounds->min.x == -60.f);
    CHECK(bounds->min.y == 10.f);
    CHECK(bounds->max.x == 340.f);
    CHECK(bounds->max.y == 510.f);
}

TEST_CASE("The mini-map sits in the canvas's bottom-right corner and keeps the graph's shape")
{
    const MiniMap wide = WideMap();
    CHECK(wide.max.x == 800.f - gv::app::kMiniMapInset);
    CHECK(wide.max.y == 600.f - gv::app::kMiniMapInset);
    CHECK(wide.max.x - wide.min.x == Approx(gv::app::kMiniMapWidth));
    CHECK(wide.max.y - wide.min.y == Approx(gv::app::kMiniMapWidth / 2.f));

    // A tall graph is limited by the map's height instead.
    const auto tall = gv::app::PlaceMiniMap({ImVec2(0.f, 0.f), ImVec2(300.f, 600.f)}, ImVec2(0.f, 0.f), ImVec2(800.f, 600.f));
    REQUIRE(tall);
    CHECK(tall->max.y - tall->min.y == Approx(gv::app::kMiniMapMaxHeight));
    CHECK(tall->max.x - tall->min.x == Approx(gv::app::kMiniMapMaxHeight / 2.f));

    // A long thin row of nodes still gets a map tall enough to click, with the row across its middle.
    const auto thin = gv::app::PlaceMiniMap({ImVec2(0.f, 0.f), ImVec2(4000.f, 100.f)}, ImVec2(0.f, 0.f), ImVec2(800.f, 600.f));
    REQUIRE(thin);
    CHECK(thin->max.y - thin->min.y == Approx(gv::app::kMiniMapMinSide));
    CHECK(gv::app::CanvasToMap(*thin, ImVec2(2000.f, 50.f)).y == Approx((thin->min.y + thin->max.y) * 0.5f));

    // No room for it on a tiny canvas.
    CHECK_FALSE(gv::app::PlaceMiniMap({ImVec2(0.f, 0.f), ImVec2(1000.f, 500.f)}, ImVec2(0.f, 0.f), ImVec2(190.f, 600.f)));
}

TEST_CASE("Mini-map and canvas points round-trip")
{
    const MiniMap map = WideMap();
    for (const ImVec2 canvas : {ImVec2(0.f, 0.f), ImVec2(1000.f, 500.f), ImVec2(333.3f, 87.5f), ImVec2(-200.f, 900.f)})
    {
        const ImVec2 back = gv::app::MapToCanvas(map, gv::app::CanvasToMap(map, canvas));
        CHECK(back.x == Approx(canvas.x).margin(1e-3));
        CHECK(back.y == Approx(canvas.y).margin(1e-3));
    }
    for (const ImVec2 screen : {map.min, map.max, ImVec2(700.f, 550.f)})
    {
        const ImVec2 back = gv::app::CanvasToMap(map, gv::app::MapToCanvas(map, screen));
        CHECK(back.x == Approx(screen.x).margin(1e-3));
        CHECK(back.y == Approx(screen.y).margin(1e-3));
    }
    // The graph's corners are the map's corners.
    CHECK(gv::app::CanvasToMap(map, ImVec2(0.f, 0.f)).x == Approx(map.min.x));
    CHECK(gv::app::CanvasToMap(map, ImVec2(1000.f, 500.f)).y == Approx(map.max.y));
}

TEST_CASE("A click outside the mini-map does nothing; a drag past its edge stops there")
{
    const MiniMap map = WideMap();
    CHECK_FALSE(gv::app::MiniMapClick(map, ImVec2(map.min.x - 1.f, map.min.y + 5.f)));
    CHECK_FALSE(gv::app::MiniMapClick(map, ImVec2(400.f, 300.f)));
    CHECK_FALSE(gv::app::MiniMapClick(map, map.max));

    const auto centre = gv::app::MiniMapClick(map, ImVec2((map.min.x + map.max.x) * 0.5f, (map.min.y + map.max.y) * 0.5f));
    REQUIRE(centre);
    CHECK(centre->x == Approx(500.f));
    CHECK(centre->y == Approx(250.f));

    const ImVec2 past = gv::app::MiniMapDrag(map, ImVec2(map.max.x + 300.f, map.min.y - 50.f));
    CHECK(past.x == Approx(1000.f));
    CHECK(past.y == Approx(0.f).margin(1e-3));
}

TEST_CASE("Centring the view keeps its size")
{
    const CanvasRect view{ImVec2(-37.25f, 12.5f), ImVec2(763.25f, 613.1f)};
    const CanvasRect moved = gv::app::CentredOn(view, ImVec2(1000.f, -40.f));
    CHECK((moved.min.x + moved.max.x) * 0.5f == Approx(1000.f));
    CHECK((moved.min.y + moved.max.y) * 0.5f == Approx(-40.f));
    CHECK(moved.max.x - moved.min.x == Approx(view.max.x - view.min.x));
    CHECK(moved.max.y - moved.min.y == Approx(view.max.y - view.min.y));
}

namespace {

struct ClickResult
{
    bool nodeSelected{false};
    bool backgroundClicked{false};
    ImVec2 centre;    //!< The view's centre after the click, in canvas units.
    float zoomBefore{0.f};
    float zoomAfter{0.f};
};

/// Clicks at `at` on an 800x600 node editor with one node under the mini-map, with or without the map.
auto ClickCanvas(ImVec2 at, bool withMap, MiniMap* placed = nullptr) -> ClickResult
{
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1000.f, 800.f);
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ed::Config config;
    config.SettingsName = nullptr;
    ed::EditorContext* editor = ed::CreateEditor(&config);

    const CanvasRect node{ImVec2(600.f, 450.f), ImVec2(776.f, 576.f)}; // under the map at 100%
    gv::app::EditorView view;
    ClickResult result;
    for (int frame = 0; frame < 12; ++frame)
    {
        io.DeltaTime = 1.f / 60.f;
        io.AddMousePosEvent(at.x, at.y);
        if (frame == 6 || frame == 8)
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, frame == 6);
        ImGui::NewFrame();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("canvas", nullptr, ImGuiWindowFlags_NoDecoration);
        ed::SetCurrentEditor(editor);
        ed::Begin("graph", ImVec2(800.f, 600.f));
        if (frame < 3)
            ed::SetViewState({ImVec2(0.f, 0.f), ImVec2(800.f, 600.f)});
        if (frame == 5)
            result.zoomBefore = ed::GetCurrentZoom();
        std::optional<gv::app::MiniMapFrame> map;
        if (withMap)
            map = gv::app::MiniMapInput({node}, ImVec2(0.f, 0.f), ImVec2(800.f, 600.f), view);
        if (map && placed != nullptr)
            *placed = map->map;
        ed::SetNodePosition(1, node.min);
        ed::BeginNode(1);
        ImGui::Dummy(ImVec2(160.f, 110.f));
        ed::EndNode();
        ed::End();
        result.backgroundClicked = result.backgroundClicked || ed::GetBackgroundClickButtonIndex() >= 0;
        ed::SetCurrentEditor(nullptr);
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::Render();
    }
    ed::SetCurrentEditor(editor);
    result.nodeSelected = ed::IsNodeSelected(1);
    const ed::ViewState shown = ed::GetViewState();
    result.centre = ImVec2((shown.VisibleMin.x + shown.VisibleMax.x) * 0.5f, (shown.VisibleMin.y + shown.VisibleMax.y) * 0.5f);
    result.zoomAfter = ed::GetCurrentZoom();
    ed::SetCurrentEditor(nullptr);
    ed::DestroyEditor(editor);
    ImGui::DestroyContext(context);
    return result;
}

} // namespace

TEST_CASE("In the real node editor, a click on the mini-map centres the view there and the canvas ignores it")
{
    const ImVec2 at(700.f, 530.f); // on the map, and on the node under it

    // Without the map, the same click selects the node.
    CHECK(ClickCanvas(at, false).nodeSelected);

    MiniMap map;
    const ClickResult clicked = ClickCanvas(at, true, &map);
    CHECK_FALSE(clicked.nodeSelected);
    CHECK_FALSE(clicked.backgroundClicked);
    const ImVec2 wanted = gv::app::MapToCanvas(map, at);
    CHECK(clicked.centre.x == Approx(wanted.x).margin(1.f));
    CHECK(clicked.centre.y == Approx(wanted.y).margin(1.f));
    CHECK(clicked.zoomAfter == Approx(clicked.zoomBefore));

    // Off the map, the canvas still gets its clicks.
    CHECK(ClickCanvas(ImVec2(300.f, 200.f), true).backgroundClicked);
}

TEST_CASE("The mini-map is shown by default and the choice is remembered")
{
    const auto dir = std::filesystem::temp_directory_path() / ("gv-minimap-" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    const auto file = dir / "settings.json";

    CHECK(gv::app::Settings{}.miniMap);
    {
        std::ofstream(file) << "{}";
    }
    CHECK(gv::app::Settings::Load(file).miniMap); // older settings files

    gv::app::Settings settings;
    settings.miniMap = false;
    REQUIRE(settings.Save(file).empty());
    CHECK_FALSE(gv::app::Settings::Load(file).miniMap);

    std::filesystem::remove_all(dir);
}
