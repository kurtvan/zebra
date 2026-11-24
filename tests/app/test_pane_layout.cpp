#include "app/PaneLayout.hpp"
#include "app/Settings.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>

using gv::app::PaneArea;
using gv::app::PaneLayout;
using gv::app::Settings;

namespace {

auto TempFile(const char* name) -> std::filesystem::path
{
    const auto dir = std::filesystem::temp_directory_path() / ("gv_pane_layout_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(dir);
    return dir / name;
}

} // namespace

TEST_CASE("Pane layout: everything is shown by default")
{
    const PaneLayout layout;
    CHECK_FALSE(layout.IsCollapsed(PaneArea::Left));
    CHECK_FALSE(layout.IsCollapsed(PaneArea::Right));
    CHECK_FALSE(layout.IsCollapsed(PaneArea::Bottom));
    CHECK_FALSE(layout.focus);
}

TEST_CASE("Pane layout: each area toggles on its own, and twice gives back the start")
{
    PaneLayout layout;
    layout.Toggle(PaneArea::Right);
    CHECK_FALSE(layout.IsCollapsed(PaneArea::Left));
    CHECK(layout.IsCollapsed(PaneArea::Right));
    CHECK_FALSE(layout.IsCollapsed(PaneArea::Bottom));
    layout.Toggle(PaneArea::Right);
    CHECK(layout == PaneLayout());
}

TEST_CASE("Pane layout: focus mode collapses everything and brings back exactly what was open")
{
    PaneLayout layout;
    layout.Toggle(PaneArea::Bottom); // bottom collapsed by hand; left and right open
    const PaneLayout before = layout;

    layout.ToggleFocus();
    CHECK(layout.focus);
    CHECK(layout.IsCollapsed(PaneArea::Left));
    CHECK(layout.IsCollapsed(PaneArea::Right));
    CHECK(layout.IsCollapsed(PaneArea::Bottom));

    layout.ToggleFocus();
    CHECK_FALSE(layout.focus);
    CHECK(layout.collapsed == before.collapsed); // the bottom stays collapsed: it was before focus mode
}

TEST_CASE("Pane layout: showing one area ends focus mode and keeps the others collapsed")
{
    PaneLayout layout;
    layout.ToggleFocus();
    layout.Toggle(PaneArea::Left);
    CHECK_FALSE(layout.focus);
    CHECK_FALSE(layout.IsCollapsed(PaneArea::Left));
    CHECK(layout.IsCollapsed(PaneArea::Right));
    CHECK(layout.IsCollapsed(PaneArea::Bottom));

    // Focus again collapses the left one too, and turning it off brings back only the left one.
    layout.ToggleFocus();
    CHECK(layout.IsCollapsed(PaneArea::Left));
    layout.ToggleFocus();
    CHECK_FALSE(layout.IsCollapsed(PaneArea::Left));
    CHECK(layout.IsCollapsed(PaneArea::Right));
}

TEST_CASE("Pane layout: focus with everything already collapsed by hand shows everything")
{
    PaneLayout layout;
    layout.Toggle(PaneArea::Left);
    layout.Toggle(PaneArea::Right);
    layout.Toggle(PaneArea::Bottom);
    layout.ToggleFocus();
    CHECK(layout == PaneLayout());
}

TEST_CASE("Pane layout: JSON round-trips, focus memory included")
{
    PaneLayout layout;
    layout.Toggle(PaneArea::Right);
    layout.ToggleFocus();
    const auto restored = PaneLayout::FromJson(layout.ToJson());
    CHECK(restored == layout);
    CHECK(restored.beforeFocus[static_cast<size_t>(PaneArea::Right)]);
}

TEST_CASE("Pane layout: bad or missing JSON falls back to everything shown")
{
    CHECK(PaneLayout::FromJson(nlohmann::json()) == PaneLayout());
    CHECK(PaneLayout::FromJson(nlohmann::json::array()) == PaneLayout());
    CHECK(PaneLayout::FromJson(nlohmann::json{{"collapsed", {{"left", "yes"}, {"right", true}}}}).collapsed ==
          std::array<bool, 3>{false, true, false});
    // Focus mode always means every area is collapsed.
    CHECK(PaneLayout::FromJson(nlohmann::json{{"focus", true}}).collapsed == std::array<bool, 3>{true, true, true});
}

TEST_CASE("Settings: the collapsed areas survive a save and load")
{
    const auto file = TempFile("settings.json");
    Settings settings;
    settings.gain = 2.0f;
    settings.panes.Toggle(PaneArea::Left);
    settings.panes.ToggleFocus();
    REQUIRE(settings.Save(file).empty());

    const auto loaded = Settings::Load(file);
    CHECK(loaded.panes == settings.panes);
    CHECK(loaded.gain == 2.0f);
    std::filesystem::remove_all(file.parent_path());
}

TEST_CASE("Settings: files from before collapsible areas show everything")
{
    const auto file = TempFile("settings.json");
    {
        std::ofstream out(file);
        out << R"({"gain": 1.5, "namesOnly": true})";
    }
    const auto loaded = Settings::Load(file);
    CHECK(loaded.panes == PaneLayout());
    CHECK(loaded.namesOnly);
    std::filesystem::remove_all(file.parent_path());
}
