#pragma once

#include "model/Classic.hpp"
#include "model/Graph.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gv::model {

constexpr int kCurrentFormatVersion = 3; //!< 3: scenes are node graphs. 2: video files live in a shared pool (Performance::videos).
constexpr int kMaxScenes = 128; //!< One MIDI CC (CC 20) on one channel.

/// Which MIDI clock beats get a beat hit.
enum class BeatHitEvery
{
    Beat,     //!< Every beat.
    TwoBeats, //!< Every second beat (beats 1 and 3 of a bar on the song grid).
    Bar,      //!< The first beat of each bar (4/4).
};

/// The synthetic transient a beat hit adds to the audio.
enum class BeatHitSound
{
    Kick,  //!< A low thump: drives bass.
    Snare, //!< A noisy snap: drives mid and treble.
    Burst, //!< A short broadband click: drives every band.
};

/// Adds a synthetic transient to the live scene's audio on the MIDI clock's beats, to strengthen the
/// visuals' reaction. Only while the clock is running; the visuals hear it, nothing is played.
struct BeatHit
{
    bool enabled{false};
    BeatHitEvery every{BeatHitEvery::Beat};
    BeatHitSound sound{BeatHitSound::Kick};
    float strength{0.5f}; //!< 0..1, peak level relative to full scale.

    auto operator==(const BeatHit&) const -> bool = default;
};

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

struct Scene
{
    std::string id;   //!< Stable and unique within the performance.
    std::string name; //!< Display label; may be empty (the UI falls back to id).
    Graph graph;      //!< What the scene shows.
    BeatHit beatHit;
    int cc20Value{0}; //!< 0..127, unique within the performance. Doesn't change when scenes are reordered.

    /// Derived from `graph` by Refresh(), not serialized. Until switchers run in the engine (N5), the engine,
    /// the video manager and the Scene panel read the graph through this classic view.
    ClassicScene classic;
    bool classicShape{true}; //!< False if the graph can't be read as a classic scene (not before the node canvas).

    /// Set by the loader / file checks, not serialized.
    bool presetMissing{false};          //!< The first preset (see Presets()) is missing.
    bool videoMissing{false};
    std::vector<bool> playlistMissing;  //!< Per playlist entry.

    /// Recomputes `classic` from `graph`. Called by the loader, CheckFiles and the edit helpers, so every
    /// scene the document holds is up to date.
    void Refresh();

    /// True if the scene cycles through a playlist.
    auto IsPlaylist() const -> bool
    {
        return classic.playlist.enabled && !classic.playlist.presets.empty();
    }

    /// True if the scene's video cycles through a playlist of pool videos.
    auto IsVideoPlaylist() const -> bool
    {
        const auto& video = classic.video;
        return video.source.type == VideoSourceType::File && video.playlist.enabled && !video.playlist.videos.empty();
    }

    /// The pool videos the scene shows: the video playlist, or just the chosen one (none for Syphon).
    auto VideoIds() const -> std::vector<std::string>
    {
        if (IsVideoPlaylist())
            return classic.video.playlist.videos;
        if (classic.video.source.type == VideoSourceType::File && !classic.video.source.videoId.empty())
            return {classic.video.source.videoId};
        return {};
    }

    /// Whether any video source node of the scene uses pool video `id`.
    auto UsesVideo(const std::string& id) const -> bool
    {
        const auto ids = VideoIdsOf(graph);
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }

    /// The presets the scene shows: the playlist, or just the one.
    auto Presets() const -> std::vector<std::filesystem::path>
    {
        return IsPlaylist() ? classic.playlist.presets : std::vector<std::filesystem::path>{classic.presetPath};
    }

    /// The video look for entry `item` (playlists), or the scene's.
    auto LookFor(int item) const -> const VideoLook&
    {
        if (IsPlaylist() && item >= 0 && item < static_cast<int>(classic.playlist.looks.size()))
            return classic.playlist.looks[static_cast<size_t>(item)];
        return classic.video.look;
    }

    /// The preset shown when the scene starts.
    auto FirstPreset() const -> const std::filesystem::path&
    {
        return IsPlaylist() ? classic.playlist.presets.front() : classic.presetPath;
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
