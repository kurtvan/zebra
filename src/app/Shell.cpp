#include "app/Shell.hpp"

#include "app/macos/FileDialog.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace gv::app {

namespace {

constexpr const char* kPreview = "Preview";
constexpr const char* kScenes = "Scenes";
constexpr const char* kSetup = "Setup";
constexpr const char* kStatus = "Status";
constexpr const char* kMessages = "Messages";

const ImVec4 kGreen{0.35f, 0.85f, 0.45f, 1.f};
const ImVec4 kAmber{0.95f, 0.70f, 0.25f, 1.f};
const ImVec4 kRed{0.95f, 0.40f, 0.40f, 1.f};
const ImVec4 kDim{0.60f, 0.60f, 0.65f, 1.f};

auto StateLabel(const engine::Engine::SceneInfo& info, bool pending, ImVec4& color) -> const char*
{
    using State = engine::ScenePool::State;
    color = kDim;
    if (pending)
    {
        color = kAmber;
        return "going live...";
    }
    switch (info.state)
    {
        case State::Live:
            color = kGreen;
            return "LIVE";
        case State::Ready:
            color = ImVec4(0.85f, 0.85f, 0.9f, 1.f);
            return "ready";
        case State::Preparing:
            return "preparing";
        case State::Failed:
            color = kRed;
            return "failed to load";
        case State::Unusable:
            color = kRed;
            return "preset missing";
        case State::None:
            return "";
    }
    return "";
}

} // namespace

void Shell::Draw(ShellState& state, AppActions& actions)
{
    if (!m_listsLoaded)
    {
        m_audioDevices = io::AudioInput::ListDevices();
        m_listsLoaded = true;
    }

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open Performance...", "Cmd+O"))
            {
                if (auto file = ChooseFile("Open Performance", {"json"}))
                    actions.OpenPerformance(*file);
            }
            if (ImGui::MenuItem("Reload Performance", "Cmd+R", false, !state.performancePath.empty()))
            {
                actions.ReloadPerformance();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Fullscreen", "Cmd+F"))
            {
                actions.ToggleFullscreen();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    const ImGuiID dockspace = ImGui::DockSpaceOverViewport();
    if (!m_layoutBuilt)
    {
        BuildDefaultLayout(dockspace);
        m_layoutBuilt = true;
    }

    DrawPreview(state, actions);
    DrawScenes(state, actions);
    DrawSetup(state, actions);
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
    const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.26f, nullptr, &center);
    const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, nullptr, &center);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.24f, nullptr, &center);

    ImGui::DockBuilderDockWindow(kScenes, left);
    ImGui::DockBuilderDockWindow(kSetup, right);
    ImGui::DockBuilderDockWindow(kStatus, bottom);
    ImGui::DockBuilderDockWindow(kMessages, bottom);
    ImGui::DockBuilderDockWindow(kPreview, center);
    ImGui::DockBuilderFinish(dockspace);
}

void Shell::DrawPreview(const ShellState& state, AppActions& actions)
{
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
    ImGui::SetCursorPos(ImVec2(regionMax.x - buttonSize.x - 8.f, regionMax.y - buttonSize.y - 8.f));
    if (ImGui::Button(label, buttonSize))
    {
        actions.ToggleFullscreen();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show only the visuals on this screen. Cmd+F or Esc to come back.");
    ImGui::End();
}

void Shell::DrawScenes(const ShellState& state, AppActions& actions)
{
    ImGui::Begin(kScenes);
    if (state.performance == nullptr)
    {
        ImGui::TextColored(kDim, "No performance loaded.");
        if (ImGui::Button("Open Performance..."))
        {
            if (auto file = ChooseFile("Open Performance", {"json"}))
                actions.OpenPerformance(*file);
        }
        ImGui::End();
        return;
    }

    const auto& performance = *state.performance;
    ImGui::TextUnformatted(performance.name.empty() ? state.performancePath.filename().c_str() : performance.name.c_str());
    ImGui::TextColored(kDim, "Click a scene or use Left/Right to trigger it.");
    ImGui::Separator();

    const int live = state.engine->LiveScene();
    const int pending = state.engine->PendingScene();

    if (ImGui::BeginTable("scenes", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("CC", ImGuiTableColumnFlags_WidthFixed, 32.f);
        ImGui::TableSetupColumn("Scene", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Preset", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 110.f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(performance.scenes.size()); ++i)
        {
            const auto& scene = performance.scenes[static_cast<size_t>(i)];
            const auto info = state.engine->Scene(i);
            ImGui::TableNextRow();
            ImGui::PushID(i);

            ImGui::TableNextColumn();
            ImGui::Text("%d", scene.cc20Value);

            ImGui::TableNextColumn();
            if (ImGui::Selectable(scene.DisplayName().c_str(), i == live, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
            {
                actions.Trigger(i);
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(scene.presetPath.stem().c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", scene.presetPath.c_str());

            ImGui::TableNextColumn();
            ImVec4 color;
            const char* label = StateLabel(info, i == pending, color);
            ImGui::TextColored(color, "%s", label);
            if (info.heavy)
            {
                ImGui::SameLine();
                ImGui::TextColored(kAmber, "heavy");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%.0f ms per frame (%s): can't reach 60 fps on this machine", info.frameMs, info.measuredLive ? "measured live" : "estimated while preparing");
            }
            if (info.droppedFraction > 0.02)
            {
                ImGui::SameLine();
                ImGui::TextColored(kAmber, "drops %.0f%%", info.droppedFraction * 100.0);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Share of output frames without a new image while this scene was live.");
            }
            if (scene.video.enabled)
            {
                ImGui::SameLine();
                ImGui::TextColored(scene.videoMissing ? kRed : kDim, scene.videoMissing ? "video missing" : "video");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Video playback arrives in milestone 3.\n%s", scene.video.source.path.c_str());
            }
            if (!info.error.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", info.error.c_str());

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Keyboard: previous / next scene (when no text field has focus).
    if (!ImGui::GetIO().WantTextInput && !performance.scenes.empty())
    {
        const int current = pending >= 0 ? pending : live;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) || ImGui::IsKeyPressed(ImGuiKey_PageDown, false))
            actions.Trigger(std::min(current + 1, static_cast<int>(performance.scenes.size()) - 1));
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) || ImGui::IsKeyPressed(ImGuiKey_PageUp, false))
            actions.Trigger(std::max(current - 1, 0));
    }
    ImGui::End();
}

void Shell::DrawSetup(ShellState& state, AppActions& actions)
{
    ImGui::Begin(kSetup);
    auto& settings = *state.settings;

    ImGui::SeparatorText("Performance");
    ImGui::TextWrapped("%s", state.performancePath.empty() ? "(none)" : state.performancePath.c_str());
    if (ImGui::Button("Open..."))
    {
        if (auto file = ChooseFile("Open Performance", {"json"}))
            actions.OpenPerformance(*file);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(state.performancePath.empty());
    if (ImGui::Button("Reload"))
        actions.ReloadPerformance();
    ImGui::EndDisabled();

    ImGui::SeparatorText("Output");
    ImGui::TextColored(kDim, "Rendering at %dx%d. Move this window to the screen\nyou perform on and press Cmd+F for fullscreen.", state.engine->RenderWidth(), state.engine->RenderHeight());

    ImGui::SeparatorText("Audio input");
    const char* currentAudio = settings.audioDevice.empty() ? "System default" : settings.audioDevice.c_str();
    if (ImGui::BeginCombo("Device", currentAudio))
    {
        if (ImGui::Selectable("System default", settings.audioDevice.empty()))
            actions.SetAudioDevice("");
        for (const auto& name : m_audioDevices)
        {
            if (ImGui::Selectable(name.c_str(), name == settings.audioDevice))
                actions.SetAudioDevice(name);
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh##audio"))
        m_audioDevices = io::AudioInput::ListDevices();
    if (state.audio != nullptr && !state.audio->IsOpen())
        ImGui::TextColored(kRed, "Not capturing: the device couldn't be opened or was removed.");
    if (ImGui::SliderFloat("Gain", &settings.gain, 0.f, 8.f, "%.2fx"))
        actions.SettingsChanged();
    const float level = state.engine->GetStats().audioLevel;
    ImGui::ProgressBar(std::min(1.f, level * 2.f), ImVec2(-1, 0), "");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Input level after gain (RMS %.3f)", level);

    ImGui::SeparatorText("MIDI");
    int channel = settings.midiChannel + 1;
    if (ImGui::SliderInt("Channel", &channel, 1, 16))
        actions.SetMidiChannel(channel - 1);
    ImGui::TextColored(kDim, "Scenes are triggered by CC %d on this channel.", io::kSceneTriggerController);
    if (state.midi != nullptr)
    {
        const auto sources = state.midi->SourceNames();
        ImGui::Text("Sources: %zu", sources.size());
        for (const auto& source : sources)
            ImGui::BulletText("%s", source.c_str());
        if (auto cc = state.midi->LastControlChange())
            ImGui::TextColored(kDim, "Last CC: channel %d, CC %d = %d", cc->channel + 1, cc->controller, cc->value);
    }

    ImGui::SeparatorText("Folders");
    ImGui::Text("Preset library");
    ImGui::TextWrapped("%s", settings.libraryRoot.empty() ? "(not set)" : settings.libraryRoot.c_str());
    if (ImGui::Button("Choose...##library"))
    {
        if (auto folder = ChooseFolder("Choose the preset library folder"))
            actions.SetLibraryRoot(*folder);
    }
    ImGui::Text("Textures");
    ImGui::TextWrapped("%s", settings.texturePaths.empty() ? "(not set: presets that use textures sample black)" : settings.texturePaths.front().c_str());
    if (ImGui::Button("Choose...##textures"))
    {
        if (auto folder = ChooseFolder("Choose the texture folder"))
            actions.SetTextureFolder(*folder);
    }
    ImGui::End();
}

void Shell::DrawStatus(const ShellState& state)
{
    ImGui::Begin(kStatus);
    const auto stats = state.engine->GetStats();

    if (state.frameIntervalsMs != nullptr && !state.frameIntervalsMs->empty())
    {
        std::vector<float> values(state.frameIntervalsMs->begin(), state.frameIntervalsMs->end());
        float worst = *std::max_element(values.begin(), values.end());
        char overlay[96];
        std::snprintf(overlay, sizeof(overlay), "frame interval: last %.1f ms, worst %.1f ms", values.back(), worst);
        ImGui::PlotLines("##frames", values.data(), static_cast<int>(values.size()), 0, overlay, 0.f, 50.f, ImVec2(-1, 60));
    }
    ImGui::Text("Frames over 25 ms: %lld", state.slowFrames);
    ImGui::SameLine(0, 24);
    ImGui::Text("Held (no new frame): %lld of %lld (%lld waiting for a scene)", stats.heldTicks, stats.ticks, stats.heldWaitingTicks);
    if (stats.warmTotal > 0)
    {
        ImGui::Text("Pre-warm: %d / %d presets", stats.warmDone, stats.warmTotal);
    }
    for (size_t lane = 0; lane < stats.laneQueue.size(); ++lane)
    {
        ImGui::Text("Lane %zu: %zu queued, live render %.1f ms CPU / %.1f ms GPU", lane, stats.laneQueue[lane], stats.laneRenderMs[lane], stats.laneGpuMs[lane]);
    }
    ImGui::End();
}

void Shell::DrawMessages(const ShellState& state)
{
    ImGui::Begin(kMessages);
    if (state.issues != nullptr)
    {
        for (const auto& issue : *state.issues)
        {
            ImGui::TextColored(issue.severity == model::Issue::Severity::Error ? kRed : kAmber, "%s %s", issue.path.c_str(), issue.message.c_str());
        }
    }
    if (state.messages != nullptr)
    {
        for (const auto& message : *state.messages)
        {
            ImGui::TextWrapped("%s", message.c_str());
        }
    }
    ImGui::End();
}

} // namespace gv::app
