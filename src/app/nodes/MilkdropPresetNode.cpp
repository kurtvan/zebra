#include "app/nodes/NodeUi.hpp"

#include "app/Widgets.hpp"
#include "library/PresetStepping.hpp"

#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace gv::app::nodes {

namespace {

using model::PresetLayer;

auto PathOf(const model::Node& node) -> const std::filesystem::path&
{
    return model::ParamsOf<model::MilkdropPresetParams>(node).path;
}

auto FolderOf(const model::Node& node) -> const std::optional<std::string>&
{
    return model::ParamsOf<model::MilkdropPresetParams>(node).folder;
}

auto Missing(const model::Node& node, NodeContext& context) -> bool
{
    return !PathOf(node).empty() && !context.FileExists(PathOf(node));
}

/// The live output on the node that's on screen, else the library thumbnail (D5).
void DrawPicture(const model::Node& node, NodeContext& context)
{
    const auto& path = PathOf(node);
    const bool live = context.IsLivePreset(node);
    const float width = context.width;
    const float height = width * 9.f / 16.f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, height));
    const ImVec2 corner(at.x + width, at.y + height);
    const unsigned int livePicture = live ? context.LivePicture() : 0;
    const unsigned int texture = livePicture != 0 ? livePicture : context.Thumbnail(path, at, corner);
    PresetPicture(ImGui::GetWindowDrawList(), at, corner, texture, path.empty() ? "no preset" : "no preview", kDim,
                  live ? ImGui::GetColorU32(kGreen) : 0);
    context.Tooltip(live ? "On screen now." : path.string().c_str());
}

/// ◀ ▶ through the chosen folder (else the preset's own) and Random from the whole library, skipping broken
/// presets; then the Browse toggle. Each press is one undo step. Without a chosen folder, ◀ ▶ need the preset
/// to be in the library (with none chosen, they step through all of it).
void DrawStepping(const model::Node& node, NodeContext& context)
{
    const library::Library* presets = context.PresetLibrary();
    const auto& path = PathOf(node);
    const auto& folder = FolderOf(node);
    const bool any = presets != nullptr && !presets->Presets().empty();
    const bool canStep = any && (folder || path.empty() || library::FolderOf(*presets, path).has_value());
    auto step = [&](int by) {
        return folder ? library::StepPresetUnder(*presets, *folder, path, by) : library::StepPreset(*presets, path, by);
    };
    auto change = [&](const char* label, const std::optional<std::filesystem::path>& to) {
        if (!to)
            return;
        const auto chosen = *to;
        context.EditOnce(node, label, [&](model::Node& n) { model::ParamsOf<model::MilkdropPresetParams>(n).path = chosen; });
    };
    ImGui::BeginDisabled(!canStep);
    if (ImGui::ArrowButton("##previous", ImGuiDir_Left))
        change("Previous preset", step(-1));
    context.Tooltip("The previous preset in this folder.");
    ImGui::SameLine();
    if (ImGui::ArrowButton("##next", ImGuiDir_Right))
        change("Next preset", step(1));
    context.Tooltip("The next preset in this folder.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!any);
    if (ImGui::Button("Random"))
        change("Random preset", library::RandomPreset(*presets, path, context.Random()));
    ImGui::EndDisabled();
    context.Tooltip("A random preset from the whole library.");
    ImGui::SameLine();
    PresetBrowserState& browser = context.PresetBrowser(node);
    const bool open = browser.open;
    if (open)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button("Browse"))
        browser.open = !browser.open;
    if (open)
        ImGui::PopStyleColor();
    context.Tooltip("Browse the library here: choose the folder ◀ ▶ step through, and click a preset to use it.");
}

constexpr int kFolderRows = 5;  //!< Folder rows the browser shows; more scroll.
constexpr int kGridColumns = 2; //!< Pictures wide enough to render down to about 60% zoom.
constexpr int kGridRows = 3;    //!< Rows of pictures shown; more scroll.
constexpr int kListRows = 8;    //!< Names shown in the list view.
constexpr float kScrollbar = 6.f;

/// A thin bar at the right of the list from `min` to `max`, showing which of its `rows` are in view.
void DrawScrollbar(ImVec2 min, ImVec2 max, float first, int visible, int rows)
{
    if (rows <= visible)
        return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float left = max.x - kScrollbar + 2.f;
    const float height = max.y - min.y;
    draw->AddRectFilled(ImVec2(left, min.y), max, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.06f)), 2.f);
    const float top = min.y + height * std::floor(first) / static_cast<float>(rows);
    const float bottom = top + height * static_cast<float>(visible) / static_cast<float>(rows);
    draw->AddRectFilled(ImVec2(left, top), ImVec2(max.x, bottom), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.30f)), 2.f);
}

/// A preset's name and author, for its tooltip.
auto Describe(const library::PresetInfo& preset) -> std::string
{
    return preset.author.empty() ? preset.name : preset.name + "\nby " + preset.author;
}

/// What Browse opens inside the node: a search box, the folder tree (choosing a folder sets the one ◀ ▶ step
/// through) and that folder's presets as pictures or names (clicking one puts it on the node). It zooms with
/// the canvas; the tree and the presets scroll within a fixed height.
void DrawBrowser(const model::Node& node, NodeContext& context)
{
    PresetBrowserState& browser = context.PresetBrowser(node);
    if (!browser.open)
        return;
    const library::Library* presets = context.PresetLibrary();
    if (presets == nullptr || presets->Presets().empty())
    {
        ImGui::TextColored(kDim, "No preset library yet: choose its folder in Setup.");
        return;
    }
    const float width = context.width;
    const float inner = width - kScrollbar - 2.f;
    const auto& path = PathOf(node);
    const std::string current = presets->IdForPath(path);

    ImGui::SetNextItemWidth(width);
    ImGui::InputTextWithHint("##search", browser.wholeLibrary ? "Search the whole library" : "Search this folder", &browser.search);
    ImGui::Checkbox("Whole library", &browser.wholeLibrary);
    context.Tooltip("Search every preset, not just this folder's.");
    ImGui::SameLine();
    ImGui::Checkbox("List", &browser.list);
    context.Tooltip("Names in a list instead of pictures.");

    // The folders: the chosen one is selected, else the preset's own.
    const std::string folder = FolderOf(node).value_or(library::FolderOf(*presets, path).value_or(""));
    library::Query query = library::FolderQuery(folder);
    const auto rows = library::FolderRows(*presets, query.category);
    const int rowCount = static_cast<int>(rows.size());
    const int shownRows = std::min(kFolderRows, rowCount);
    const ImVec2 treeMin = ImGui::GetCursorScreenPos();
    const ImVec2 treeMax(treeMin.x + width, treeMin.y + static_cast<float>(shownRows) * ImGui::GetTextLineHeightWithSpacing());
    browser.folderScroll = ScrollRows(browser.folderScroll, context.Wheel(treeMin, treeMax), rowCount, shownRows);
    std::string category = query.category;
    std::string subcategory = query.subcategory;
    ImGui::BeginGroup();
    auto folderTooltip = [&](const library::FolderRow& row) {
        const std::string name = row.depth == 0 ? "the whole library" : library::FolderName(row.category, row.subcategory);
        context.Tooltip(("◀ ▶ step through " + name + (row.depth == 0 ? "." : " and its subfolders.")).c_str());
    };
    if (FolderTree(rows, static_cast<int>(browser.folderScroll), shownRows, inner, category, subcategory, folderTooltip))
    {
        const std::string chosen = library::FolderName(category, subcategory);
        context.EditOnce(node, "Choose Preset Folder", [&](model::Node& n) { model::ParamsOf<model::MilkdropPresetParams>(n).folder = chosen; });
        query = library::FolderQuery(chosen);
        browser.presetScroll = 0.f;
    }
    ImGui::EndGroup();
    DrawScrollbar(treeMin, treeMax, browser.folderScroll, shownRows, rowCount);
    ImGui::Separator();

    // The folder's presets, or the whole library's while searching all of it.
    query.text = browser.search;
    if (browser.wholeLibrary && !browser.search.empty())
        query.category = query.subcategory = "";
    static const std::set<std::string> noFavourites;
    const auto found = library::Search(*presets, query, noFavourites);
    if (found.empty())
    {
        ImGui::TextColored(kDim, "No presets match.");
        return;
    }
    const int count = static_cast<int>(found.size());
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float cellWidth = std::floor((inner - spacing * (kGridColumns - 1)) / kGridColumns);
    const float cellHeight = std::floor(cellWidth * 9.f / 16.f);
    const int perLine = browser.list ? 1 : kGridColumns;
    const int lines = (count + perLine - 1) / perLine;
    const int shown = std::min(browser.list ? kListRows : kGridRows, lines);
    const float lineHeight = browser.list ? ImGui::GetTextLineHeightWithSpacing() : cellHeight + ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 listMin = ImGui::GetCursorScreenPos();
    const ImVec2 listMax(listMin.x + width, listMin.y + static_cast<float>(shown) * lineHeight);
    browser.presetScroll = ScrollRows(browser.presetScroll, context.Wheel(listMin, listMax), lines, shown);
    const int first = static_cast<int>(browser.presetScroll) * perLine;
    ImGui::BeginGroup();
    for (int i = first; i < std::min(count, first + shown * perLine); ++i)
    {
        const auto& preset = presets->Presets()[static_cast<size_t>(found[static_cast<size_t>(i)])];
        const auto presetPath = presets->PathForId(preset.id);
        const bool chosen = preset.id == current;
        ImGui::PushID(preset.id.c_str());
        bool clicked = false;
        if (browser.list)
        {
            clicked = ImGui::Selectable(preset.name.c_str(), chosen, ImGuiSelectableFlags_None, ImVec2(inner, 0.f));
        }
        else
        {
            if ((i - first) % perLine != 0)
                ImGui::SameLine();
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max(min.x + cellWidth, min.y + cellHeight);
            clicked = ImGui::InvisibleButton("##preset", ImVec2(cellWidth, cellHeight));
            const bool hovered = ImGui::IsItemHovered();
            const unsigned int texture = context.Thumbnail(presetPath, min, max);
            const ImU32 outline = chosen || hovered ? ImGui::GetColorU32(chosen ? kAccent : ImVec4(1, 1, 1, 0.35f)) : 0;
            PresetPicture(ImGui::GetWindowDrawList(), min, max, texture, preset.broken ? "can't load" : "...", preset.broken ? kRed : kDim, outline);
        }
        context.Tooltip(Describe(preset).c_str());
        if (clicked && !chosen)
            context.EditOnce(node, "Change Milkdrop Preset", [&](model::Node& n) { model::ParamsOf<model::MilkdropPresetParams>(n).path = presetPath; });
        ImGui::PopID();
    }
    ImGui::EndGroup();
    DrawScrollbar(listMin, listMax, browser.presetScroll, shown, lines);
}

/// Favourite (for presets in the library), Choose..., Reveal.
void DrawButtons(const model::Node& node, NodeContext& context)
{
    const auto& path = PathOf(node);
    if (const auto favourite = path.empty() ? std::nullopt : context.IsFavourite(path))
    {
        // The star, centred on the buttons' row.
        const ImVec2 row = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(row.x, row.y + (ImGui::GetFrameHeight() - 15.f) * 0.5f));
        if (StarButton("##favourite", *favourite))
            context.ToggleFavourite(path);
        context.Tooltip("Favourite");
        ImGui::SetCursorScreenPos(ImVec2(row.x + 15.f + ImGui::GetStyle().ItemSpacing.x, row.y));
    }
    if (ImGui::Button("Choose..."))
    {
        if (auto file = context.ChooseFile("Choose a preset", {"milk"}))
        {
            const auto chosen = *file;
            context.EditOnce(node, "Change Milkdrop Preset", [&](model::Node& n) { model::ParamsOf<model::MilkdropPresetParams>(n).path = chosen; });
        }
    }
    context.Tooltip("Or drop a preset from the Presets panel on this node.");
    ImGui::SameLine();
    ImGui::BeginDisabled(path.empty() || Missing(node, context));
    if (ImGui::Button("Reveal"))
        context.Reveal(path);
    ImGui::EndDisabled();
    context.Tooltip("Show the file in Finder.");
}

void DrawFrameTime(const model::Node& node, NodeContext& context)
{
    if (!context.IsLivePreset(node))
        return;
    if (const auto time = context.FrameTime(); time && time->first > 0.0)
        ImGui::TextColored(time->second ? kAmber : kDim, "%.1f ms per frame%s", time->first, time->second ? ": too heavy" : "");
}

} // namespace

auto MilkdropPresetUi() -> NodeUi
{
    NodeUi ui;
    ui.type = model::NodeType::MilkdropPreset;
    ui.width = 260.f;
    ui.header = ImVec4(0.40f, 0.26f, 0.52f, 0.60f);
    ui.menuGroup = 2;
    ui.title = [](const model::Node& node, NodeContext&) -> std::string {
        return PathOf(node).empty() ? "Milkdrop preset (none chosen)" : PathOf(node).stem().string();
    };
    ui.status = [](const model::Node& node, NodeContext& context) -> NodeStatus {
        if (PathOf(node).empty())
            return {NodeState::Empty, "No preset chosen."};
        if (Missing(node, context))
            return {NodeState::Missing, "File not found: " + PathOf(node).string()};
        if (context.IsLivePreset(node))
            return {NodeState::Live, "On screen."};
        if (context.SceneReady())
            return {NodeState::Ready, "Ready."};
        return {};
    };
    // The three layers, then one input per texture the preset samples, plus any wired texture it no longer
    // has (so the wire shows).
    ui.inputs = [](const model::Node& node, NodeContext& context) {
        std::vector<InputSpec> inputs = {
            {static_cast<int>(PresetLayer::Feedback), {}, model::PortKind::Video, "Feedback"},
            {static_cast<int>(PresetLayer::BeforeComposite), {}, model::PortKind::Video, "Before composite"},
            {static_cast<int>(PresetLayer::OnTop), {}, model::PortKind::Video, "On top"},
        };
        std::vector<std::string> names = context.TexturesOf(PathOf(node));
        for (const auto& wire : context.SceneGraph().wires)
        {
            if (wire.toNode == node.id && wire.toInput == static_cast<int>(PresetLayer::Texture) &&
                std::find(names.begin(), names.end(), wire.textureName) == names.end())
                names.push_back(wire.textureName);
        }
        for (const auto& name : names)
            inputs.push_back({static_cast<int>(PresetLayer::Texture), name, model::PortKind::Video, "Texture: " + name});
        return inputs;
    };

    Control picture;
    picture.id = "##picture";
    picture.draw = DrawPicture;
    Control stepping;
    stepping.id = "##stepping";
    stepping.draw = DrawStepping;
    Control browser;
    browser.id = "##browser";
    browser.draw = DrawBrowser;
    Control buttons;
    buttons.id = "##buttons";
    buttons.draw = DrawButtons;
    Control missing;
    missing.kind = Control::Kind::Text;
    missing.warning = true;
    missing.text = [](const model::Node& node, NodeContext& context) -> std::string { return Missing(node, context) ? "File not found." : ""; };
    Control frameTime;
    frameTime.id = "##frame-time";
    frameTime.draw = DrawFrameTime;
    ui.controls = {picture, stepping, browser, buttons, missing, frameTime, {Control::Kind::Inputs}};
    ui.addItems = {{"Milkdrop preset...", nullptr, "Or drag a preset from the Presets panel onto the canvas.", true}};
    return ui;
}

} // namespace gv::app::nodes
