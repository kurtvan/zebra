#include "model/LegacyScene.hpp"

#include <algorithm>

namespace gv::model {

namespace {

// Column layout for converted scenes, left to right, spaced for the node canvas's node sizes (N4.3).
constexpr float kRow = 200.0f;         //!< One look chain (the luma gate is the tallest effect node).
constexpr float kSourceRow = 230.0f;   //!< Video sources stacked into a Video switcher.
constexpr float kPresetBlock = 420.0f; //!< At least one preset node (thumbnail and inputs) per block.
constexpr float kSourceX = 40.0f;
constexpr float kVideoSwitcherX = 320.0f;
constexpr float kContrastX = 600.0f;
constexpr float kSaturationX = 820.0f;
constexpr float kGateX = 1040.0f;
constexpr float kOpacityX = 1260.0f;
constexpr float kPresetX = 1500.0f;
constexpr float kFrameSwitcherX = 1800.0f;
constexpr float kViewportX = 2080.0f;

auto Add(Graph& graph, NodeType type, float x, float y, NodeParams params) -> std::string
{
    const std::string id = AddNode(graph, type, x, y);
    graph.Find(id)->params = std::move(params);
    return id;
}

auto IsVideoPlaylist(const SceneVideo& video) -> bool
{
    return video.source.type == VideoSourceType::File && video.playlist.enabled && !video.playlist.videos.empty();
}

auto IsPresetPlaylist(const ScenePlaylist& playlist) -> bool
{
    return playlist.enabled && !playlist.presets.empty();
}

auto LookAt(const ClassicScene& scene, size_t item) -> VideoLook
{
    if (IsPresetPlaylist(scene.playlist) && item < scene.playlist.looks.size())
        return scene.playlist.looks[item];
    return scene.video.look;
}

/// One look chain from `root` into `preset`'s `layer` input, laid out on row `y`.
void AddChain(Graph& graph, const std::string& root, const std::string& preset, PresetLayer layer, const VideoLook& look, bool invert,
              float softness, float y)
{
    const auto contrast = Add(graph, NodeType::Contrast, kContrastX, y, ContrastParams{look.contrast});
    const auto saturation = Add(graph, NodeType::Saturation, kSaturationX, y, SaturationParams{look.saturation});
    const auto gate = Add(graph, NodeType::LumaGate, kGateX, y, LumaGateParams{look.lumaMin, softness, invert});
    const auto mix = Add(graph, NodeType::Opacity, kOpacityX, y, OpacityParams{look.mix});
    Connect(graph, root, contrast, 0);
    Connect(graph, contrast, saturation, 0);
    Connect(graph, saturation, gate, 0);
    Connect(graph, gate, mix, 0);
    Connect(graph, mix, preset, static_cast<int>(layer));
}

} // namespace

auto NormalizeClassic(ClassicScene scene) -> ClassicScene
{
    const bool presetPlaylist = IsPresetPlaylist(scene.playlist);
    if (presetPlaylist)
    {
        scene.presetPath = scene.playlist.presets.front();
        scene.playlist.enabled = true;
        scene.playlist.looks.resize(scene.playlist.presets.size(), scene.video.look);
    }
    else
    {
        scene.playlist = {};
    }

    auto& video = scene.video;
    if (!video.enabled)
    {
        video = {};
        for (auto& look : scene.playlist.looks)
            look = {};
        return scene;
    }
    if (IsVideoPlaylist(video))
    {
        video.source = {VideoSourceType::File, video.playlist.videos.front(), {}, {}, {}};
    }
    else
    {
        video.playlist = {};
        if (video.source.type == VideoSourceType::File)
            video.source = {VideoSourceType::File, video.source.videoId, {}, {}, {}};
        else
            video.source = {VideoSourceType::Syphon, {}, {}, video.source.appName, video.source.serverName};
    }
    auto clean = [](VideoLook& look) {
        if (!look.overlay)
            look.softness = 0.0f;
    };
    for (auto& look : scene.playlist.looks)
        clean(look);
    clean(video.look);
    if (presetPlaylist)
        video.look = scene.playlist.looks.front();
    return scene;
}

auto GraphFromClassic(const ClassicScene& classic) -> Graph
{
    const ClassicScene scene = NormalizeClassic(classic);
    Graph graph;
    const bool presetPlaylist = IsPresetPlaylist(scene.playlist);
    const std::vector<std::filesystem::path> presets = presetPlaylist ? scene.playlist.presets : std::vector<std::filesystem::path>{scene.presetPath};
    const bool video = scene.video.enabled;

    // The video: one source, or a Video switcher of sources.
    std::string root;
    if (video && IsVideoPlaylist(scene.video))
    {
        const auto& playlist = scene.video.playlist;
        SwitcherParams params{PortKind::Video, playlist.unit, playlist.intervalSeconds, playlist.intervalCount, playlist.shuffle, 1};
        root = Add(graph, NodeType::Switcher, kVideoSwitcherX, 0.0f, params);
        for (size_t i = 0; i < playlist.videos.size(); ++i)
        {
            const VideoSourceParams source{{VideoSourceType::File, playlist.videos[i], {}, {}, {}}, scene.video.restartOnEnter, scene.video.loop};
            const auto id = Add(graph, NodeType::VideoSource, kSourceX, kSourceRow * static_cast<float>(i), source);
            Connect(graph, id, root, static_cast<int>(i));
        }
    }
    else if (video)
    {
        root = Add(graph, NodeType::VideoSource, kSourceX, 0.0f, VideoSourceParams{scene.video.source, scene.video.restartOnEnter, scene.video.loop});
    }

    // Each preset with its look chains. Rows: the feedback chain, then the overlay chain when there is one.
    std::vector<std::string> presetIds;
    float y = 0.0f;
    for (size_t i = 0; i < presets.size(); ++i)
    {
        const float blockStart = y;
        const std::string preset = Add(graph, NodeType::MilkdropPreset, kPresetX, y, MilkdropPresetParams{presets[i]});
        presetIds.push_back(preset);
        if (video)
        {
            const VideoLook look = LookAt(scene, i);
            AddChain(graph, root, preset, PresetLayer::Feedback, look, scene.video.invertLuma, kFeedbackGateSoftness, y);
            if (look.overlay)
            {
                y += kRow;
                AddChain(graph, root, preset, PresetLayer::OnTop, look, scene.video.invertLuma, look.softness, y);
            }
            y += kRow;
        }
        y = blockStart + std::max(y - blockStart, kPresetBlock);
    }

    const std::string viewport = Add(graph, NodeType::Viewport, kViewportX, 0.0f, ViewportParams{});
    if (presetPlaylist)
    {
        const auto& playlist = scene.playlist;
        SwitcherParams params{PortKind::Frame, playlist.unit, playlist.intervalSeconds, playlist.intervalCount, playlist.shuffle, 1};
        const auto switcher = Add(graph, NodeType::Switcher, kFrameSwitcherX, 0.0f, params);
        for (size_t i = 0; i < presetIds.size(); ++i)
            Connect(graph, presetIds[i], switcher, static_cast<int>(i));
        Connect(graph, switcher, viewport, 0);
    }
    else
    {
        Connect(graph, presetIds.front(), viewport, 0);
    }
    return graph;
}

} // namespace gv::model
