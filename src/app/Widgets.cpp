#include "app/Widgets.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace gv::app {

auto Lower(std::string text) -> std::string
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

auto IsVideoFile(const std::filesystem::path& file) -> bool
{
    const auto extension = Lower(file.extension().string());
    return extension == ".mp4" || extension == ".mov" || extension == ".m4v";
}

auto StarButton(const char* id, bool on, float size) -> bool
{
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 center(position.x + size * 0.5f, position.y + size * 0.5f + 0.5f);
    ImVec2 points[10];
    for (int i = 0; i < 10; ++i)
    {
        const float radius = (i % 2 == 0) ? size * 0.5f : size * 0.21f;
        const float angle = -1.5707963f + static_cast<float>(i) * 0.6283185f;
        points[i] = ImVec2(center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius);
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 colour = ImGui::GetColorU32(on ? kAmber : (hovered ? ImVec4(0.8f, 0.8f, 0.85f, 1.f) : ImVec4(0.42f, 0.44f, 0.48f, 1.f)));
    if (on)
        draw->AddConcavePolyFilled(points, 10, colour);
    else
        draw->AddPolyline(points, 10, colour, ImDrawFlags_Closed, 1.2f);
    return pressed;
}

void PresetDragSource(const std::filesystem::path& preset, const std::string& label)
{
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
    {
        const std::string path = preset.string();
        ImGui::SetDragDropPayload(kPresetDragPayload, path.c_str(), path.size() + 1);
        ImGui::Text("%s", label.c_str());
        ImGui::TextColored(kDim, "Drop on a scene to use it, below the list to add a scene,\non the preview to put it in the live scene, or on the Graph");
        ImGui::EndDragDropSource();
    }
}

void PresetPicture(ImDrawList* draw, ImVec2 min, ImVec2 max, unsigned int texture, const char* placeholder, ImVec4 colour, ImU32 outline)
{
    draw->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(0.07f, 0.07f, 0.08f, 1.f)), 4.f);
    if (texture != 0)
    {
        draw->AddImageRounded(static_cast<ImTextureID>(texture), min, max, ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, 4.f); // GL is bottom-up
    }
    else
    {
        const ImVec2 size = ImGui::CalcTextSize(placeholder);
        draw->AddText(ImVec2(min.x + (max.x - min.x - size.x) * 0.5f, min.y + (max.y - min.y - size.y) * 0.5f), ImGui::GetColorU32(colour), placeholder);
    }
    if (outline != 0)
        draw->AddRect(min, max, outline, 4.f, 0, 2.f);
}

auto FolderTree(std::span<const library::FolderRow> rows, int first, int count, float width, std::string& category, std::string& subcategory,
                const std::function<void(const library::FolderRow&)>& afterRow) -> bool
{
    bool changed = false;
    const int end = std::min(static_cast<int>(rows.size()), first + count);
    for (int i = std::max(first, 0); i < end; ++i)
    {
        const auto& row = rows[static_cast<size_t>(i)];
        const std::string& name = row.depth == 2 ? row.subcategory : row.category;
        const std::string label = (row.depth == 0 ? std::string("All presets") : name) + "  " + std::to_string(row.count) + "##" + row.category + "/" +
                                  row.subcategory;
        const bool selected = row.category == category && row.subcategory == subcategory && (row.depth > 0 || category.empty());
        const float indent = static_cast<float>(std::max(row.depth - 1, 0)) * 12.f;
        if (indent > 0.f)
            ImGui::Indent(indent);
        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_None, ImVec2(width - indent, 0.f)) && !selected)
        {
            category = row.category;
            subcategory = row.subcategory;
            changed = true;
        }
        if (afterRow)
            afterRow(row);
        if (indent > 0.f)
            ImGui::Unindent(indent);
    }
    return changed;
}

auto ScrollRows(float position, float wheel, int rows, int visible) -> float
{
    const float last = static_cast<float>(std::max(rows - visible, 0));
    return std::clamp(position - wheel, 0.f, last);
}

void Hint(const char* text)
{
    ImGui::SameLine();
    ImGui::TextColored(kDim, "(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}

} // namespace gv::app
