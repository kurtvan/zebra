#include "model/PerformanceJson.hpp"

#include "model/Document.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <set>
#include <sstream>

namespace gv::model {

using nlohmann::json;

namespace {

auto Error(std::string path, std::string message) -> Issue
{
    return {Issue::Severity::Error, std::move(path), std::move(message)};
}

auto Warning(std::string path, std::string message) -> Issue
{
    return {Issue::Severity::Warning, std::move(path), std::move(message)};
}

/// Reads an optional field of type T, recording an error if it has the wrong type.
template<typename T>
void ReadField(const json& object, const char* key, T& out, const std::string& path, std::vector<Issue>& issues)
{
    auto it = object.find(key);
    if (it == object.end() || it->is_null())
    {
        return;
    }
    try
    {
        out = it->get<T>();
    }
    catch (const json::exception&)
    {
        issues.push_back(Error(path + "/" + key, std::string("has the wrong type (found ") + it->type_name() + ")"));
    }
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
        issues.push_back(Error(path + "/type", "unknown video source type \"" + type + "\" (expected \"file\" or \"syphon\")"));
    }
    return source;
}

auto ParseScene(const json& object, const std::string& path, std::vector<Issue>& issues) -> Scene
{
    Scene scene;
    ReadField(object, "id", scene.id, path, issues);
    ReadField(object, "name", scene.name, path, issues);

    if (auto preset = object.find("preset"); preset != object.end() && preset->is_object())
    {
        std::string presetPath;
        ReadField(*preset, "path", presetPath, path + "/preset", issues);
        scene.presetPath = presetPath;
    }

    if (auto playlist = object.find("playlist"); playlist != object.end() && playlist->is_object())
    {
        const std::string playlistPath = path + "/playlist";
        ReadField(*playlist, "enabled", scene.playlist.enabled, playlistPath, issues);
        ReadField(*playlist, "intervalSeconds", scene.playlist.intervalSeconds, playlistPath, issues);
        ReadField(*playlist, "intervalCount", scene.playlist.intervalCount, playlistPath, issues);
        std::string unit = "seconds";
        ReadField(*playlist, "intervalUnit", unit, playlistPath, issues);
        if (unit == "seconds")
            scene.playlist.unit = IntervalUnit::Seconds;
        else if (unit == "beats")
            scene.playlist.unit = IntervalUnit::Beats;
        else if (unit == "bars")
            scene.playlist.unit = IntervalUnit::Bars;
        else
            issues.push_back(Error(playlistPath + "/intervalUnit", "unknown unit \"" + unit + "\" (expected \"seconds\", \"beats\" or \"bars\")"));
        ReadField(*playlist, "shuffle", scene.playlist.shuffle, playlistPath, issues);
        if (auto presets = playlist->find("presets"); presets != playlist->end() && presets->is_array())
        {
            for (size_t i = 0; i < presets->size(); ++i)
            {
                std::string presetPath;
                if ((*presets)[i].is_object())
                    ReadField((*presets)[i], "path", presetPath, playlistPath + "/presets/" + std::to_string(i), issues);
                scene.playlist.presets.emplace_back(presetPath);
            }
        }
    }

    if (auto video = object.find("video"); video != object.end() && video->is_object())
    {
        const std::string videoPath = path + "/video";
        ReadField(*video, "enabled", scene.video.enabled, videoPath, issues);
        ReadField(*video, "mix", scene.video.mix, videoPath, issues);
        ReadField(*video, "lumaMin", scene.video.lumaMin, videoPath, issues);
        ReadField(*video, "contrast", scene.video.contrast, videoPath, issues);
        ReadField(*video, "saturation", scene.video.saturation, videoPath, issues);
        ReadField(*video, "restartOnEnter", scene.video.restartOnEnter, videoPath, issues);
        if (auto source = video->find("source"); source != video->end() && source->is_object())
        {
            scene.video.source = ParseVideoSource(*source, videoPath + "/source", issues);
        }
    }

    if (auto midi = object.find("midi"); midi != object.end() && midi->is_object())
    {
        ReadField(*midi, "cc20Value", scene.cc20Value, path + "/midi", issues);
    }
    else
    {
        scene.cc20Value = -1; // Validate() reports it.
    }
    return scene;
}

/// Format 1 stored a file path per scene. Each distinct file becomes one pool video (start 0, speed 1).
void MigrateFileSources(Performance& performance)
{
    for (auto& scene : performance.scenes)
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
    for (size_t i = 0; i < scenes->size(); ++i)
    {
        const std::string path = "/scenes/" + std::to_string(i);
        if (!(*scenes)[i].is_object())
        {
            result.issues.push_back(Error(path, "scene must be an object"));
            continue;
        }
        performance.scenes.push_back(ParseScene((*scenes)[i], path, result.issues));
    }
    MigrateFileSources(performance);

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
    root["scenes"] = json::array();
    for (const auto& scene : performance.scenes)
    {
        json object;
        object["id"] = scene.id;
        if (!scene.name.empty())
        {
            object["name"] = scene.name;
        }
        object["preset"] = {{"path", scene.presetPath.string()}};
        if (scene.playlist.enabled || !scene.playlist.presets.empty())
        {
            json presets = json::array();
            for (const auto& preset : scene.playlist.presets)
                presets.push_back({{"path", preset.string()}});
            object["playlist"] = {
                {"enabled", scene.playlist.enabled},
                {"presets", presets},
                {"intervalUnit", scene.playlist.unit == IntervalUnit::Seconds ? "seconds" : scene.playlist.unit == IntervalUnit::Beats ? "beats" : "bars"},
                {"intervalSeconds", scene.playlist.intervalSeconds},
                {"intervalCount", scene.playlist.intervalCount},
                {"shuffle", scene.playlist.shuffle}};
        }

        json source;
        if (scene.video.source.type == VideoSourceType::File)
        {
            source = {{"type", "file"}, {"video", scene.video.source.videoId}};
        }
        else
        {
            source = {{"type", "syphon"}, {"appName", scene.video.source.appName}, {"serverName", scene.video.source.serverName}};
        }
        object["video"] = {
            {"enabled", scene.video.enabled},
            {"source", source},
            {"mix", scene.video.mix},
            {"lumaMin", scene.video.lumaMin},
            {"contrast", scene.video.contrast},
            {"saturation", scene.video.saturation},
            {"restartOnEnter", scene.video.restartOnEnter}};
        object["midi"] = {{"cc20Value", scene.cc20Value}};
        root["scenes"].push_back(object);
    }
    root["videos"] = json::array();
    for (const auto& video : performance.videos)
    {
        json object = {{"id", video.id}, {"path", video.path.string()}, {"startSeconds", video.startSeconds}, {"speed", video.speed}};
        if (!video.name.empty())
            object["name"] = video.name;
        root["videos"].push_back(std::move(object));
    }
    return root.dump(2) + "\n";
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

        if (scene.playlist.enabled)
        {
            if (scene.playlist.presets.empty())
                issues.push_back(Error(path + "/playlist/presets", "a playlist needs at least one preset"));
            for (size_t i = 0; i < scene.playlist.presets.size(); ++i)
            {
                if (scene.playlist.presets[i].empty())
                    issues.push_back(Error(path + "/playlist/presets/" + std::to_string(i) + "/path", "is empty"));
            }
            if (scene.playlist.unit == IntervalUnit::Seconds &&
                (scene.playlist.intervalSeconds < kMinPlaylistInterval || scene.playlist.intervalSeconds > kMaxPlaylistInterval))
                issues.push_back(Error(path + "/playlist/intervalSeconds", "must be between 1 and 3600 seconds"));
            if (scene.playlist.unit != IntervalUnit::Seconds && (scene.playlist.intervalCount < 1 || scene.playlist.intervalCount > kMaxPlaylistCount))
                issues.push_back(Error(path + "/playlist/intervalCount", "must be between 1 and 256"));
        }
        else if (scene.presetPath.empty())
        {
            issues.push_back(Error(path + "/preset/path", "is empty"));
        }

        if (scene.cc20Value < 0 || scene.cc20Value > 127)
        {
            issues.push_back(Error(path + "/midi/cc20Value", "must be between 0 and 127"));
        }
        else if (!ccValues.insert(scene.cc20Value).second)
        {
            issues.push_back(Error(path + "/midi/cc20Value", std::to_string(scene.cc20Value) + " is used by another scene"));
        }

        if (scene.video.mix < 0.f || scene.video.mix > 1.f)
        {
            issues.push_back(Error(path + "/video/mix", "must be between 0 and 1"));
        }
        if (scene.video.lumaMin < 0.f || scene.video.lumaMin > 1.f)
        {
            issues.push_back(Error(path + "/video/lumaMin", "must be between 0 and 1"));
        }
        if (scene.video.contrast < 0.f || scene.video.contrast > kMaxContrast)
        {
            issues.push_back(Error(path + "/video/contrast", "must be between 0 and 100"));
        }
        if (scene.video.saturation < 0.f || scene.video.saturation > 2.f)
        {
            issues.push_back(Error(path + "/video/saturation", "must be between 0 and 2"));
        }
        if (scene.video.enabled && scene.video.source.type == VideoSourceType::File && performance.FindVideo(scene.video.source.videoId) == nullptr)
        {
            issues.push_back(Error(path + "/video/source/video",
                                   scene.video.source.videoId.empty() ? "no video chosen but video is enabled"
                                                                      : "video \"" + scene.video.source.videoId + "\" isn't in the video pool"));
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

        scene.presetMissing = !scene.FirstPreset().empty() && !std::filesystem::is_regular_file(scene.FirstPreset(), ec);
        if (scene.presetMissing && !scene.IsPlaylist())
        {
            issues.push_back(Warning(path + "/preset/path", "file not found: " + scene.presetPath.string()));
        }
        scene.playlistMissing.assign(scene.playlist.presets.size(), false);
        for (size_t p = 0; p < scene.playlist.presets.size(); ++p)
        {
            const auto& preset = scene.playlist.presets[p];
            scene.playlistMissing[p] = !preset.empty() && !std::filesystem::is_regular_file(preset, ec);
            if (scene.playlistMissing[p] && scene.playlist.enabled)
                issues.push_back(Warning(path + "/playlist/presets/" + std::to_string(p) + "/path", "file not found: " + preset.string()));
        }

        const PoolVideo* video = performance.VideoFor(scene);
        scene.videoMissing = scene.video.enabled && video != nullptr && !std::filesystem::is_regular_file(video->path, ec);
    }
    return issues;
}

} // namespace gv::model
