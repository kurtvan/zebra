#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gv::model {

constexpr int kCurrentFormatVersion = 2; //!< 2: video files live in a shared pool (Performance::videos).
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
    std::string videoId;        //!< File sources: the pool video (Performance::videos).
    std::filesystem::path path; //!< Format 1 only (a file path per scene); turned into a pool video when loading.
    std::string appName;        //!< Syphon sources: matched together with serverName (server UUIDs change per launch).
    std::string serverName;     //!< An empty name matches any; both empty connects to the first server found.
};

/// How the video is blended into a preset.
struct VideoLook
{
    float mix{1.0f};     //!< 0..1
    float lumaMin{0.0f}; //!< 0..1
    float contrast{1.0f};   //!< 0..kMaxContrast, 1 = unchanged; very high = hard black/white (with saturation 0). Before the luma gate.
    float saturation{1.0f}; //!< 0..2, 1 = unchanged, 0 = greyscale.
    bool overlay{false};    //!< Also draw the video on top of the finished frame, masked by the luma gate.
    float softness{0.0f};   //!< 0..1: overlay mask edge, 0 = hard.

    auto operator==(const VideoLook&) const -> bool = default;
};

/// What a playlist's interval counts.
enum class IntervalUnit
{
    Seconds,
    Beats, //!< MIDI clock beats.
    Bars,  //!< MIDI clock bars (4/4).
};

/// A scene's file video that cycles through several pool videos instead of showing one. Each video plays
/// from its start point when it comes up.
struct VideoPlaylist
{
    bool enabled{false};
    std::vector<std::string> videos; //!< Pool video ids, in order.
    IntervalUnit unit{IntervalUnit::Seconds};
    double intervalSeconds{30.0}; //!< Unit Seconds: 1..3600.
    int intervalCount{4};         //!< Units Beats/Bars: 1..256, changing on bar lines of the MIDI clock.
    bool shuffle{false};          //!< Random order (never the same video twice in a row) instead of top to bottom.
};

struct SceneVideo
{
    bool enabled{false};
    VideoSource source;
    VideoLook look; //!< Single-preset scenes. Playlist entries have their own (ScenePlaylist::looks).
    bool restartOnEnter{true};
    bool invertLuma{false}; //!< The luma gate keeps the video's dark parts instead of its bright ones (every look of the scene).
    bool loop{true}; //!< File sources. Off: the video plays to the end once and holds its last frame until restarted.
    VideoPlaylist playlist; //!< File sources only.
};

/// A scene that cycles through several presets instead of showing one.
struct ScenePlaylist
{
    bool enabled{false};
    std::vector<std::filesystem::path> presets;
    std::vector<VideoLook> looks; //!< Per entry, kept the same length as `presets` (use the helpers below to edit).
    IntervalUnit unit{IntervalUnit::Seconds};
    double intervalSeconds{30.0}; //!< Unit Seconds: 1..3600.
    int intervalCount{4};         //!< Units Beats/Bars: 1..256, switching on beat/bar lines of the MIDI clock.
    bool shuffle{false};          //!< Random order (never the same preset twice in a row) instead of top to bottom.

    void Add(const std::filesystem::path& preset, const VideoLook& look)
    {
        looks.resize(presets.size(), look);
        presets.push_back(preset);
        looks.push_back(look);
    }
    void Remove(size_t item)
    {
        looks.resize(presets.size());
        presets.erase(presets.begin() + static_cast<std::ptrdiff_t>(item));
        looks.erase(looks.begin() + static_cast<std::ptrdiff_t>(item));
    }
    void Swap(size_t a, size_t b)
    {
        looks.resize(presets.size());
        std::swap(presets[a], presets[b]);
        std::swap(looks[a], looks[b]);
    }
};

constexpr int kMaxPlaylistCount = 256;

/// How a pool video's picture is fitted to the output canvas.
enum class VideoFit
{
    Fill,   //!< Covers the canvas, cropping the edges that don't fit.
    Fit,    //!< The whole picture, with black bars.
    Stretch //!< Stretched to the canvas shape.
};

constexpr double kMinVideoSpeed = 0.1;
constexpr double kMaxVideoSpeed = 4.0;

/// A video file in the performance's shared pool. Scenes refer to it by id; scenes using the same video
/// share one player, so it carries on from where it was unless a scene restarts it.
struct PoolVideo
{
    std::string id;   //!< Stable and unique within the performance.
    std::string name; //!< Display label; empty: the file name.
    std::filesystem::path path;
    double startSeconds{0.0}; //!< Where playback starts, restarts and loops back to.
    double speed{1.0};        //!< kMinVideoSpeed..kMaxVideoSpeed, forward only.
    VideoFit fit{VideoFit::Fill};

    bool missing{false}; //!< Set by the file checks, not serialized.

    auto DisplayName() const -> std::string
    {
        return name.empty() ? path.stem().string() : name;
    }
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

    /// True if the scene's video cycles through a playlist of pool videos.
    auto IsVideoPlaylist() const -> bool
    {
        return video.source.type == VideoSourceType::File && video.playlist.enabled && !video.playlist.videos.empty();
    }

    /// The pool videos the scene shows: the video playlist, or just the chosen one (none for Syphon).
    auto VideoIds() const -> std::vector<std::string>
    {
        if (IsVideoPlaylist())
            return video.playlist.videos;
        if (video.source.type == VideoSourceType::File && !video.source.videoId.empty())
            return {video.source.videoId};
        return {};
    }

    /// Whether the scene uses pool video `id` (as its video or in its video playlist).
    auto UsesVideo(const std::string& id) const -> bool
    {
        const auto ids = VideoIds();
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }

    /// The presets the scene shows: the playlist, or just presetPath.
    auto Presets() const -> std::vector<std::filesystem::path>
    {
        return IsPlaylist() ? playlist.presets : std::vector<std::filesystem::path>{presetPath};
    }

    /// The video look for entry `item` (playlists), or the scene's look.
    auto LookFor(int item) const -> const VideoLook&
    {
        if (IsPlaylist() && item >= 0 && item < static_cast<int>(playlist.looks.size()))
            return playlist.looks[static_cast<size_t>(item)];
        return video.look;
    }

    /// The look to edit for entry `item`: the entry's own in a playlist, else the scene's.
    auto MutableLookFor(int item) -> VideoLook&
    {
        if (IsPlaylist())
        {
            playlist.looks.resize(playlist.presets.size(), video.look);
            if (item >= 0 && item < static_cast<int>(playlist.looks.size()))
                return playlist.looks[static_cast<size_t>(item)];
        }
        return video.look;
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
    std::vector<PoolVideo> videos;

    auto FindVideo(const std::string& id) const -> const PoolVideo*;

    /// The pool video a scene's video starts with (the first of a video playlist), if it's a file source
    /// that exists in the pool.
    auto VideoFor(const Scene& scene) const -> const PoolVideo*;

    /// Index of the scene triggered by CC 20 value `value`, if any.
    auto SceneForCc20(int value) const -> std::optional<size_t>;

    /// Lowest CC 20 value not used by any scene, if any is free.
    auto FreeCc20Value() const -> std::optional<int>;
};

} // namespace gv::model
