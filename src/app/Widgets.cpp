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

void Hint(const char* text)
{
    ImGui::SameLine();
    ImGui::TextColored(kDim, "(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}

} // namespace gv::app
