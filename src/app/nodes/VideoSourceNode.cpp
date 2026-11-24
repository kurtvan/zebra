#include "app/nodes/NodeUi.hpp"

#include "app/Widgets.hpp"
#include "model/Document.hpp"

namespace gv::app::nodes {

namespace {

using model::VideoSourceParams;
using model::VideoSourceType;

constexpr const char* kTypes[] = {"File", "Syphon"}; // by VideoSourceType

auto SourceOf(const model::Node& node) -> const model::VideoSource&
{
    return model::ParamsOf<VideoSourceParams>(node).source;
}

auto IsFile(const model::Node& node) -> bool
{
    return SourceOf(node).type == VideoSourceType::File;
}

auto SyphonLabel(const model::VideoSource& source) -> std::string
{
    if (source.appName.empty() && source.serverName.empty())
        return "Any sender";
    return source.appName + (source.serverName.empty() ? "" : " / " + source.serverName);
}

/// A button that looks like a combo (combos can't open inside the zoomed canvas). Returns whether it was pressed.
auto ChoiceButton(const char* id, const std::string& label, float width) -> bool
{
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.f, 0.5f));
    const bool pressed = ImGui::Button((label + "  v" + id).c_str(), ImVec2(width, 0.f));
    ImGui::PopStyleVar();
    return pressed;
}

void DrawFileChoice(const model::Node& node, NodeContext& context)
{
    const model::PoolVideo* video = context.Performance().FindVideo(SourceOf(node).videoId);
    if (ChoiceButton("##file", video != nullptr ? video->DisplayName() : "Choose a video", context.width))
        context.OpenPopup(node, context.control);
    context.Tooltip("The video from the pool. Its start point and speed are in the Videos panel.");
}

void DrawFileList(const model::Node& node, NodeContext& context)
{
    const auto& performance = context.Performance();
    for (const auto& video : performance.videos)
    {
        ImGui::PushID(video.id.c_str());
        if (ImGui::Selectable(video.DisplayName().c_str(), video.id == SourceOf(node).videoId))
        {
            const std::string id = video.id;
            context.EditOnce(node, "Change Video", [&](model::Node& n) { model::ParamsOf<VideoSourceParams>(n).source.videoId = id; });
        }
        ImGui::PopID();
    }
    if (performance.videos.empty())
        ImGui::TextColored(kDim, "The video pool is empty.");
    ImGui::Separator();
    if (ImGui::Selectable("Add Video..."))
    {
        ImGui::CloseCurrentPopup();
        if (auto file = context.ChooseFile("Add a video", kVideoExtensions))
        {
            // The pool and the node change together, as one step.
            const auto path = *file;
            const std::string nodeId = node.id;
            context.EditPerformance(
                [&](model::Performance& edited, model::Graph& graph) {
                    const std::string id = model::AddPoolVideo(edited, path);
                    if (model::Node* n = graph.Find(nodeId))
                        model::ParamsOf<VideoSourceParams>(*n).source = {VideoSourceType::File, id, {}, {}, {}};
                },
                "Change Video");
        }
    }
}

void DrawSyphonChoice(const model::Node& node, NodeContext& context)
{
    if (ChoiceButton("##syphon", SyphonLabel(SourceOf(node)), context.width))
        context.OpenPopup(node, context.control);
    context.Tooltip("The Syphon sender to show.");
}

void DrawSyphonList(const model::Node& node, NodeContext& context)
{
    const std::string current = SyphonLabel(SourceOf(node));
    if (ImGui::Selectable("Any sender", current == "Any sender"))
    {
        context.EditOnce(node, "Change Syphon Server", [](model::Node& n) {
            auto& source = model::ParamsOf<VideoSourceParams>(n).source;
            source.appName.clear();
            source.serverName.clear();
        });
    }
    const auto servers = context.SyphonServers();
    for (const auto& server : servers)
    {
        const std::string label = server.appName + (server.serverName.empty() ? "" : " / " + server.serverName);
        if (ImGui::Selectable(label.c_str(), label == current))
        {
            context.EditOnce(node, "Change Syphon Server", [&](model::Node& n) {
                auto& source = model::ParamsOf<VideoSourceParams>(n).source;
                source.appName = server.appName;
                source.serverName = server.serverName;
            });
        }
    }
    if (servers.empty())
        ImGui::TextColored(kDim, "No Syphon servers are running.");
}

} // namespace

auto VideoSourceUi() -> NodeUi
{
    NodeUi ui;
    ui.type = model::NodeType::VideoSource;
    ui.width = 240.f;
    ui.header = ImVec4(0.19f, 0.32f, 0.52f, 0.60f);
    ui.menuGroup = 0;
    ui.title = [](const model::Node& node, NodeContext& context) -> std::string {
        const auto& source = SourceOf(node);
        if (source.type == VideoSourceType::Syphon)
            return "Syphon: " + SyphonLabel(source);
        const model::PoolVideo* video = context.Performance().FindVideo(source.videoId);
        return video != nullptr ? video->DisplayName() : "Video (none chosen)";
    };
    ui.status = [](const model::Node& node, NodeContext& context) -> NodeStatus {
        if (IsFile(node))
        {
            const model::PoolVideo* video = context.Performance().FindVideo(SourceOf(node).videoId);
            if (video == nullptr)
                return {NodeState::Empty, "No video chosen."};
            if (video->missing)
                return {NodeState::Missing, "File not found: " + video->path.string()};
        }
        if (context.Live())
        {
            if (!context.IsLiveVideo(node))
                return {NodeState::Ready, "Not on screen now."};
            if (const auto video = context.VideoState())
                return {video->first ? NodeState::Live : NodeState::Waiting, video->second};
            return {NodeState::Live, "The scene is live."};
        }
        return {NodeState::Ready, "Ready."};
    };

    Control type;
    type.kind = Control::Kind::Choice;
    type.id = "##type";
    type.radio = true;
    type.options = kTypes;
    type.get = [](const model::Node& node) { return static_cast<double>(SourceOf(node).type); };
    type.set = [](model::Node& node, double value) { model::ParamsOf<VideoSourceParams>(node).source.type = static_cast<VideoSourceType>(value); };
    type.undo = "Change Video Source";

    Control file;
    file.id = "##file";
    file.draw = DrawFileChoice;
    file.popup = DrawFileList;
    file.visible = IsFile;
    Control syphon;
    syphon.id = "##syphon";
    syphon.draw = DrawSyphonChoice;
    syphon.popup = DrawSyphonList;
    syphon.visible = [](const model::Node& node) { return !IsFile(node); };

    Control missing;
    missing.kind = Control::Kind::Text;
    missing.warning = true;
    missing.visible = IsFile;
    missing.text = [](const model::Node& node, NodeContext& context) -> std::string {
        const model::PoolVideo* video = context.Performance().FindVideo(SourceOf(node).videoId);
        return video != nullptr && video->missing ? "File not found." : "";
    };

    Control restart;
    restart.kind = Control::Kind::Checkbox;
    restart.id = "##restart";
    restart.label = "Restart on scene start";
    restart.get = ParamGet<VideoSourceParams, &VideoSourceParams::restartOnEnter>;
    restart.set = ParamSet<VideoSourceParams, &VideoSourceParams::restartOnEnter>;
    restart.undo = "Change Restart";
    restart.tooltip = "On: the video starts from its start point whenever the scene goes live.\nOff: it carries on from where it was.";

    Control loop;
    loop.kind = Control::Kind::Checkbox;
    loop.id = "##loop";
    loop.label = "Loop";
    loop.get = ParamGet<VideoSourceParams, &VideoSourceParams::loop>;
    loop.set = ParamSet<VideoSourceParams, &VideoSourceParams::loop>;
    loop.undo = "Loop Video";
    loop.undoOff = "Play Video Once";
    loop.tooltip = "On: the video loops back to its start point at the end.\nOff: it plays to the end once and holds its last frame.";
    loop.visible = IsFile;

    ui.controls = {type, file, syphon, missing, restart, loop};
    ui.addItems = {
        {"Video file"},
        {"Syphon video", [](model::Node& node) { model::ParamsOf<VideoSourceParams>(node).source.type = VideoSourceType::Syphon; }},
    };
    return ui;
}

} // namespace gv::app::nodes
