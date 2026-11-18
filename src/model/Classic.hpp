#pragma once

#include "model/Graph.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gv::model {

// Format 1-2 scene settings ("classic" scenes): a preset or preset playlist, plus one video with its look.
// Shows in those formats are converted to graphs when they load (GraphFromClassic). Until switchers run in the
// engine (N5), the engine, the video manager and the read-only Scene panel read graphs back through
// ClassicFromGraph. N5 deletes everything here except what the format-2 parser needs.

/// How the video is blended into a preset.
struct VideoLook
{
    float mix{1.0f};     //!< 0..1
    float lumaMin{0.0f}; //!< 0..1
    float contrast{1.0f};   //!< 0..kMaxContrast, 1 = unchanged. Before the luma gate.
    float saturation{1.0f}; //!< 0..2, 1 = unchanged, 0 = greyscale.
    bool overlay{false};    //!< Also draw the video on top of the finished frame, masked by the luma gate.
    float softness{0.0f};   //!< 0..1: overlay mask edge, 0 = hard.

    auto operator==(const VideoLook&) const -> bool = default;
};

/// A scene's file video that cycles through several pool videos.
struct VideoPlaylist
{
    bool enabled{false};
    std::vector<std::string> videos; //!< Pool video ids, in order.
    IntervalUnit unit{IntervalUnit::Seconds};
    double intervalSeconds{30.0};
    int intervalCount{4};
    bool shuffle{false};

    auto operator==(const VideoPlaylist&) const -> bool = default;
};

struct SceneVideo
{
    bool enabled{false};
    VideoSource source;
    VideoLook look; //!< Single-preset scenes. Playlist entries have their own (ScenePlaylist::looks).
    bool restartOnEnter{true};
    bool invertLuma{false}; //!< The luma gate keeps the video's dark parts (every look of the scene).
    bool loop{true};
    VideoPlaylist playlist; //!< File sources only.

    auto operator==(const SceneVideo&) const -> bool = default;
};

/// A scene that cycles through several presets.
struct ScenePlaylist
{
    bool enabled{false};
    std::vector<std::filesystem::path> presets;
    std::vector<VideoLook> looks; //!< Per entry, the same length as `presets`.
    IntervalUnit unit{IntervalUnit::Seconds};
    double intervalSeconds{30.0};
    int intervalCount{4};
    bool shuffle{false};

    auto operator==(const ScenePlaylist&) const -> bool = default;
};

struct ClassicScene
{
    std::filesystem::path presetPath; //!< The preset, when the scene isn't a playlist.
    ScenePlaylist playlist;
    SceneVideo video;

    auto operator==(const ClassicScene&) const -> bool = default;
};

/// Today's feedback blend has a fixed soft edge (smoothstep +-0.05), which is a luma gate with this softness.
constexpr float kFeedbackGateSoftness = 0.1f;

/// The graph that plays a classic scene exactly as before (product doc, "Existing shows"):
/// - each preset gets a look chain (Contrast -> Saturation -> Luma gate -> Mix) into its Feedback input, and a
///   second chain into On top when its look has the overlay;
/// - a preset playlist becomes a Frame switcher, one preset per input;
/// - a video playlist (file sources) becomes a Video switcher of video sources, shared by every chain.
/// Disabled video, and a video playlist on a Syphon source (ignored today), are dropped. Nodes get
/// deterministic ids and a column layout.
auto GraphFromClassic(const ClassicScene& scene) -> Graph;

/// Reads a graph back as a classic scene, or nullopt if it isn't shaped like one (only possible once the node
/// canvas exists). The inverse of GraphFromClassic for everything it keeps.
auto ClassicFromGraph(const Graph& graph) -> std::optional<ClassicScene>;

/// What the engine and the video manager need from any graph, for scenes ClassicFromGraph can't read (video
/// into a texture or before the composite, or other chains): the presets in playlist order and the playlist
/// timing (a Frame switcher feeding the viewport), and the video source or Video switcher the presets' inputs
/// come from. Looks are left at their defaults; lanes draw from the compiled plan. Removed in N5 with the rest
/// of the classic view.
auto RuntimeViewFromGraph(const Graph& graph) -> ClassicScene;

/// `scene` as GraphFromClassic keeps it, so that ClassicFromGraph(GraphFromClassic(s)) == NormalizeClassic(s):
/// disabled video and Syphon video playlists dropped, a playlist scene's own look set to its first entry's.
auto NormalizeClassic(ClassicScene scene) -> ClassicScene;

} // namespace gv::model
