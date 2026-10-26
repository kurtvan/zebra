#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gv::model {

constexpr int kCurrentFormatVersion = 1;
constexpr int kMaxScenes = 128; //!< One MIDI CC (CC 20) on one channel.

enum class VideoSourceType
{
    File,
    Syphon
};

struct VideoSource
{
    VideoSourceType type{VideoSourceType::File};
    std::filesystem::path path; //!< File sources.
    std::string appName;        //!< Syphon sources: matched together with serverName (server UUIDs change per launch).
    std::string serverName;     //!< An empty name matches any; both empty connects to the first server found.
};

struct SceneVideo
{
    bool enabled{false};
    VideoSource source;
    float mix{1.0f};     //!< 0..1
    float lumaMin{0.0f}; //!< 0..1
    float contrast{1.0f};   //!< 0..2, 1 = unchanged. Applied before the luma gate.
    float saturation{1.0f}; //!< 0..2, 1 = unchanged, 0 = greyscale.
    bool restartOnEnter{true};
};

struct Scene
{
    std::string id;   //!< Stable and unique within the performance.
    std::string name; //!< Display label; may be empty (the UI falls back to id).
    std::filesystem::path presetPath;
    SceneVideo video;
    int cc20Value{0}; //!< 0..127, unique within the performance. Doesn't change when scenes are reordered.

    /// Set by the loader / file checks, not serialized.
    bool presetMissing{false};
    bool videoMissing{false};

    auto DisplayName() const -> const std::string&
    {
        return name.empty() ? id : name;
    }
};

struct Performance
{
    int version{kCurrentFormatVersion};
    std::string name;
    std::vector<Scene> scenes;

    /// Index of the scene triggered by CC 20 value `value`, if any.
    auto SceneForCc20(int value) const -> std::optional<size_t>;

    /// Lowest CC 20 value not used by any scene, if any is free.
    auto FreeCc20Value() const -> std::optional<int>;
};

} // namespace gv::model
