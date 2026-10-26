#include "app/Settings.hpp"

#include "common/log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace gv::app {

using nlohmann::json;

namespace {

constexpr const char* kLogCategory = "settings";

/// Suggested locations, relative to the home folder, checked on first launch.
/// The first ones are where the author keeps the Cream of the Crop pack and projectM's texture pack.
constexpr const char* kLibraryCandidates[] = {
    "projects/visualizers/libprojectM-4.1.4/presets/milk",
    "Music/MilkDrop/presets",
};
constexpr const char* kTextureCandidates[] = {
    "projects/visualizers/milkdrop-textures/textures",
    "Music/MilkDrop/textures",
};

auto Home() -> std::filesystem::path
{
    const char* home = std::getenv("HOME");
    return home != nullptr ? std::filesystem::path(home) : std::filesystem::path();
}

template<typename T>
void Read(const json& root, const char* key, T& out)
{
    auto it = root.find(key);
    if (it == root.end())
    {
        return;
    }
    try
    {
        out = it->get<T>();
    }
    catch (const json::exception&)
    {
        GV_LOG_WARN(kLogCategory, "ignoring setting \"%s\": wrong type", key);
    }
}

} // namespace

auto Settings::Defaults() -> Settings
{
    Settings settings;
    const auto home = Home();
    std::error_code ec;
    for (const char* candidate : kLibraryCandidates)
    {
        if (std::filesystem::is_directory(home / candidate, ec))
        {
            settings.libraryRoot = home / candidate;
            break;
        }
    }
    for (const char* candidate : kTextureCandidates)
    {
        if (std::filesystem::is_directory(home / candidate, ec))
        {
            settings.texturePaths.push_back(home / candidate);
            break;
        }
    }
    return settings;
}

auto Settings::Load(const std::filesystem::path& file) -> Settings
{
    std::ifstream in(file);
    if (!in)
    {
        GV_LOG_INFO(kLogCategory, "no settings at %s yet; using defaults", file.c_str());
        return Defaults();
    }

    json root;
    try
    {
        root = json::parse(in);
    }
    catch (const json::exception& error)
    {
        GV_LOG_WARN(kLogCategory, "settings file is invalid (%s); using defaults", error.what());
        return Defaults();
    }

    Settings settings;
    std::string libraryRoot;
    std::vector<std::string> texturePaths;
    std::string lastPerformance;
    Read(root, "libraryRoot", libraryRoot);
    Read(root, "texturePaths", texturePaths);
    Read(root, "audioDevice", settings.audioDevice);
    Read(root, "gain", settings.gain);
    Read(root, "midiChannel", settings.midiChannel);
    Read(root, "namesOnly", settings.namesOnly);
    Read(root, "lastPerformance", lastPerformance);

    settings.libraryRoot = libraryRoot;
    for (const auto& path : texturePaths)
        settings.texturePaths.emplace_back(path);
    settings.lastPerformance = lastPerformance;
    settings.gain = std::clamp(settings.gain, 0.0f, 16.0f);
    settings.midiChannel = std::clamp(settings.midiChannel, 0, 15);
    return settings;
}

auto Settings::Save(const std::filesystem::path& file) const -> std::string
{
    json root;
    root["libraryRoot"] = libraryRoot.string();
    root["texturePaths"] = json::array();
    for (const auto& path : texturePaths)
        root["texturePaths"].push_back(path.string());
    root["audioDevice"] = audioDevice;
    root["gain"] = gain;
    root["midiChannel"] = midiChannel;
    root["namesOnly"] = namesOnly;
    root["lastPerformance"] = lastPerformance.string();

    const auto temp = std::filesystem::path(file.string() + ".tmp");
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out)
            return "could not write " + temp.string();
        out << root.dump(2) << '\n';
    }
    std::error_code ec;
    std::filesystem::rename(temp, file, ec);
    return ec ? "could not replace " + file.string() + ": " + ec.message() : std::string();
}

} // namespace gv::app
