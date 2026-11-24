#include "model/nodes/NodeDef.hpp"
#include "model/nodes/NodeJson.hpp"
#include "model/Performance.hpp"

namespace gv::model::nodes {

namespace {

auto SourceJson(const VideoSource& source) -> nlohmann::json
{
    if (source.type == VideoSourceType::File)
        return {{"type", "file"}, {"video", source.videoId}};
    return {{"type", "syphon"}, {"appName", source.appName}, {"serverName", source.serverName}};
}

} // namespace

/// A video file from the pool, or a Syphon sender: no inputs, one video output.
auto VideoSourceDef() -> NodeDef
{
    NodeDef def;
    def.type = NodeType::VideoSource;
    def.jsonName = "videoSource";
    def.idPrefix = "video";
    def.displayName = "Video source";
    def.defaults = VideoSourceParams{};
    def.output = [](const Node&) -> std::optional<PortKind> { return PortKind::Video; };
    // A file source plays a video from the pool. One with no video yet is a warning; one naming a video
    // that isn't in the pool is a broken file.
    def.checkInShow = [](const Performance& performance, const Node& node, std::vector<ShowProblem>& problems) {
        const auto& source = ParamsOf<VideoSourceParams>(node).source;
        if (source.type != VideoSourceType::File || performance.FindVideo(source.videoId) != nullptr)
            return;
        if (source.videoId.empty())
            problems.push_back({false, "no video chosen"});
        else
            problems.push_back({true, "video \"" + source.videoId + "\" isn't in the video pool"});
    };
    def.write = [](const Node& node, nlohmann::json& object) {
        const auto& params = ParamsOf<VideoSourceParams>(node);
        object["source"] = SourceJson(params.source);
        object["restartOnEnter"] = params.restartOnEnter;
        object["loop"] = params.loop;
    };
    def.read = [](const nlohmann::json& object, Node& node, const std::string& path, std::vector<Issue>& issues) {
        auto& params = ParamsOf<VideoSourceParams>(node);
        if (auto source = object.find("source"); source != object.end() && source->is_object())
            params.source = ParseVideoSource(*source, path + "/source", issues);
        ReadField(object, "restartOnEnter", params.restartOnEnter, path, issues);
        ReadField(object, "loop", params.loop, path, issues);
    };
    return def;
}

} // namespace gv::model::nodes
