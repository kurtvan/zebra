#include "app/PaneLayout.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace gv::app {

using nlohmann::json;

namespace {

constexpr const char* kAreaKeys[kPaneAreaCount] = {"left", "right", "bottom"};

auto Index(PaneArea area) -> size_t
{
    return static_cast<size_t>(area);
}

auto AreasToJson(const std::array<bool, kPaneAreaCount>& areas) -> json
{
    json out = json::object();
    for (size_t i = 0; i < areas.size(); ++i)
        out[kAreaKeys[i]] = areas[i];
    return out;
}

auto AreasFromJson(const json& root, const char* key) -> std::array<bool, kPaneAreaCount>
{
    std::array<bool, kPaneAreaCount> areas{};
    const auto it = root.find(key);
    if (it == root.end() || !it->is_object())
        return areas;
    for (size_t i = 0; i < areas.size(); ++i)
    {
        const auto area = it->find(kAreaKeys[i]);
        if (area != it->end() && area->is_boolean())
            areas[i] = area->get<bool>();
    }
    return areas;
}

} // namespace

auto PaneLayout::IsCollapsed(PaneArea area) const -> bool
{
    return collapsed[Index(area)];
}

void PaneLayout::Toggle(PaneArea area)
{
    collapsed[Index(area)] = !collapsed[Index(area)];
    focus = false;
}

void PaneLayout::ToggleFocus()
{
    if (focus)
    {
        collapsed = beforeFocus;
        focus = false;
        return;
    }
    if (std::all_of(collapsed.begin(), collapsed.end(), [](bool c) { return c; }))
    {
        collapsed.fill(false);
        return;
    }
    beforeFocus = collapsed;
    collapsed.fill(true);
    focus = true;
}

auto PaneLayout::ToJson() const -> json
{
    json out;
    out["collapsed"] = AreasToJson(collapsed);
    out["focus"] = focus;
    out["beforeFocus"] = AreasToJson(beforeFocus);
    return out;
}

auto PaneLayout::FromJson(const json& root) -> PaneLayout
{
    PaneLayout layout;
    if (!root.is_object())
        return layout;
    layout.collapsed = AreasFromJson(root, "collapsed");
    layout.beforeFocus = AreasFromJson(root, "beforeFocus");
    const auto focus = root.find("focus");
    layout.focus = focus != root.end() && focus->is_boolean() && focus->get<bool>();
    if (layout.focus)
        layout.collapsed.fill(true); // focus mode means everything is collapsed, whatever the file says
    return layout;
}

} // namespace gv::app
