#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gv::model {

constexpr int kCurrentFormatVersion = 1;
constexpr int kMaxScenes = 128; //!< One MIDI CC (CC 20) on one channel.
constexpr float kMaxContrast = 100.0f; //!< At ~50-100 the video becomes a hard split at mid-grey.

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
    float contrast{1.0f};   //!< 0..kMaxContrast, 1 = unchanged; very high = hard black/white (with saturation 0). Before the luma gate.
    float saturation{1.0f}; //!< 0..2, 1 = unchanged, 0 = greyscale.
    bool restartOnEnter{true};
};

/// A scene that cycles through several presets instead of showing one.
struct ScenePlaylist
{
    bool enabled{false};
    std::vector<std::filesystem::path> presets;
    double intervalSeconds{30.0}; //!< 1..3600
    bool shuffle{false};          //!< Random order (never the same preset twice in a row) instead of top to bottom.
};

constexpr double kMinPlaylistInterval = 1.0;
constexpr double kMaxPlaylistInterval = 3600.0;

struct Scene
{
    std::string id;   //!< Stable and unique within the performance.
    std::string name; //!< Display label; may be empty (the UI falls back to id).
    std::filesystem::path presetPath; //!< The preset, when the scene isn't a playlist.
    ScenePlaylist playlist;
    SceneVideo video;
    int cc20Value{0}; //!< 0..127, unique within the performance. Doesn't change when scenes are reordered.

    /// Set by the loader / file checks, not serialized.
    bool presetMissing{false};          //!< The first preset (see Presets()) is missing.
    bool videoMissing{false};
    std::vector<bool> playlistMissing;  //!< Per playlist entry.

    /// True if the scene cycles through a playlist.
    auto IsPlaylist() const -> bool
    {
        return playlist.enabled && !playlist.presets.empty();
    }

    /// The presets the scene shows: the playlist, or just presetPath.
    auto Presets() const -> std::vector<std::filesystem::path>
    {
        return IsPlaylist() ? playlist.presets : std::vector<std::filesystem::path>{presetPath};
    }

    /// The preset shown when the scene starts.
    auto FirstPreset() const -> const std::filesystem::path&
    {
        return IsPlaylist() ? playlist.presets.front() : presetPath;
    }

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
