#include "model/PerformanceJson.hpp"

#include "model/Document.hpp"
#include "model/LegacyScene.hpp"
#include "model/nodes/NodeDef.hpp"
#include "model/nodes/NodeJson.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <tuple>
#include <variant>
#include <optional>
#include <set>
#include <sstream>

namespace gv::model {

using nlohmann::json;

auto JsonError(std::string path, std::string message) -> Issue
{
    return {Issue::Severity::Error, std::move(path), std::move(message)};
}

auto JsonWarning(std::string path, std::string message) -> Issue
{
    return {Issue::Severity::Warning, std::move(path), std::move(message)};
}

auto ParseVideoSource(const json& object, const std::string& path, std::vector<Issue>& issues) -> VideoSource
{
    VideoSource source;
    std::string type = "file";
    ReadField(object, "type", type, path, issues);
    if (type == "file")
    {
        std::string filePath;
        ReadField(object, "video", source.videoId, path, issues);
        ReadField(object, "path", filePath, path, issues); // format 1
        source.type = VideoSourceType::File;
        source.path = filePath;
    }
    else if (type == "syphon")
    {
        source.type = VideoSourceType::Syphon;
        ReadField(object, "appName", source.appName, path, issues);
        ReadField(object, "serverName", source.serverName, path, issues);
    }
    else
    {
        issues.push_back(JsonError(path + "/type", "unknown video source type \"" + type + "\" (expected \"file\" or \"syphon\")"));
    }
    return source;
}

namespace {

auto Error(std::string path, std::string message) -> Issue
{
    return JsonError(std::move(path), std::move(message));
}

auto Warning(std::string path, std::string message) -> Issue
{
    return JsonWarning(std::move(path), std::move(message));
}

auto ParseLook(const json& object, const std::string& path, std::vector<Issue>& issues) -> VideoLook
{
    VideoLook look;
    ReadField(object, "mix", look.mix, path, issues);
    ReadField(object, "lumaMin", look.lumaMin, path, issues);
    ReadField(object, "contrast", look.contrast, path, issues);
    ReadField(object, "saturation", look.saturation, path, issues);
    ReadField(object, "overlay", look.overlay, path, issues);
    ReadField(object, "softness", look.softness, path, issues);
    return look;
}

auto NodeJson(const Node& node) -> json
{
    const NodeDef& def = Def(node.type);
    json object = {{"id", node.id}, {"type", def.jsonName}, {"x", node.x}, {"y", node.y}, {"expanded", node.expanded}};
    if (def.write != nullptr)
        def.write(node, object);
    if (!node.depths.empty())
        object["depths"] = node.depths;
    return object;
}

auto WireJson(const Wire& wire, const Graph& graph) -> json
{
    json object = {{"from", wire.fromNode}, {"to", wire.toNode}, {"input", wire.toInput}};
    if (const Node* to = graph.Find(wire.toNode); to != nullptr)
    {
        if (const auto names = Def(to->type).inputNames; wire.toInput >= 0 && wire.toInput < static_cast<int>(names.size()))
            object["input"] = names[static_cast<size_t>(wire.toInput)];
    }
    if (!wire.textureName.empty())
        object["texture"] = wire.textureName;
    return object;
}

auto GraphJson(const Graph& graph) -> json
{
    json nodes = json::array();
    for (const auto& node : graph.nodes)
        nodes.push_back(NodeJson(node));
    // Wires sorted, so saves are stable.
    std::vector<Wire> wires = graph.wires;
    std::sort(wires.begin(), wires.end(), [](const Wire& a, const Wire& b) {
        return std::tie(a.toNode, a.toInput, a.textureName, a.fromNode) < std::tie(b.toNode, b.toInput, b.textureName, b.fromNode);
    });
    json wiresJson = json::array();
    for (const auto& wire : wires)
        wiresJson.push_back(WireJson(wire, graph));
    return {{"nodes", nodes}, {"wires", wiresJson}};
}

auto ParseNode(const json& object, const std::string& path, std::vector<Issue>& issues) -> std::optional<Node>
{
    Node node;
    std::string type;
    ReadField(object, "id", node.id, path, issues);
    ReadField(object, "type", type, path, issues);
    ReadField(object, "x", node.x, path, issues);
    ReadField(object, "y", node.y, path, issues);
    ReadField(object, "expanded", node.expanded, path, issues);
    ReadField(object, "depths", node.depths, path, issues);
    const auto known = NodeTypeFromJson(type);
    if (!known)
    {
        issues.push_back(Warning(path + "/type", "unknown node type \"" + type + "\": the node was left out"));
        return std::nullopt;
    }
    node.type = *known;
    const NodeDef& def = Def(node.type);
    node.params = def.defaults;
    if (def.read != nullptr)
        def.read(object, node, path, issues);
    return node;
}

auto ParseWire(const json& object, const Graph& graph, const std::string& path, std::vector<Issue>& issues) -> std::optional<Wire>
{
    Wire wire;
    ReadField(object, "from", wire.fromNode, path, issues);
    ReadField(object, "to", wire.toNode, path, issues);
    ReadField(object, "texture", wire.textureName, path, issues);
    if (auto input = object.find("input"); input != object.end() && input->is_string())
    {
        const Node* to = graph.Find(wire.toNode);
        const auto names = to != nullptr ? Def(to->type).inputNames : std::span<const char* const>{};
        const auto it = std::find_if(names.begin(), names.end(), [&](const char* name) { return *input == name; });
        if (it == names.end())
        {
            issues.push_back(Warning(path + "/input", "unknown input \"" + input->get<std::string>() + "\": the wire was left out"));
            return std::nullopt;
        }
        wire.toInput = static_cast<int>(it - names.begin());
    }
    else
    {
        ReadField(object, "input", wire.toInput, path, issues);
    }
    return wire;
}

auto ParseGraph(const json& object, const std::string& path, std::vector<Issue>& issues) -> Graph
{
    Graph graph;
    if (auto nodes = object.find("nodes"); nodes != object.end() && nodes->is_array())
    {
        for (size_t i = 0; i < nodes->size(); ++i)
        {
            const std::string nodePath = path + "/nodes/" + std::to_string(i);
            if (!(*nodes)[i].is_object())
                issues.push_back(Warning(nodePath, "node must be an object: it was left out"));
            else if (auto node = ParseNode((*nodes)[i], nodePath, issues))
                graph.nodes.push_back(std::move(*node));
        }
    }
    if (auto wires = object.find("wires"); wires != object.end() && wires->is_array())
    {
        for (size_t i = 0; i < wires->size(); ++i)
        {
            const std::string wirePath = path + "/wires/" + std::to_string(i);
            if (!(*wires)[i].is_object())
                issues.push_back(Warning(wirePath, "wire must be an object: it was left out"));
            else if (auto wire = ParseWire((*wires)[i], graph, wirePath, issues))
                graph.wires.push_back(std::move(*wire));
        }
    }
    return graph;
}

/// Format 1-2 scenes: everything but the graph, which is converted from the classic settings after the
/// file's videos are moved into the pool (MigrateFileSources).
auto ParseScene(const json& object, const std::string& path, std::vector<Issue>& issues, ClassicScene& classic) -> Scene
{
    Scene scene;
    std::vector<std::optional<VideoLook>> entryLooks;
    ReadField(object, "id", scene.id, path, issues);
    ReadField(object, "name", scene.name, path, issues);

    if (auto graph = object.find("graph"); graph != object.end() && graph->is_object())
        scene.graph = ParseGraph(*graph, path + "/graph", issues);

    if (auto preset = object.find("preset"); preset != object.end() && preset->is_object())
    {
        std::string presetPath;
        ReadField(*preset, "path", presetPath, path + "/preset", issues);
        classic.presetPath = presetPath;
    }

    if (auto playlist = object.find("playlist"); playlist != object.end() && playlist->is_object())
    {
        const std::string playlistPath = path + "/playlist";
        ReadField(*playlist, "enabled", classic.playlist.enabled, playlistPath, issues);
        ParseTiming(*playlist, classic.playlist, playlistPath, issues);
        if (auto presets = playlist->find("presets"); presets != playlist->end() && presets->is_array())
        {
            for (size_t i = 0; i < presets->size(); ++i)
            {
                std::string presetPath;
                const std::string entryPath = playlistPath + "/presets/" + std::to_string(i);
                if ((*presets)[i].is_object())
                    ReadField((*presets)[i], "path", presetPath, entryPath, issues);
                classic.playlist.presets.emplace_back(presetPath);
                // Entries without their own look (older files) take the scene's, filled in below.
                std::optional<VideoLook> look;
                if ((*presets)[i].is_object())
                {
                    if (auto object = (*presets)[i].find("look"); object != (*presets)[i].end() && object->is_object())
                        look = ParseLook(*object, entryPath + "/look", issues);
                }
                entryLooks.push_back(look);
            }
        }
    }

    if (auto video = object.find("video"); video != object.end() && video->is_object())
    {
        const std::string videoPath = path + "/video";
        ReadField(*video, "enabled", classic.video.enabled, videoPath, issues);
        classic.video.look = ParseLook(*video, videoPath, issues);
        ReadField(*video, "restartOnEnter", classic.video.restartOnEnter, videoPath, issues);
        ReadField(*video, "loop", classic.video.loop, videoPath, issues);
        ReadField(*video, "invertLuma", classic.video.invertLuma, videoPath, issues);
        if (auto source = video->find("source"); source != video->end() && source->is_object())
        {
            classic.video.source = ParseVideoSource(*source, videoPath + "/source", issues);
        }
        if (auto playlist = video->find("playlist"); playlist != video->end() && playlist->is_object())
        {
            const std::string playlistPath = videoPath + "/playlist";
            ReadField(*playlist, "enabled", classic.video.playlist.enabled, playlistPath, issues);
            ParseTiming(*playlist, classic.video.playlist, playlistPath, issues);
            ReadField(*playlist, "videos", classic.video.playlist.videos, playlistPath, issues);
        }
    }

    if (auto hit = object.find("beatHit"); hit != object.end() && hit->is_object())
    {
        const std::string hitPath = path + "/beatHit";
        ReadField(*hit, "enabled", scene.beatHit.enabled, hitPath, issues);
        ReadField(*hit, "strength", scene.beatHit.strength, hitPath, issues);
        std::string every = "beat";
        ReadField(*hit, "every", every, hitPath, issues);
        if (every == "beat")
            scene.beatHit.every = BeatHitEvery::Beat;
        else if (every == "2beats")
            scene.beatHit.every = BeatHitEvery::TwoBeats;
        else if (every == "bar")
            scene.beatHit.every = BeatHitEvery::Bar;
        else
            issues.push_back(Error(hitPath + "/every", "unknown value \"" + every + "\" (expected \"beat\", \"2beats\" or \"bar\")"));
        std::string sound = "kick";
        ReadField(*hit, "sound", sound, hitPath, issues);
        if (sound == "kick")
            scene.beatHit.sound = BeatHitSound::Kick;
        else if (sound == "snare")
            scene.beatHit.sound = BeatHitSound::Snare;
        else if (sound == "burst")
            scene.beatHit.sound = BeatHitSound::Burst;
        else
            issues.push_back(Error(hitPath + "/sound", "unknown sound \"" + sound + "\" (expected \"kick\", \"snare\" or \"burst\")"));
    }

    if (auto midi = object.find("midi"); midi != object.end() && midi->is_object())
    {
        ReadField(*midi, "cc20Value", scene.cc20Value, path + "/midi", issues);
    }
    else
    {
        scene.cc20Value = -1; // Validate() reports it.
    }
    for (const auto& look : entryLooks)
        classic.playlist.looks.push_back(look.value_or(classic.video.look));
    return scene;
}

auto NodeLabel(const GraphError& error) -> std::string
{
    return error.nodeId.empty() ? std::string() : "node \"" + error.nodeId + "\": ";
}

/// Format 1 stored a file path per scene. Each distinct file becomes one pool video (start 0, speed 1).
void MigrateFileSources(Performance& performance, std::vector<ClassicScene>& classics)
{
    for (auto& scene : classics)
    {
        auto& source = scene.video.source;
        if (source.type == VideoSourceType::File && source.videoId.empty() && !source.path.empty())
        {
            source.videoId = AddPoolVideo(performance, source.path);
            source.path.clear();
        }
    }
}

} // namespace

auto LoadResult::HasErrors() const -> bool
{
    for (const auto& issue : issues)
    {
        if (issue.severity == Issue::Severity::Error)
        {
            return true;
        }
    }
    return false;
}

auto ParsePerformance(const std::string& text) -> LoadResult
{
    LoadResult result;
    json root;
    try
    {
        root = json::parse(text);
    }
    catch (const json::parse_error& error)
    {
        result.issues.push_back(Error("", std::string("not valid JSON: ") + error.what()));
        return result;
    }
    if (!root.is_object())
    {
        result.issues.push_back(Error("", "the top level must be an object"));
        return result;
    }

    int version = 0;
    ReadField(root, "version", version, "", result.issues);
    if (version < 1)
    {
        result.issues.push_back(Error("/version", "missing or invalid format version"));
        return result;
    }
    if (version > kCurrentFormatVersion)
    {
        result.issues.push_back(Error("/version", "file format version " + std::to_string(version) + " is newer than this app supports (" + std::to_string(kCurrentFormatVersion) + ")"));
        return result;
    }

    Performance& performance = result.performance;
    performance.version = kCurrentFormatVersion;
    ReadField(root, "name", performance.name, "", result.issues);
    if (auto canvas = root.find("canvas"); canvas != root.end() && !canvas->is_null())
    {
        if (canvas->is_object())
        {
            performance.canvas.matchScreen = false;
            ReadField(*canvas, "width", performance.canvas.width, "/canvas", result.issues);
            ReadField(*canvas, "height", performance.canvas.height, "/canvas", result.issues);
        }
        else
        {
            result.issues.push_back(Error("/canvas", "must be an object"));
        }
    }

    if (auto videos = root.find("videos"); videos != root.end() && videos->is_array())
    {
        for (size_t i = 0; i < videos->size(); ++i)
        {
            const std::string videoPath = "/videos/" + std::to_string(i);
            if (!(*videos)[i].is_object())
            {
                result.issues.push_back(Error(videoPath, "video must be an object"));
                continue;
            }
            PoolVideo video;
            std::string filePath;
            ReadField((*videos)[i], "id", video.id, videoPath, result.issues);
            ReadField((*videos)[i], "name", video.name, videoPath, result.issues);
            ReadField((*videos)[i], "path", filePath, videoPath, result.issues);
            ReadField((*videos)[i], "startSeconds", video.startSeconds, videoPath, result.issues);
            ReadField((*videos)[i], "speed", video.speed, videoPath, result.issues);
            std::string fit = "fill";
            ReadField((*videos)[i], "fit", fit, videoPath, result.issues);
            if (fit == "fill")
                video.fit = VideoFit::Fill;
            else if (fit == "fit")
                video.fit = VideoFit::Fit;
            else if (fit == "stretch")
                video.fit = VideoFit::Stretch;
            else
                result.issues.push_back(Error(videoPath + "/fit", "unknown fit \"" + fit + "\" (expected \"fill\", \"fit\" or \"stretch\")"));
            video.path = filePath;
            performance.videos.push_back(std::move(video));
        }
    }

    auto scenes = root.find("scenes");
    if (scenes == root.end() || !scenes->is_array())
    {
        result.issues.push_back(Error("/scenes", "missing or not an array"));
        return result;
    }
    std::vector<ClassicScene> classics;
    std::vector<std::string> paths;
    for (size_t i = 0; i < scenes->size(); ++i)
    {
        const std::string path = "/scenes/" + std::to_string(i);
        if (!(*scenes)[i].is_object())
        {
            result.issues.push_back(Error(path, "scene must be an object"));
            continue;
        }
        classics.emplace_back();
        paths.push_back(path);
        performance.scenes.push_back(ParseScene((*scenes)[i], path, result.issues, classics.back()));
    }
    if (version < 3)
    {
        MigrateFileSources(performance, classics);
        for (size_t i = 0; i < performance.scenes.size(); ++i)
        {
            const auto& video = classics[i].video;
            if (video.enabled && video.source.type == VideoSourceType::Syphon && video.playlist.enabled && !video.playlist.videos.empty())
                result.issues.push_back(Warning(paths[i] + "/video/playlist", "a video playlist only works with video files, so it was left out of this Syphon scene"));
            performance.scenes[i].graph = GraphFromClassic(classics[i]);
        }
    }
    for (size_t i = 0; i < performance.scenes.size(); ++i)
    {
        auto& graph = performance.scenes[i].graph;
        FollowWires(graph);
        const auto errors = ValidateGraph(graph);
        if (!errors.empty() && !graph.wires.empty())
        {
            // A damaged or hand-edited graph loads unfinished: its nodes, with nothing wired.
            for (const auto& error : errors)
                result.issues.push_back(Warning(paths[i] + "/graph", NodeLabel(error) + error.message));
            result.issues.push_back(Warning(paths[i] + "/graph", "the scene's wires were removed so it can load; rewire it on the node canvas"));
            graph.wires.clear();
            FollowWires(graph);
        }
        performance.scenes[i].Refresh();
    }

    auto validation = Validate(performance);
    result.issues.insert(result.issues.end(), validation.begin(), validation.end());
    result.ok = !result.HasErrors();
    return result;
}

auto LoadPerformance(const std::filesystem::path& file) -> LoadResult
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        LoadResult result;
        result.issues.push_back(Error("", "could not open " + file.string()));
        return result;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();

    auto result = ParsePerformance(buffer.str());
    auto fileIssues = CheckFiles(result.performance);
    result.issues.insert(result.issues.end(), fileIssues.begin(), fileIssues.end());
    return result;
}

auto SerializePerformance(const Performance& performance) -> std::string
{
    json root;
    root["version"] = kCurrentFormatVersion;
    root["name"] = performance.name;
    if (!performance.canvas.matchScreen)
        root["canvas"] = {{"width", performance.canvas.width}, {"height", performance.canvas.height}};
    root["scenes"] = json::array();
    for (const auto& scene : performance.scenes)
    {
        json object;
        object["id"] = scene.id;
        if (!scene.name.empty())
        {
            object["name"] = scene.name;
        }
        object["graph"] = GraphJson(scene.graph);
        object["midi"] = {{"cc20Value", scene.cc20Value}};
        if (scene.beatHit != BeatHit{})
        {
            const auto& hit = scene.beatHit;
            object["beatHit"] = {{"enabled", hit.enabled},
                                 {"every", hit.every == BeatHitEvery::Beat ? "beat" : hit.every == BeatHitEvery::TwoBeats ? "2beats" : "bar"},
                                 {"sound", hit.sound == BeatHitSound::Kick ? "kick" : hit.sound == BeatHitSound::Snare ? "snare" : "burst"},
                                 {"strength", hit.strength}};
        }
        root["scenes"].push_back(object);
    }
    root["videos"] = json::array();
    for (const auto& video : performance.videos)
    {
        json object = {{"id", video.id}, {"path", video.path.string()}, {"startSeconds", video.startSeconds}, {"speed", video.speed},
                       {"fit", video.fit == VideoFit::Fill ? "fill" : video.fit == VideoFit::Fit ? "fit" : "stretch"}};
        if (!video.name.empty())
            object["name"] = video.name;
        root["videos"].push_back(std::move(object));
    }
    return root.dump(2) + "\n";
}

auto BackupOlderFormat(const std::filesystem::path& file) -> std::filesystem::path
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return {};
    int version = 0;
    try
    {
        const json root = json::parse(in);
        if (root.is_object() && root.contains("version") && root["version"].is_number_integer())
            version = root["version"].get<int>();
    }
    catch (const json::exception&)
    {
        return {};
    }
    if (version < 1 || version >= kCurrentFormatVersion)
        return {};
    const auto backup = file.parent_path() / (file.stem().string() + ".v" + std::to_string(version) + "-backup.json");
    std::error_code ec;
    if (std::filesystem::exists(backup, ec) || !std::filesystem::copy_file(file, backup, ec))
        return {};
    return backup;
}

auto SavePerformance(const Performance& performance, const std::filesystem::path& file) -> std::string
{
    const auto temp = std::filesystem::path(file.string() + ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return "could not write " + temp.string();
        }
        out << SerializePerformance(performance);
        if (!out)
        {
            return "could not write " + temp.string();
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, file, ec);
    if (ec)
    {
        return "could not replace " + file.string() + ": " + ec.message();
    }
    return {};
}

auto Validate(const Performance& performance) -> std::vector<Issue>
{
    std::vector<Issue> issues;
    if (performance.scenes.size() > static_cast<size_t>(kMaxScenes))
    {
        issues.push_back(Error("/scenes", "has " + std::to_string(performance.scenes.size()) + " scenes; the maximum is " + std::to_string(kMaxScenes)));
    }
    if (!performance.canvas.matchScreen)
    {
        for (const auto& [key, size] : {std::pair{"width", performance.canvas.width}, std::pair{"height", performance.canvas.height}})
        {
            if (size < kMinCanvasSize || size > kMaxCanvasSize || size % 2 != 0)
                issues.push_back(Error(std::string("/canvas/") + key, "must be an even number from " + std::to_string(kMinCanvasSize) + " to " +
                                                                          std::to_string(kMaxCanvasSize) + " (found " + std::to_string(size) + ")"));
        }
    }

    std::set<std::string> ids;
    std::set<int> ccValues;
    for (size_t i = 0; i < performance.scenes.size(); ++i)
    {
        const auto& scene = performance.scenes[i];
        const std::string path = "/scenes/" + std::to_string(i);

        if (scene.id.empty())
        {
            issues.push_back(Error(path + "/id", "is empty"));
        }
        else if (!ids.insert(scene.id).second)
        {
            issues.push_back(Error(path + "/id", "\"" + scene.id + "\" is used by another scene"));
        }

        if (scene.cc20Value < 0 || scene.cc20Value > 127)
        {
            issues.push_back(Error(path + "/midi/cc20Value", "must be between 0 and 127"));
        }
        else if (!ccValues.insert(scene.cc20Value).second)
        {
            issues.push_back(Error(path + "/midi/cc20Value", std::to_string(scene.cc20Value) + " is used by another scene"));
        }

        if (scene.beatHit.strength < 0.f || scene.beatHit.strength > 1.f)
            issues.push_back(Error(path + "/beatHit/strength", "must be between 0 and 1"));

        for (const auto& error : ValidateGraph(scene.graph))
            issues.push_back(Error(path + "/graph", NodeLabel(error) + error.message));
        // A node with nothing chosen yet (as the canvas adds them) is unfinished, not broken: a warning, so the
        // show still saves, opens and recovers after a crash. A video the pool doesn't have is broken.
        for (const auto& node : scene.graph.nodes)
        {
            std::vector<ShowProblem> problems;
            if (const NodeDef& def = Def(node.type); def.checkInShow != nullptr)
                def.checkInShow(performance, node, problems);
            for (const auto& problem : problems)
            {
                const std::string message = "node \"" + node.id + "\": " + problem.message;
                issues.push_back(problem.error ? Error(path + "/graph", message) : Warning(path + "/graph", message));
            }
        }
    }
    std::set<std::string> videoIds;
    for (size_t i = 0; i < performance.videos.size(); ++i)
    {
        const auto& video = performance.videos[i];
        const std::string path = "/videos/" + std::to_string(i);
        if (video.id.empty())
            issues.push_back(Error(path + "/id", "is empty"));
        else if (!videoIds.insert(video.id).second)
            issues.push_back(Error(path + "/id", "\"" + video.id + "\" is used by another video"));
        if (video.path.empty())
            issues.push_back(Error(path + "/path", "is empty"));
        if (video.speed < kMinVideoSpeed || video.speed > kMaxVideoSpeed)
            issues.push_back(Error(path + "/speed", "must be between 0.1 and 4"));
        if (video.startSeconds < 0.0)
            issues.push_back(Error(path + "/startSeconds", "can't be negative"));
    }
    return issues;
}

auto CheckFiles(Performance& performance) -> std::vector<Issue>
{
    std::vector<Issue> issues;
    for (size_t i = 0; i < performance.videos.size(); ++i)
    {
        auto& video = performance.videos[i];
        std::error_code ec;
        video.missing = !video.path.empty() && !std::filesystem::is_regular_file(video.path, ec);
        if (video.missing)
            issues.push_back(Warning("/videos/" + std::to_string(i) + "/path", "file not found: " + video.path.string()));
    }
    for (size_t i = 0; i < performance.scenes.size(); ++i)
    {
        auto& scene = performance.scenes[i];
        const std::string path = "/scenes/" + std::to_string(i);
        std::error_code ec;
        scene.Refresh();

        // The presets the viewport can show (unreachable preset nodes play nothing, so they don't count).
        scene.presetsMissing.assign(scene.presetEntries.size(), false);
        for (size_t p = 0; p < scene.presetEntries.size(); ++p)
        {
            const auto& preset = scene.presetEntries[p].path;
            scene.presetsMissing[p] = !preset.empty() && !std::filesystem::is_regular_file(preset, ec);
            if (scene.presetsMissing[p])
                issues.push_back(Warning(path + "/graph", "preset file not found: " + preset.string()));
        }
        scene.presetMissing = scene.presetsMissing.front();

        scene.videoMissing = false;
        for (const auto& id : VideoIdsOf(scene.graph))
        {
            if (const PoolVideo* video = performance.FindVideo(id); video != nullptr && video->missing)
                scene.videoMissing = true;
        }
    }
    return issues;
}

} // namespace gv::model
