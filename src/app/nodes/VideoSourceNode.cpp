#include "app/nodes/NodeDraw.hpp"

#include "app/Shell.hpp"
#include "app/Widgets.hpp"
#include "app/macos/FileDialog.hpp"
#include "io/VideoSource.hpp"
#include "model/Document.hpp"

namespace gv::app {

namespace {

enum Popup
{
    kFilePopup = 1,
    kSyphonPopup = 2,
};

auto SyphonLabel(const model::VideoSource& source) -> std::string
{
    if (source.appName.empty() && source.serverName.empty())
        return "Any sender";
    return source.appName + (source.serverName.empty() ? "" : " / " + source.serverName);
}

/// A button that looks like a combo and opens popup `kind` (combos can't open inside the zoomed canvas).
auto ChoiceButton(const char* id, const std::string& label, float width) -> bool
{
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.f, 0.5f));
    const bool pressed = ImGui::Button((label + "  v" + id).c_str(), ImVec2(width, 0.f));
    ImGui::PopStyleVar();
    return pressed;
}

} // namespace

auto VideoSourceTitle(const model::Node& node, const NodeContext& context) -> std::string
{
    const auto& source = model::ParamsOf<model::VideoSourceParams>(node).source;
    if (source.type == model::VideoSourceType::Syphon)
        return "Syphon: " + SyphonLabel(source);
    const model::PoolVideo* video = context.performance.FindVideo(source.videoId);
    return video != nullptr ? video->DisplayName() : "Video (none chosen)";
}

auto VideoSourceStatus(const model::Node& node, NodeContext& context) -> NodeStatus
{
    const auto& source = model::ParamsOf<model::VideoSourceParams>(node).source;
    if (source.type == model::VideoSourceType::File)
    {
        const model::PoolVideo* video = context.performance.FindVideo(source.videoId);
        if (video == nullptr)
            return {kDim, "No video chosen."};
        if (video->missing)
            return {kRed, "File not found: " + video->path.string()};
    }
    if (context.live)
    {
        if (context.state.video != nullptr)
        {
            if (const auto state = context.state.video->StateFor(context.scene))
                return {*state == io::VideoState::Live ? kGreen : kAmber, std::string("Video: ") + io::ToString(*state)};
        }
        return {kGreen, "The scene is live."};
    }
    return {ImVec4(0.85f, 0.86f, 0.9f, 1.f), "Ready."};
}

void DrawVideoSourceBody(const model::Node& node, NodeContext& context)
{
    const auto& params = model::ParamsOf<model::VideoSourceParams>(node);
    const auto& source = params.source;

    int type = source.type == model::VideoSourceType::Syphon ? 1 : 0;
    bool typeChanged = ImGui::RadioButton("File", &type, 0);
    ImGui::SameLine();
    typeChanged |= ImGui::RadioButton("Syphon", &type, 1);
    if (typeChanged)
    {
        const auto newType = type == 1 ? model::VideoSourceType::Syphon : model::VideoSourceType::File;
        if (newType != source.type)
            context.EditOnce(node, "Change Video Source", [&](model::Node& n) { model::ParamsOf<model::VideoSourceParams>(n).source.type = newType; });
    }

    if (source.type == model::VideoSourceType::File)
    {
        const model::PoolVideo* video = context.performance.FindVideo(source.videoId);
        if (ChoiceButton("##file", video != nullptr ? video->DisplayName() : "Choose a video", context.width))
            context.OpenPopup(node, kFilePopup);
        context.Tooltip("The video from the pool. Its start point and speed are in the Videos panel.");
        if (video != nullptr && video->missing)
            ImGui::TextColored(kRed, "File not found.");
    }
    else
    {
        if (ChoiceButton("##syphon", SyphonLabel(source), context.width))
            context.OpenPopup(node, kSyphonPopup);
        context.Tooltip("The Syphon sender to show.");
    }

    bool restart = params.restartOnEnter;
    if (ImGui::Checkbox("Restart on scene start", &restart))
        context.EditOnce(node, "Change Restart", [&](model::Node& n) { model::ParamsOf<model::VideoSourceParams>(n).restartOnEnter = restart; });
    context.Tooltip("On: the video starts from its start point whenever the scene goes live.\nOff: it carries on from where it was.");
    if (source.type == model::VideoSourceType::File)
    {
        bool loop = params.loop;
        if (ImGui::Checkbox("Loop", &loop))
            context.EditOnce(node, loop ? "Loop Video" : "Play Video Once", [&](model::Node& n) { model::ParamsOf<model::VideoSourceParams>(n).loop = loop; });
        context.Tooltip("On: the video loops back to its start point at the end.\nOff: it plays to the end once and holds its last frame.");
    }
}

void DrawVideoSourcePopup(const model::Node& node, int kind, NodeContext& context)
{
    const auto& source = model::ParamsOf<model::VideoSourceParams>(node).source;
    if (kind == kFilePopup)
    {
        for (const auto& video : context.performance.videos)
        {
            ImGui::PushID(video.id.c_str());
            if (ImGui::Selectable(video.DisplayName().c_str(), video.id == source.videoId))
            {
                const std::string id = video.id;
                context.EditOnce(node, "Change Video", [&](model::Node& n) { model::ParamsOf<model::VideoSourceParams>(n).source.videoId = id; });
            }
            ImGui::PopID();
        }
        if (context.performance.videos.empty())
            ImGui::TextColored(kDim, "The video pool is empty.");
        ImGui::Separator();
        if (ImGui::Selectable("Add Video..."))
        {
            ImGui::CloseCurrentPopup();
            if (auto file = ChooseFile("Add a video", kVideoExtensions))
            {
                // The pool and the node change together, as one step.
                auto edited = context.performance;
                const std::string id = model::AddPoolVideo(edited, *file);
                if (model::Node* n = edited.scenes[static_cast<size_t>(context.scene)].graph.Find(node.id))
                    model::ParamsOf<model::VideoSourceParams>(*n).source = {model::VideoSourceType::File, id, {}, {}, {}};
                context.actions.Edit(std::move(edited), "Change Video");
                context.actions.EndEdit();
            }
        }
    }
    else if (kind == kSyphonPopup)
    {
        const std::string current = SyphonLabel(source);
        if (ImGui::Selectable("Any sender", current == "Any sender"))
        {
            context.EditOnce(node, "Change Syphon Server", [](model::Node& n) {
                auto& s = model::ParamsOf<model::VideoSourceParams>(n).source;
                s.appName.clear();
                s.serverName.clear();
            });
        }
        const auto servers = io::ListSyphonServers();
        for (const auto& server : servers)
        {
            const std::string label = server.appName + (server.serverName.empty() ? "" : " / " + server.serverName);
            if (ImGui::Selectable(label.c_str(), label == current))
            {
                context.EditOnce(node, "Change Syphon Server", [&](model::Node& n) {
                    auto& s = model::ParamsOf<model::VideoSourceParams>(n).source;
                    s.appName = server.appName;
                    s.serverName = server.serverName;
                });
            }
        }
        if (servers.empty())
            ImGui::TextColored(kDim, "No Syphon servers are running.");
    }
}

} // namespace gv::app
