#include "app/Shell.hpp"

#include "app/macos/FileDialog.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace gv::app {

namespace {

constexpr const char* kPreview = "Preview";
constexpr const char* kScenes = "Scenes";
constexpr const char* kProperties = "Scene";
constexpr const char* kSetup = "Setup";
constexpr const char* kBrowser = "Presets";
constexpr const char* kStatus = "Status";
constexpr const char* kMessages = "Messages";
constexpr const char* kSceneDragPayload = "GV_SCENE";
constexpr const char* kPresetDragPayload = "GV_PRESET"; //!< Payload: a preset's absolute path (NUL-terminated).

const ImVec4 kAccent{0.29f, 0.72f, 0.70f, 1.f};
const ImVec4 kGreen{0.36f, 0.82f, 0.48f, 1.f};
const ImVec4 kAmber{0.96f, 0.72f, 0.30f, 1.f};
const ImVec4 kRed{0.96f, 0.42f, 0.42f, 1.f};
const ImVec4 kDim{0.58f, 0.60f, 0.64f, 1.f};

const std::vector<std::string> kVideoExtensions = {"mp4", "mov", "m4v"};

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

auto StateLabel(const engine::Engine::SceneInfo& info, bool pending, ImVec4& color) -> const char*
{
    using State = engine::ScenePool::State;
    color = kDim;
    if (pending)
    {
        color = kAmber;
        return "going live";
    }
    switch (info.state)
    {
        case State::Live:
            color = kGreen;
            return "LIVE";
        case State::Ready:
            color = ImVec4(0.85f, 0.86f, 0.9f, 1.f);
            return "ready";
        case State::Preparing:
            return "preparing";
        case State::Failed:
            color = kRed;
            return "failed";
        case State::Unusable:
            color = kRed;
            return "preset missing";
        case State::None:
            return "";
    }
    return "";
}

/// A small star toggle drawn with the draw list (the UI font has no star glyph).
auto StarButton(const char* id, bool on, float size = 15.f) -> bool
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

/// Starts dragging a preset (to drop it on a scene).
void PresetDragSource(const std::filesystem::path& preset, const std::string& label)
{
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
    {
        const std::string path = preset.string();
        ImGui::SetDragDropPayload(kPresetDragPayload, path.c_str(), path.size() + 1);
        ImGui::Text("%s", label.c_str());
        ImGui::TextColored(kDim, "Drop on a scene to use it, or below the list to add a scene");
        ImGui::EndDragDropSource();
    }
}

/// A one-line help marker.
void Hint(const char* text)
{
    ImGui::SameLine();
    ImGui::TextColored(kDim, "(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}

} // namespace

void Shell::ApplyTheme()
{
    ImGuiIO& io = ImGui::GetIO();
    // The system UI font where available (it's on every Mac; not redistributed), else ImGui's default.
    for (const char* path : {"/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Helvetica.ttc"})
    {
        if (std::filesystem::exists(path) && io.Fonts->AddFontFromFileTTF(path, 15.0f) != nullptr)
            break;
    }

    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    style.WindowPadding = ImVec2(12, 10);
    style.FramePadding = ImVec2(9, 6);
    style.ItemSpacing = ImVec2(9, 7);
    style.ItemInnerSpacing = ImVec2(7, 5);
    style.CellPadding = ImVec2(7, 6);
    style.ScrollbarSize = 13;
    style.GrabMinSize = 12;
    style.WindowRounding = 6;
    style.ChildRounding = 6;
    style.FrameRounding = 5;
    style.PopupRounding = 6;
    style.ScrollbarRounding = 6;
    style.GrabRounding = 5;
    style.TabRounding = 5;
    style.WindowBorderSize = 0;
    style.FrameBorderSize = 0;
    style.TabBarBorderSize = 0;
    style.DockingSeparatorSize = 3;

    ImVec4* c = style.Colors;
    const ImVec4 bg{0.105f, 0.11f, 0.12f, 1.f};
    const ImVec4 panel{0.14f, 0.145f, 0.16f, 1.f};
    const ImVec4 raised{0.19f, 0.20f, 0.22f, 1.f};
    const ImVec4 hover{0.24f, 0.25f, 0.28f, 1.f};
    c[ImGuiCol_Text] = ImVec4(0.92f, 0.93f, 0.95f, 1.f);
    c[ImGuiCol_TextDisabled] = kDim;
    c[ImGuiCol_WindowBg] = panel;
    c[ImGuiCol_ChildBg] = panel;
    c[ImGuiCol_PopupBg] = ImVec4(0.16f, 0.17f, 0.19f, 0.98f);
    c[ImGuiCol_MenuBarBg] = bg;
    c[ImGuiCol_DockingEmptyBg] = bg;
    c[ImGuiCol_Border] = ImVec4(0.22f, 0.23f, 0.25f, 1.f);
    c[ImGuiCol_FrameBg] = raised;
    c[ImGuiCol_FrameBgHovered] = hover;
    c[ImGuiCol_FrameBgActive] = hover;
    c[ImGuiCol_TitleBg] = bg;
    c[ImGuiCol_TitleBgActive] = bg;
    c[ImGuiCol_TitleBgCollapsed] = bg;
    c[ImGuiCol_Tab] = bg;
    c[ImGuiCol_TabHovered] = hover;
    c[ImGuiCol_TabSelected] = panel;
    c[ImGuiCol_TabDimmed] = bg;
    c[ImGuiCol_TabDimmedSelected] = panel;
    c[ImGuiCol_TabSelectedOverline] = kAccent;
    c[ImGuiCol_Button] = raised;
    c[ImGuiCol_ButtonHovered] = hover;
    c[ImGuiCol_ButtonActive] = ImVec4(0.29f, 0.31f, 0.34f, 1.f);
    c[ImGuiCol_Header] = ImVec4(0.20f, 0.36f, 0.36f, 0.85f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.30f, 0.31f, 1.f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.24f, 0.42f, 0.42f, 1.f);
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_SliderGrab] = kAccent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.39f, 0.84f, 0.82f, 1.f);
    c[ImGuiCol_Separator] = ImVec4(0.22f, 0.23f, 0.25f, 1.f);
    c[ImGuiCol_SeparatorHovered] = kAccent;
    c[ImGuiCol_SeparatorActive] = kAccent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_DockingPreview] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.5f);
    c[ImGuiCol_TableHeaderBg] = bg;
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
    c[ImGuiCol_PlotLines] = kAccent;
    c[ImGuiCol_PlotHistogram] = kAccent;
    c[ImGuiCol_DragDropTarget] = kAccent;
    c[ImGuiCol_NavCursor] = kAccent;
}

void Shell::Draw(ShellState& state, AppActions& actions)
{
    if (!m_listsLoaded)
    {
        m_audioDevices = io::AudioInput::ListDevices();
        m_listsLoaded = true;
    }

    DrawStatusBar(state);
    const ImGuiID dockspace = ImGui::DockSpaceOverViewport();
    if (!m_layoutBuilt)
    {
        BuildDefaultLayout(dockspace);
        m_layoutBuilt = true;
    }

    DrawPreview(state, actions);
    DrawScenes(state, actions);
    DrawSceneProperties(state, actions);
    DrawSetup(state, actions);
    DrawBrowser(state, actions);
    DrawStatus(state);
    DrawMessages(state);
}

void Shell::BuildDefaultLayout(unsigned int dockspace)
{
    ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace);
    if (node != nullptr && !node->IsLeafNode())
    {
        return; // a saved layout exists
    }
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);

    ImGuiID center = dockspace;
    const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.27f, nullptr, &center);
    const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.31f, nullptr, &center);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.40f, nullptr, &center);

    ImGui::DockBuilderDockWindow(kScenes, left);
    ImGui::DockBuilderDockWindow(kProperties, right);
    ImGui::DockBuilderDockWindow(kSetup, right);
    ImGui::DockBuilderDockWindow(kBrowser, bottom);
    ImGui::DockBuilderDockWindow(kStatus, bottom);
    ImGui::DockBuilderDockWindow(kMessages, bottom);
    ImGui::DockBuilderDockWindow(kPreview, center);
    ImGui::DockBuilderFinish(dockspace);
    m_showBrowserTab = 2; // the browser, not the last docked tab, should be in front (after docking settles)
}

void Shell::DrawPreview(const ShellState& state, AppActions& actions)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 1));
    ImGui::Begin(kPreview);
    const auto* engine = state.engine;
    if (state.preview != 0 && engine != nullptr)
    {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float aspect = static_cast<float>(engine->RenderWidth()) / static_cast<float>(engine->RenderHeight());
        ImVec2 size(available.x, available.x / aspect);
        if (size.y > available.y)
            size = ImVec2(available.y * aspect, available.y);
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + (available.x - size.x) * 0.5f, ImGui::GetCursorPosY() + (available.y - size.y) * 0.5f));
        // GL textures are bottom-up; flip V.
        ImGui::Image(static_cast<ImTextureID>(state.preview), size, ImVec2(0, 1), ImVec2(1, 0));
    }

    // Fullscreen button, bottom right of the panel.
    const char* label = "Fullscreen";
    const ImVec2 padding = ImGui::GetStyle().FramePadding;
    const ImVec2 buttonSize(ImGui::CalcTextSize(label).x + padding.x * 2.f, ImGui::GetFrameHeight());
    const ImVec2 regionMax = ImGui::GetWindowContentRegionMax();
    ImGui::SetCursorPos(ImVec2(regionMax.x - buttonSize.x - 10.f, regionMax.y - buttonSize.y - 10.f));
    if (ImGui::Button(label, buttonSize))
    {
        actions.ToggleFullscreen();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show only the visuals on this screen. Cmd+F or Esc to come back.");
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void Shell::DrawScenes(ShellState& state, AppActions& actions)
{
    ImGui::Begin(kScenes);
    const auto& performance = state.document->Get();
    int& selected = *state.selectedScene;
    const int count = static_cast<int>(performance.scenes.size());
    selected = count > 0 ? std::clamp(selected, 0, count - 1) : -1;

    // Toolbar.
    if (ImGui::Button("Add"))
    {
        auto edited = performance;
        const auto preset = selected >= 0 ? performance.scenes[static_cast<size_t>(selected)].presetPath : std::filesystem::path();
        const int index = model::AddScene(edited, selected, preset);
        if (index >= 0)
        {
            actions.Edit(std::move(edited), "Add Scene");
            selected = index;
            m_scrollToScene = index;
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Add a scene after the selected one (Shift+Cmd+N)");
    ImGui::SameLine();
    ImGui::BeginDisabled(selected < 0);
    if (ImGui::Button("Duplicate"))
    {
        auto edited = performance;
        const int index = model::DuplicateScene(edited, selected);
        if (index >= 0)
        {
            actions.Edit(std::move(edited), "Duplicate Scene");
            selected = index;
            m_scrollToScene = index;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete"))
    {
        auto edited = performance;
        if (model::DeleteScene(edited, selected))
        {
            actions.Edit(std::move(edited), "Delete Scene");
            selected = std::min(selected, count - 2);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextColored(kDim, "%d / %d", count, model::kMaxScenes);

    if (count == 0)
    {
        ImGui::Spacing();
        ImGui::TextColored(kDim, "No scenes yet. Add one, or drop .milk presets here.");
    }

    const int live = state.engine->LiveScene();
    const int pending = state.engine->PendingScene();
    int hoveredScene = -1;
    int moveFrom = -1;
    int moveTo = -1;

    if (count > 0 &&
        ImGui::BeginTable("scenes", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("CC", ImGuiTableColumnFlags_WidthFixed, 30.f);
        ImGui::TableSetupColumn("Scene", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Preset", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 92.f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < count; ++i)
        {
            const auto& scene = performance.scenes[static_cast<size_t>(i)];
            const auto info = state.engine->Scene(i);
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 30.f);
            if (i == live)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4(0.18f, 0.40f, 0.24f, 0.55f)));
            ImGui::PushID(i);

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kDim, "%d", scene.cc20Value);

            ImGui::TableNextColumn();
            if (ImGui::Selectable(scene.DisplayName().c_str(), i == selected,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(0, 26.f)))
            {
                selected = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    actions.Trigger(i);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
                hoveredScene = i;
            if (m_scrollToScene == i)
            {
                ImGui::SetScrollHereY();
                m_scrollToScene = -1;
            }
            // Drag to reorder.
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                ImGui::SetDragDropPayload(kSceneDragPayload, &i, sizeof(int));
                ImGui::Text("Move \"%s\"", scene.DisplayName().c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneDragPayload))
                {
                    moveFrom = *static_cast<const int*>(payload->Data);
                    moveTo = i;
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPresetDragPayload))
                {
                    AssignPreset(state, actions, static_cast<const char*>(payload->Data), i);
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::BeginPopupContextItem("scene-menu"))
            {
                selected = i;
                if (ImGui::MenuItem("Go Live"))
                    actions.Trigger(i);
                if (ImGui::MenuItem("Duplicate"))
                {
                    auto edited = performance;
                    if (const int index = model::DuplicateScene(edited, i); index >= 0)
                    {
                        actions.Edit(std::move(edited), "Duplicate Scene");
                        selected = index;
                    }
                }
                if (ImGui::MenuItem("Delete"))
                {
                    auto edited = performance;
                    if (model::DeleteScene(edited, i))
                        actions.Edit(std::move(edited), "Delete Scene");
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Reveal Preset in Finder", nullptr, false, !scene.presetMissing))
                    RevealInFinder(scene.presetPath);
                ImGui::EndPopup();
            }

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(scene.presetMissing ? kRed : kDim, "%s", scene.presetPath.stem().c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s%s", scene.presetPath.c_str(), scene.presetMissing ? "\n(file not found)" : "");

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImVec4 color;
            const char* label = StateLabel(info, i == pending, color);
            ImGui::TextColored(color, "%s", label);
            if (!info.error.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", info.error.c_str());
            // Badges: heavy or dropping frames, and video.
            if (info.heavy || info.droppedFraction > 0.02)
            {
                ImGui::SameLine(0, 4);
                ImGui::TextColored(kAmber, "!");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%.0f ms per frame (%s)%s%.0f%% of frames dropped while live",
                                      info.frameMs, info.measuredLive ? "measured live" : "estimated",
                                      info.heavy ? ": can't reach 60 fps on this machine.\n" : ".\n",
                                      std::max(0.0, info.droppedFraction) * 100.0);
            }
            if (scene.video.enabled && state.video != nullptr)
            {
                const auto videoState = state.video->StateFor(i);
                const bool bad = !videoState || *videoState == io::VideoState::Missing || *videoState == io::VideoState::Failed;
                const bool waiting = videoState && *videoState == io::VideoState::Disconnected;
                ImGui::SameLine(0, 4);
                ImGui::TextColored(bad ? kRed : (waiting ? kAmber : kDim), "%s", scene.video.source.type == model::VideoSourceType::Syphon ? "S" : "V");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Video: %s (%s)\nmix %.2f, luma gate %.2f%s", state.video->DescribeFor(i).c_str(),
                                      videoState ? io::ToString(*videoState) : "none", scene.video.mix, scene.video.lumaMin,
                                      scene.video.restartOnEnter ? ", restarts on entry" : "");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Presets dropped anywhere else in the panel add a scene at the end.
    if (hoveredScene < 0 && ImGui::GetDragDropPayload() != nullptr && ImGui::GetDragDropPayload()->IsDataType(kPresetDragPayload))
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (ImGui::BeginDragDropTargetCustom(window->InnerRect, window->ID))
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPresetDragPayload))
                AssignPreset(state, actions, static_cast<const char*>(payload->Data), -1);
            ImGui::EndDragDropTarget();
        }
    }

    if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo)
    {
        auto edited = performance;
        if (model::MoveScene(edited, moveFrom, moveTo))
        {
            actions.Edit(std::move(edited), "Move Scene");
            selected = moveTo;
        }
    }

    // Keyboard, when the scene list has focus and no text field is active.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && count > 0)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            m_scrollToScene = selected = std::min(selected + 1, count - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            m_scrollToScene = selected = std::max(selected - 1, 0);
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
            actions.Trigger(selected);
        if ((ImGui::IsKeyPressed(ImGuiKey_Backspace, false) || ImGui::IsKeyPressed(ImGuiKey_Delete, false)) && selected >= 0)
        {
            auto edited = performance;
            if (model::DeleteScene(edited, selected))
            {
                actions.Edit(std::move(edited), "Delete Scene");
                selected = std::min(selected, count - 2);
            }
        }
    }
    // Left/Right trigger the previous/next scene from anywhere (not while typing).
    if (!ImGui::GetIO().WantTextInput && count > 0)
    {
        const int current = pending >= 0 ? pending : live;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) || ImGui::IsKeyPressed(ImGuiKey_PageDown, false))
            actions.Trigger(std::min(current + 1, count - 1));
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) || ImGui::IsKeyPressed(ImGuiKey_PageUp, false))
            actions.Trigger(std::max(current - 1, 0));
    }

    HandleDrops(state, actions, hoveredScene);
    ImGui::End();
}

void Shell::HandleDrops(ShellState& state, AppActions& actions, int hoveredScene)
{
    if (state.droppedFiles == nullptr || state.droppedFiles->empty())
    {
        return;
    }
    auto edited = state.document->Get();
    int& selected = *state.selectedScene;
    int changes = 0;
    for (const auto& file : *state.droppedFiles)
    {
        const auto extension = Lower(file.extension().string());
        if (extension == ".milk")
        {
            if (hoveredScene >= 0 && state.droppedFiles->size() == 1)
            {
                edited.scenes[static_cast<size_t>(hoveredScene)].presetPath = file;
                selected = hoveredScene;
            }
            else if (const int index = model::AddScene(edited, static_cast<int>(edited.scenes.size()) - 1, file); index >= 0)
            {
                selected = index;
                m_scrollToScene = index;
            }
            changes++;
        }
        else if (IsVideoFile(file) && (hoveredScene >= 0 || selected >= 0))
        {
            const int target = hoveredScene >= 0 ? hoveredScene : selected;
            auto& video = edited.scenes[static_cast<size_t>(target)].video;
            video.enabled = true;
            video.source.type = model::VideoSourceType::File;
            video.source.path = file;
            selected = target;
            changes++;
        }
        else if (extension == ".json")
        {
            state.droppedFiles->clear();
            actions.OpenPerformance(file);
            return;
        }
    }
    state.droppedFiles->clear();
    if (changes > 0)
    {
        actions.Edit(std::move(edited), changes == 1 ? "Drop File" : "Drop Files");
    }
}

void Shell::DrawSceneProperties(ShellState& state, AppActions& actions)
{
    ImGui::Begin(kProperties);
    const auto& performance = state.document->Get();
    const int index = *state.selectedScene;
    if (index < 0 || index >= static_cast<int>(performance.scenes.size()))
    {
        ImGui::TextColored(kDim, "Select a scene to edit it.");
        ImGui::End();
        return;
    }
    const auto& scene = performance.scenes[static_cast<size_t>(index)];
    const std::string key = scene.id; // coalescing keys are per scene and field

    // Edits copy the performance, change one field and hand it to the document.
    auto edit = [&](const char* label, const char* field, auto&& change) {
        auto edited = performance;
        change(edited.scenes[static_cast<size_t>(index)]);
        actions.Edit(std::move(edited), label, std::string(field) + ":" + key);
    };
    auto endEditIfReleased = [&] {
        if (ImGui::IsItemDeactivatedAfterEdit())
            actions.EndEdit();
    };

    const bool isLive = state.engine->LiveScene() == index;
    ImGui::BeginDisabled(isLive);
    if (ImGui::Button(isLive ? "Live" : "Go Live", ImVec2(110, 0)))
        actions.Trigger(index);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextColored(kDim, "Scene %d of %zu", index + 1, performance.scenes.size());

    ImGui::SeparatorText("Scene");
    std::string name = scene.name;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##name", scene.id.c_str(), &name))
        edit("Rename Scene", "name", [&](model::Scene& s) { s.name = name; });
    endEditIfReleased();

    int cc = scene.cc20Value;
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt("MIDI CC 20 value", &cc))
    {
        cc = std::clamp(cc, 0, 127);
        edit("Change CC Value", "cc", [&](model::Scene& s) { s.cc20Value = cc; });
    }
    endEditIfReleased();
    for (int other = 0; other < static_cast<int>(performance.scenes.size()); ++other)
    {
        if (other != index && performance.scenes[static_cast<size_t>(other)].cc20Value == scene.cc20Value)
        {
            ImGui::TextColored(kRed, "Also used by \"%s\": change one before saving.", performance.scenes[static_cast<size_t>(other)].DisplayName().c_str());
            break;
        }
    }

    ImGui::SeparatorText("Preset");
    if (state.library != nullptr && state.favourites != nullptr)
    {
        if (const std::string id = state.library->IdForPath(scene.presetPath); !id.empty() && state.library->Find(id) != nullptr)
        {
            if (StarButton("##favourite-scene-preset", state.favourites->Contains(id)))
                actions.ToggleFavourite(id);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Favourite");
            ImGui::SameLine();
        }
    }
    ImGui::TextWrapped("%s", scene.presetPath.empty() ? "(none)" : scene.presetPath.filename().c_str());
    if (scene.presetMissing)
        ImGui::TextColored(kRed, "File not found.");
    if (ImGui::Button("Choose Preset..."))
    {
        if (auto file = ChooseFile("Choose a preset", {"milk"}))
            edit("Change Preset", "preset", [&](model::Scene& s) { s.presetPath = *file; });
        actions.EndEdit();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(scene.presetMissing || scene.presetPath.empty());
    if (ImGui::Button("Reveal"))
        RevealInFinder(scene.presetPath);
    ImGui::EndDisabled();
    const auto info = state.engine->Scene(index);
    if (info.frameMs > 0.0)
        ImGui::TextColored(info.heavy ? kAmber : kDim, "%.1f ms per frame%s", info.frameMs, info.heavy ? ": too heavy for 60 fps" : "");

    ImGui::SeparatorText("Video");
    bool videoEnabled = scene.video.enabled;
    if (ImGui::Checkbox("Blend video into the visuals", &videoEnabled))
    {
        edit(videoEnabled ? "Enable Video" : "Disable Video", "video-enabled", [&](model::Scene& s) { s.video.enabled = videoEnabled; });
        actions.EndEdit();
    }
    ImGui::BeginDisabled(!scene.video.enabled);
    int type = scene.video.source.type == model::VideoSourceType::Syphon ? 1 : 0;
    bool typeChanged = ImGui::RadioButton("File", &type, 0);
    ImGui::SameLine();
    typeChanged |= ImGui::RadioButton("Syphon", &type, 1);
    if (typeChanged)
    {
        const auto newType = type == 1 ? model::VideoSourceType::Syphon : model::VideoSourceType::File;
        if (newType != scene.video.source.type)
        {
            edit("Change Video Source", "video-type", [&](model::Scene& s) { s.video.source.type = newType; });
            actions.EndEdit();
        }
    }
    if (scene.video.source.type == model::VideoSourceType::File)
    {
        ImGui::TextWrapped("%s", scene.video.source.path.empty() ? "(no file)" : scene.video.source.path.filename().c_str());
        if (scene.videoMissing)
            ImGui::TextColored(kRed, "File not found.");
        if (ImGui::Button("Choose Video..."))
        {
            if (auto file = ChooseFile("Choose a video", kVideoExtensions))
                edit("Change Video", "video-file", [&](model::Scene& s) { s.video.source.path = *file; });
            actions.EndEdit();
        }
        ImGui::SameLine();
        ImGui::TextColored(kDim, "or drop one on the scene");
    }
    else
    {
        const auto& source = scene.video.source;
        const std::string current = source.appName.empty() && source.serverName.empty()
                                        ? "Any sender"
                                        : source.appName + (source.serverName.empty() ? "" : " / " + source.serverName);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##syphon", current.c_str()))
        {
            if (ImGui::Selectable("Any sender", current == "Any sender"))
            {
                edit("Change Syphon Server", "syphon", [&](model::Scene& s) {
                    s.video.source.appName.clear();
                    s.video.source.serverName.clear();
                });
                actions.EndEdit();
            }
            const auto servers = io::ListSyphonServers();
            for (const auto& server : servers)
            {
                const std::string label = server.appName + (server.serverName.empty() ? "" : " / " + server.serverName);
                if (ImGui::Selectable(label.c_str(), label == current))
                {
                    edit("Change Syphon Server", "syphon", [&](model::Scene& s) {
                        s.video.source.appName = server.appName;
                        s.video.source.serverName = server.serverName;
                    });
                    actions.EndEdit();
                }
            }
            if (servers.empty())
                ImGui::TextColored(kDim, "No Syphon servers are running.");
            ImGui::EndCombo();
        }
    }
    if (state.video != nullptr)
    {
        if (const auto videoState = state.video->StateFor(index))
            ImGui::TextColored(*videoState == io::VideoState::Live ? kGreen : kAmber, "Status: %s", io::ToString(*videoState));
    }

    float mix = scene.video.mix;
    ImGui::SetNextItemWidth(-90);
    if (ImGui::SliderFloat("Mix", &mix, 0.f, 1.f, "%.2f"))
        edit("Change Mix", "mix", [&](model::Scene& s) { s.video.mix = mix; });
    endEditIfReleased();
    float luma = scene.video.lumaMin;
    ImGui::SetNextItemWidth(-90);
    if (ImGui::SliderFloat("Luma gate", &luma, 0.f, 1.f, "%.2f"))
        edit("Change Luma Gate", "luma", [&](model::Scene& s) { s.video.lumaMin = luma; });
    endEditIfReleased();
    Hint("Video pixels darker than this are left out of the blend.");
    bool restart = scene.video.restartOnEnter;
    if (ImGui::Checkbox("Restart video when the scene starts", &restart))
    {
        edit("Change Restart", "restart", [&](model::Scene& s) { s.video.restartOnEnter = restart; });
        actions.EndEdit();
    }
    ImGui::EndDisabled();
    ImGui::End();
}

void Shell::DrawSetup(ShellState& state, AppActions& actions)
{
    ImGui::Begin(kSetup);
    auto& settings = *state.settings;

    ImGui::SeparatorText("Performance");
    std::string name = state.document->Get().name;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##performance-name", "Performance name", &name))
    {
        auto edited = state.document->Get();
        edited.name = name;
        actions.Edit(std::move(edited), "Rename Performance", "performance-name");
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
        actions.EndEdit();
    ImGui::TextColored(kDim, "%s", state.document->Path().empty() ? "Not saved yet" : state.document->Path().c_str());

    ImGui::SeparatorText("Output");
    ImGui::TextColored(kDim, "Rendering at %dx%d. Move this window to the screen\nyou perform on and press Cmd+F for fullscreen.",
                       state.engine->RenderWidth(), state.engine->RenderHeight());

    ImGui::SeparatorText("Audio input");
    const char* currentAudio = settings.audioDevice.empty() ? "System default" : settings.audioDevice.c_str();
    ImGui::SetNextItemWidth(-80);
    if (ImGui::BeginCombo("##audio", currentAudio))
    {
        if (ImGui::Selectable("System default", settings.audioDevice.empty()))
            actions.SetAudioDevice("");
        for (const auto& device : m_audioDevices)
        {
            if (ImGui::Selectable(device.c_str(), device == settings.audioDevice))
                actions.SetAudioDevice(device);
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
        m_audioDevices = io::AudioInput::ListDevices();
    if (state.audio != nullptr && !state.audio->IsOpen())
        ImGui::TextColored(kRed, "Not capturing: the device couldn't be opened or was removed.");
    ImGui::SetNextItemWidth(-80);
    if (ImGui::SliderFloat("Gain", &settings.gain, 0.f, 8.f, "%.2fx"))
        actions.SettingsChanged();
    const float level = state.engine->GetStats().audioLevel;
    ImGui::ProgressBar(std::min(1.f, level * 2.f), ImVec2(-80, 6), "");

    ImGui::SeparatorText("MIDI");
    int channel = settings.midiChannel + 1;
    ImGui::SetNextItemWidth(-80);
    if (ImGui::SliderInt("Channel", &channel, 1, 16))
        actions.SetMidiChannel(channel - 1);
    ImGui::TextColored(kDim, "Scenes are triggered by CC %d on this channel.", io::kSceneTriggerController);
    if (state.midi != nullptr)
    {
        const auto sources = state.midi->SourceNames();
        ImGui::Text("%zu source%s", sources.size(), sources.size() == 1 ? "" : "s");
        for (const auto& source : sources)
            ImGui::BulletText("%s", source.c_str());
    }

    ImGui::SeparatorText("Folders");
    ImGui::Text("Preset library");
    ImGui::TextColored(kDim, "%s", settings.libraryRoot.empty() ? "(not set)" : settings.libraryRoot.c_str());
    if (ImGui::Button("Choose...##library"))
    {
        if (auto folder = ChooseFolder("Choose the preset library folder"))
            actions.SetLibraryRoot(*folder);
    }
    ImGui::Text("Textures");
    ImGui::TextColored(kDim, "%s", settings.texturePaths.empty() ? "(not set: presets that use textures sample black)" : settings.texturePaths.front().c_str());
    if (ImGui::Button("Choose...##textures"))
    {
        if (auto folder = ChooseFolder("Choose the texture folder"))
            actions.SetTextureFolder(*folder);
    }
    ImGui::End();
}

void Shell::AssignPreset(ShellState& state, AppActions& actions, const std::filesystem::path& preset, int scene)
{
    auto edited = state.document->Get();
    if (scene >= 0 && scene < static_cast<int>(edited.scenes.size()))
    {
        edited.scenes[static_cast<size_t>(scene)].presetPath = preset;
        actions.Edit(std::move(edited), "Change Preset");
        *state.selectedScene = scene;
    }
    else if (const int index = model::AddScene(edited, static_cast<int>(edited.scenes.size()) - 1, preset); index >= 0)
    {
        actions.Edit(std::move(edited), "Add Scene");
        *state.selectedScene = index;
        m_scrollToScene = index;
    }
    actions.EndEdit();
}

void Shell::DrawBrowser(ShellState& state, AppActions& actions)
{
    if (m_showBrowserTab > 0 && --m_showBrowserTab == 0)
    {
        ImGui::SetNextWindowFocus();
    }
    if (!ImGui::Begin(kBrowser))
    {
        // Hidden tab: no previews needed.
        if (state.thumbnails != nullptr)
            state.thumbnails->SetWanted({}, {});
        ImGui::End();
        return;
    }
    const auto* library = state.library;
    if (library == nullptr || state.settings->libraryRoot.empty())
    {
        ImGui::TextColored(kDim, "Choose the folder with your MilkDrop presets (e.g. the Cream of the Crop pack).");
        if (ImGui::Button("Choose Preset Folder..."))
        {
            if (auto folder = ChooseFolder("Choose the preset library folder"))
                actions.SetLibraryRoot(*folder);
        }
        ImGui::End();
        return;
    }

    // Toolbar: search, toggles, count, rescan.
    ImGui::SetNextItemWidth(260);
    ImGui::InputTextWithHint("##search", "Search name, author, category", &m_query.text);
    ImGui::SameLine();
    ImGui::Checkbox("Favourites", &m_query.favouritesOnly);
    ImGui::SameLine();
    ImGui::Checkbox("Hide broken", &m_query.hideBroken);
    ImGui::SameLine();
    bool previews = !state.settings->namesOnly;
    if (ImGui::Checkbox("Previews", &previews))
    {
        state.settings->namesOnly = !previews;
        actions.SettingsChanged();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Live mini-previews of the presets on screen. Off: a plain list.");

    if (!(m_query == m_lastQuery) || m_lastLibraryVersion != state.libraryVersion)
    {
        static const std::set<std::string> noFavourites;
        m_results = library::Search(*library, m_query, state.favourites != nullptr ? state.favourites->Ids() : noFavourites);
        m_lastQuery = m_query;
        m_lastLibraryVersion = state.libraryVersion;
    }
    ImGui::SameLine();
    ImGui::TextColored(kDim, "%zu of %zu", m_results.size(), library->Presets().size());
    ImGui::SameLine();
    if (state.indexing)
    {
        ImGui::TextColored(kAmber, "Indexing... %d", state.indexProgress);
    }
    else if (ImGui::SmallButton("Rescan"))
    {
        actions.RescanLibrary();
    }

    // Left: categories. Right: results.
    const float treeWidth = 230.f;
    ImGui::BeginChild("categories", ImVec2(treeWidth, 0), ImGuiChildFlags_Borders);
    if (ImGui::Selectable("All presets", m_query.category.empty()))
    {
        m_query.category.clear();
        m_query.subcategory.clear();
    }
    for (const auto& category : library->Categories())
    {
        const std::string label = (category.name.empty() ? std::string("(no category)") : category.name) + "  " + std::to_string(category.count);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (category.subcategories.empty())
            flags |= ImGuiTreeNodeFlags_Leaf;
        if (m_query.category == category.name && m_query.subcategory.empty())
            flags |= ImGuiTreeNodeFlags_Selected;
        const bool open = ImGui::TreeNodeEx(category.name.c_str(), flags, "%s", label.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            m_query.category = category.name;
            m_query.subcategory.clear();
        }
        if (open)
        {
            for (const auto& [name, count] : category.subcategories)
            {
                const std::string subLabel = name + "  " + std::to_string(count);
                const bool selected = m_query.category == category.name && m_query.subcategory == name;
                if (ImGui::Selectable((subLabel + "##" + category.name).c_str(), selected))
                {
                    m_query.category = category.name;
                    m_query.subcategory = name;
                }
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("results", ImVec2(0, 0));
    if (m_results.empty())
    {
        ImGui::TextColored(kDim, state.indexing && library->Presets().empty() ? "Indexing your presets..." : "No presets match.");
        if (state.thumbnails != nullptr)
            state.thumbnails->SetWanted({}, {});
    }
    else if (!state.settings->namesOnly && state.thumbnails != nullptr)
    {
        DrawPresetGrid(state, actions);
    }
    else if ((state.thumbnails != nullptr ? (state.thumbnails->SetWanted({}, {}), true) : true) && ImGui::BeginTable("presets", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 20.f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Author", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Notes", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableHeadersRow();

        const int selectedScene = *state.selectedScene;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_results.size()));
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                const auto& preset = library->Presets()[static_cast<size_t>(m_results[static_cast<size_t>(row)])];
                const auto path = library->PathForId(preset.id);
                ImGui::TableNextRow();
                ImGui::PushID(preset.id.c_str());

                ImGui::TableNextColumn();
                const bool favourite = state.favourites != nullptr && state.favourites->Contains(preset.id);
                if (StarButton("##fav", favourite))
                    actions.ToggleFavourite(preset.id);

                ImGui::TableNextColumn();
                if (ImGui::Selectable(preset.name.c_str(), m_selectedPreset == preset.id,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick))
                {
                    m_selectedPreset = preset.id;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        AssignPreset(state, actions, path, selectedScene);
                }
                if (ImGui::IsItemHovered() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                    ImGui::SetTooltip("%s\nDouble-click to use it in the selected scene, or drag it onto a scene.", preset.id.c_str());
                PresetDragSource(path, preset.name);
                if (ImGui::BeginPopupContextItem("preset-menu"))
                {
                    m_selectedPreset = preset.id;
                    if (ImGui::MenuItem("Use in Selected Scene", nullptr, false, selectedScene >= 0))
                        AssignPreset(state, actions, path, selectedScene);
                    if (ImGui::MenuItem("Add as New Scene"))
                        AssignPreset(state, actions, path, -1);
                    if (ImGui::MenuItem(favourite ? "Remove from Favourites" : "Add to Favourites"))
                        actions.ToggleFavourite(preset.id);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Reveal in Finder"))
                        RevealInFinder(path);
                    ImGui::EndPopup();
                }

                ImGui::TableNextColumn();
                ImGui::TextColored(kDim, "%s", preset.author.c_str());

                ImGui::TableNextColumn();
                ImGui::TextColored(kDim, "%s%s%s", preset.category.c_str(), preset.subcategory.empty() ? "" : " / ", preset.subcategory.c_str());

                ImGui::TableNextColumn();
                if (preset.broken)
                {
                    ImGui::TextColored(kRed, "broken");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", preset.error.c_str());
                    ImGui::SameLine();
                }
                if (preset.frameMs > 1000.0 / 60.0)
                {
                    ImGui::TextColored(kAmber, "heavy %.0f ms", preset.frameMs);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Measured %.0f ms per frame: can't reach 60 fps on this machine.", preset.frameMs);
                    ImGui::SameLine();
                }
                if (state.availableTextures != nullptr && !preset.textures.empty())
                {
                    const auto missing = library::MissingTextures(preset, *state.availableTextures);
                    if (!missing.empty())
                    {
                        ImGui::TextColored(kAmber, "%zu texture%s missing", missing.size(), missing.size() == 1 ? "" : "s");
                        if (ImGui::IsItemHovered())
                        {
                            std::string list;
                            for (const auto& name : missing)
                                list += (list.empty() ? "" : ", ") + name;
                            ImGui::SetTooltip("Not in your texture folder: %s\nThose parts render black.", list.c_str());
                        }
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::End();
}

/// Short notes about a preset: broken, heavy, missing textures. Empty if none.
static auto PresetNotes(const library::PresetInfo& preset, const std::set<std::string>* availableTextures, std::string* details) -> std::string
{
    std::string notes;
    auto add = [&](const std::string& note) { notes += (notes.empty() ? "" : ", ") + note; };
    if (preset.broken)
    {
        add("broken");
        if (details != nullptr)
            *details += "Failed to load: " + preset.error + "\n";
    }
    if (preset.frameMs > 1000.0 / 60.0)
    {
        char text[48];
        std::snprintf(text, sizeof(text), "heavy %.0f ms", preset.frameMs);
        add(text);
        if (details != nullptr)
            *details += "Measured " + std::string(text + 6) + " per frame: can't reach 60 fps on this machine.\n";
    }
    if (availableTextures != nullptr && !preset.textures.empty())
    {
        const auto missing = library::MissingTextures(preset, *availableTextures);
        if (!missing.empty())
        {
            add(std::to_string(missing.size()) + " texture" + (missing.size() == 1 ? "" : "s") + " missing");
            if (details != nullptr)
            {
                std::string list;
                for (const auto& name : missing)
                    list += (list.empty() ? "" : ", ") + name;
                *details += "Not in your texture folder: " + list + " (renders black).\n";
            }
        }
    }
    return notes;
}

void Shell::DrawPresetGrid(ShellState& state, AppActions& actions)
{
    const auto& library = *state.library;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float imageWidth = 176.f;
    const float imageHeight = imageWidth * thumbnails::ThumbnailService::kHeight / thumbnails::ThumbnailService::kWidth;
    const float lineHeight = ImGui::GetTextLineHeight();
    const float cellWidth = imageWidth + style.ItemSpacing.x;
    const float cellHeight = imageHeight + lineHeight * 2.f + 10.f + style.ItemSpacing.y;
    const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + style.ItemSpacing.x) / cellWidth));
    const int count = static_cast<int>(m_results.size());
    const int rows = (count + columns - 1) / columns;
    const int selectedScene = *state.selectedScene;

    std::vector<thumbnails::Request> visible;
    std::vector<thumbnails::Request> prefetch;
    auto request = [&](int index) {
        const auto& preset = library.Presets()[static_cast<size_t>(m_results[static_cast<size_t>(index)])];
        return thumbnails::Request{preset.id, library.PathForId(preset.id), std::to_string(preset.modified) + ":" + std::to_string(preset.size)};
    };

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImGuiListClipper clipper;
    clipper.Begin(rows, cellHeight);
    int firstRow = rows;
    int lastRow = -1;
    while (clipper.Step())
    {
        firstRow = std::min(firstRow, clipper.DisplayStart);
        lastRow = std::max(lastRow, clipper.DisplayEnd - 1);
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
        {
            for (int column = 0; column < columns; ++column)
            {
                const int index = row * columns + column;
                if (index >= count)
                    break;
                const auto& preset = library.Presets()[static_cast<size_t>(m_results[static_cast<size_t>(index)])];
                const auto path = library.PathForId(preset.id);
                visible.push_back(request(index));
                if (column > 0)
                    ImGui::SameLine(0, style.ItemSpacing.x);

                ImGui::PushID(preset.id.c_str());
                ImGui::BeginGroup(); // the tile plus its overlaid star count as one item in the grid layout
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                ImGui::SetNextItemAllowOverlap();
                const bool clicked = ImGui::InvisibleButton("##tile", ImVec2(imageWidth, cellHeight - style.ItemSpacing.y));
                const bool hovered = ImGui::IsItemHovered();
                if (clicked)
                {
                    m_selectedPreset = preset.id;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        AssignPreset(state, actions, path, selectedScene);
                }
                PresetDragSource(path, preset.name);
                if (ImGui::BeginPopupContextItem("preset-menu"))
                {
                    m_selectedPreset = preset.id;
                    if (ImGui::MenuItem("Use in Selected Scene", nullptr, false, selectedScene >= 0))
                        AssignPreset(state, actions, path, selectedScene);
                    if (ImGui::MenuItem("Add as New Scene"))
                        AssignPreset(state, actions, path, -1);
                    const bool favourite = state.favourites != nullptr && state.favourites->Contains(preset.id);
                    if (ImGui::MenuItem(favourite ? "Remove from Favourites" : "Add to Favourites"))
                        actions.ToggleFavourite(preset.id);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Reveal in Finder"))
                        RevealInFinder(path);
                    ImGui::EndPopup();
                }
                std::string details;
                const std::string notes = PresetNotes(preset, state.availableTextures, &details);

                // Image (or a placeholder).
                const ImVec2 imageMin = origin;
                const ImVec2 imageMax(origin.x + imageWidth, origin.y + imageHeight);
                const auto tile = state.thumbnails->Get(preset.id);
                draw->AddRectFilled(imageMin, imageMax, ImGui::GetColorU32(ImVec4(0.07f, 0.07f, 0.08f, 1.f)), 4.f);
                if (tile && tile->texture != 0)
                {
                    // GL textures are bottom-up; flip V.
                    draw->AddImageRounded(static_cast<ImTextureID>(tile->texture), imageMin, imageMax, ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, 4.f);
                }
                else
                {
                    const char* label = tile && tile->state == thumbnails::TileState::Broken ? "can't load" : "...";
                    const ImVec2 size = ImGui::CalcTextSize(label);
                    draw->AddText(ImVec2(imageMin.x + (imageWidth - size.x) * 0.5f, imageMin.y + (imageHeight - size.y) * 0.5f),
                                  ImGui::GetColorU32(tile && tile->state == thumbnails::TileState::Broken ? kRed : kDim), label);
                }
                if (m_selectedPreset == preset.id || hovered)
                    draw->AddRect(imageMin, imageMax, ImGui::GetColorU32(m_selectedPreset == preset.id ? kAccent : ImVec4(1, 1, 1, 0.35f)), 4.f, 0, 2.f);

                // Name and second line (notes, else author), clipped to the tile.
                const ImVec4 clip(origin.x, origin.y, origin.x + imageWidth, origin.y + cellHeight);
                draw->AddText(nullptr, 0.f, ImVec2(origin.x + 2.f, imageMax.y + 4.f), ImGui::GetColorU32(ImGuiCol_Text), preset.name.c_str(), nullptr, 0.f, &clip);
                const bool dark = tile && tile->state == thumbnails::TileState::Dark;
                const std::string second = !notes.empty() ? notes : (dark ? "starts dark" : preset.author);
                draw->AddText(nullptr, 0.f, ImVec2(origin.x + 2.f, imageMax.y + 4.f + lineHeight), ImGui::GetColorU32(!notes.empty() ? kAmber : kDim),
                              second.c_str(), nullptr, 0.f, &clip);

                // Favourite star over the image's top-left corner.
                const bool favourite = state.favourites != nullptr && state.favourites->Contains(preset.id);
                if (favourite || hovered || ImGui::IsMouseHoveringRect(imageMin, imageMax))
                {
                    ImGui::SetCursorScreenPos(ImVec2(origin.x + 5.f, origin.y + 5.f));
                    if (StarButton("##fav", favourite))
                        actions.ToggleFavourite(preset.id);
                }
                ImGui::EndGroup();

                if (hovered && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                {
                    ImGui::SetTooltip("%s\n%s%s%s\n%s%sDouble-click to use it in the selected scene, or drag it onto a scene.", preset.name.c_str(),
                                      preset.author.empty() ? "" : (preset.author + "  ·  ").c_str(), preset.category.c_str(),
                                      preset.subcategory.empty() ? "" : (" / " + preset.subcategory).c_str(), details.c_str(), details.empty() ? "" : "\n");
                }
                ImGui::PopID();
            }
        }
    }
    clipper.End();

    // Prepare one row above and below what's on screen.
    for (int row : {firstRow - 1, lastRow + 1})
    {
        for (int column = 0; row >= 0 && row < rows && column < columns; ++column)
        {
            const int index = row * columns + column;
            if (index < count)
                prefetch.push_back(request(index));
        }
    }
    state.thumbnails->SetWanted(std::move(visible), std::move(prefetch));
}

void Shell::DrawStatus(const ShellState& state)
{
    ImGui::Begin(kStatus);
    const auto stats = state.engine->GetStats();

    if (state.frameIntervalsMs != nullptr && !state.frameIntervalsMs->empty())
    {
        std::vector<float> values(state.frameIntervalsMs->begin(), state.frameIntervalsMs->end());
        const float worst = *std::max_element(values.begin(), values.end());
        char overlay[96];
        std::snprintf(overlay, sizeof(overlay), "frame interval: last %.1f ms, worst %.1f ms", values.back(), worst);
        ImGui::PlotLines("##frames", values.data(), static_cast<int>(values.size()), 0, overlay, 0.f, 50.f, ImVec2(-1, 60));
    }
    ImGui::Text("Frames over 25 ms: %lld", state.slowFrames);
    ImGui::SameLine(0, 24);
    ImGui::Text("Held (no new frame): %lld of %lld (%lld waiting for a scene)", stats.heldTicks, stats.ticks, stats.heldWaitingTicks);
    if (stats.warmTotal > 0)
        ImGui::Text("Pre-warm: %d / %d presets", stats.warmDone, stats.warmTotal);
    for (size_t lane = 0; lane < stats.laneQueue.size(); ++lane)
        ImGui::TextColored(kDim, "Lane %zu: %zu queued, live render %.1f ms CPU / %.1f ms GPU", lane, stats.laneQueue[lane], stats.laneRenderMs[lane], stats.laneGpuMs[lane]);
    if (state.thumbnails != nullptr)
        ImGui::TextColored(kDim, "Preview instances: %d", state.thumbnails->LiveInstances());
    ImGui::End();
}

void Shell::DrawMessages(const ShellState& state)
{
    ImGui::Begin(kMessages);
    if (state.issues != nullptr)
    {
        for (const auto& issue : *state.issues)
            ImGui::TextColored(issue.severity == model::Issue::Severity::Error ? kRed : kAmber, "%s %s", issue.path.c_str(), issue.message.c_str());
    }
    if (state.messages != nullptr)
    {
        for (const auto& message : *state.messages)
            ImGui::TextWrapped("%s", message.c_str());
    }
    ImGui::End();
}

void Shell::DrawStatusBar(const ShellState& state)
{
    const float height = ImGui::GetFrameHeight() + 6.f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 3));
    if (ImGui::BeginViewportSideBar("##statusbar", ImGui::GetMainViewport(), ImGuiDir_Down, height,
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar))
    {
        if (ImGui::BeginMenuBar())
        {
            const auto& performance = state.document->Get();
            const int live = state.engine->LiveScene();
            if (live >= 0 && live < static_cast<int>(performance.scenes.size()))
                ImGui::TextColored(kGreen, "LIVE  %s", performance.scenes[static_cast<size_t>(live)].DisplayName().c_str());
            else
                ImGui::TextColored(kDim, "Nothing live");

            ImGui::SameLine(0, 28);
            if (state.midi != nullptr)
            {
                if (auto cc = state.midi->LastControlChange())
                    ImGui::TextColored(kDim, "MIDI ch %d CC %d = %d", cc->channel + 1, cc->controller, cc->value);
                else
                    ImGui::TextColored(kDim, "MIDI: no messages yet");
            }
            ImGui::SameLine(0, 28);
            const auto stats = state.engine->GetStats();
            ImGui::TextColored(kDim, "Audio");
            ImGui::SameLine();
            ImGui::ProgressBar(std::min(1.f, stats.audioLevel * 2.f), ImVec2(70, 5), "");
            ImGui::SameLine(0, 28);
            if (stats.warmTotal > 0 && stats.warmDone < stats.warmTotal)
            {
                ImGui::TextColored(kAmber, "Preparing presets %d/%d", stats.warmDone, stats.warmTotal);
                ImGui::SameLine(0, 28);
            }
            ImGui::TextColored(kDim, "%dx%d", state.engine->RenderWidth(), state.engine->RenderHeight());
            if (state.document->IsDirty())
            {
                ImGui::SameLine(0, 28);
                ImGui::TextColored(kAmber, "Unsaved changes");
            }
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace gv::app
