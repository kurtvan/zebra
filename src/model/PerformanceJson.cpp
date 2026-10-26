#include "model/PerformanceJson.hpp"

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
        ReadField(object, "path", filePath, path, issues);
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

/// Hook for upgrading older files. Version 1 is the first format, so there's nothing to do yet.
void Migrate(json& /*root*/, int /*fromVersion*/)
{
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
    if (version < kCurrentFormatVersion)
    {
        Migrate(root, version);
    }

    Performance& performance = result.performance;
    performance.version = kCurrentFormatVersion;
    ReadField(root, "name", performance.name, "", result.issues);

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

        json source;
        if (scene.video.source.type == VideoSourceType::File)
        {
            source = {{"type", "file"}, {"path", scene.video.source.path.string()}};
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

        if (scene.presetPath.empty())
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
        if (scene.video.contrast < 0.f || scene.video.contrast > 2.f)
        {
            issues.push_back(Error(path + "/video/contrast", "must be between 0 and 2"));
        }
        if (scene.video.saturation < 0.f || scene.video.saturation > 2.f)
        {
            issues.push_back(Error(path + "/video/saturation", "must be between 0 and 2"));
        }
        if (scene.video.enabled && scene.video.source.type == VideoSourceType::File && scene.video.source.path.empty())
        {
            issues.push_back(Error(path + "/video/source/path", "is empty but video is enabled"));
        }
    }
    return issues;
}

auto CheckFiles(Performance& performance) -> std::vector<Issue>
{
    std::vector<Issue> issues;
    for (size_t i = 0; i < performance.scenes.size(); ++i)
    {
        auto& scene = performance.scenes[i];
        const std::string path = "/scenes/" + std::to_string(i);
        std::error_code ec;

        scene.presetMissing = !scene.presetPath.empty() && !std::filesystem::is_regular_file(scene.presetPath, ec);
        if (scene.presetMissing)
        {
            issues.push_back(Warning(path + "/preset/path", "file not found: " + scene.presetPath.string()));
        }

        scene.videoMissing = scene.video.enabled && scene.video.source.type == VideoSourceType::File &&
                             !scene.video.source.path.empty() && !std::filesystem::is_regular_file(scene.video.source.path, ec);
        if (scene.videoMissing)
        {
            issues.push_back(Warning(path + "/video/source/path", "file not found: " + scene.video.source.path.string()));
        }
    }
    return issues;
}

} // namespace gv::model
