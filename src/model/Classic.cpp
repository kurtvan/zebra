#include "model/Classic.hpp"

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

/// A chain read back from the graph: the effects between a preset input and its root, in source -> preset order.
struct Chain
{
    const Node* root{nullptr};
    std::vector<const Node*> effects;
};

auto ReadChain(const Graph& graph, const Node* last) -> Chain
{
    Chain chain;
    const Node* node = last;
    for (int steps = 0; node != nullptr && IsEffect(node->type) && steps <= static_cast<int>(graph.nodes.size()); ++steps)
    {
        chain.effects.push_back(node);
        node = graph.Upstream(node->id, 0);
    }
    std::reverse(chain.effects.begin(), chain.effects.end());
    chain.root = node;
    return chain;
}

/// The look a classic chain carries (Contrast -> Saturation -> Luma gate -> Opacity), or nullopt for any other chain.
struct ChainLook
{
    VideoLook look;
    float softness{0.0f};
    bool invert{false};
};

auto LookOf(const Chain& chain) -> std::optional<ChainLook>
{
    const auto& e = chain.effects;
    if (e.size() != 4 || e[0]->type != NodeType::Contrast || e[1]->type != NodeType::Saturation || e[2]->type != NodeType::LumaGate ||
        e[3]->type != NodeType::Opacity)
        return std::nullopt;
    ChainLook result;
    result.look.contrast = ParamsOf<ContrastParams>(*e[0]).contrast;
    result.look.saturation = ParamsOf<SaturationParams>(*e[1]).saturation;
    const auto& gate = ParamsOf<LumaGateParams>(*e[2]);
    result.look.lumaMin = gate.threshold;
    result.softness = gate.softness;
    result.invert = gate.invert;
    result.look.mix = ParamsOf<OpacityParams>(*e[3]).opacity;
    return result;
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

auto ClassicFromGraph(const Graph& graph) -> std::optional<ClassicScene>
{
    const Node* viewport = graph.Viewport();
    if (viewport == nullptr)
        return std::nullopt;
    ClassicScene scene;
    const Node* top = graph.Upstream(viewport->id, 0);
    if (top == nullptr)
        return scene; // unfinished: no preset

    // The presets: one, or a Frame switcher's.
    std::vector<const Node*> presets;
    if (top->type == NodeType::MilkdropPreset)
    {
        presets.push_back(top);
    }
    else if (top->type == NodeType::Switcher)
    {
        const auto& params = ParamsOf<SwitcherParams>(*top);
        for (int i = 0; i < params.inputCount; ++i)
        {
            const Node* input = graph.Upstream(top->id, i);
            if (input == nullptr)
                continue;
            if (input->type != NodeType::MilkdropPreset)
                return std::nullopt;
            presets.push_back(input);
        }
        scene.playlist.enabled = true;
        scene.playlist.unit = params.unit;
        scene.playlist.intervalSeconds = params.intervalSeconds;
        scene.playlist.intervalCount = params.intervalCount;
        scene.playlist.shuffle = params.shuffle;
        for (const Node* preset : presets)
            scene.playlist.presets.push_back(ParamsOf<MilkdropPresetParams>(*preset).path);
        if (presets.empty())
            return scene;
    }
    else
    {
        return std::nullopt;
    }

    // Each preset's look, from its feedback chain (and on-top chain, for the overlay).
    std::vector<VideoLook> looks;
    const Node* root = nullptr;
    std::optional<bool> invert;
    int withVideo = 0;
    for (const Node* preset : presets)
    {
        if (graph.WireInto(preset->id, static_cast<int>(PresetLayer::BeforeComposite)) != nullptr)
            return std::nullopt;
        for (const auto& wire : graph.wires)
        {
            if (wire.toNode == preset->id && wire.toInput == static_cast<int>(PresetLayer::Texture))
                return std::nullopt;
        }
        const Node* feedbackEnd = graph.Upstream(preset->id, static_cast<int>(PresetLayer::Feedback));
        const Node* onTopEnd = graph.Upstream(preset->id, static_cast<int>(PresetLayer::OnTop));
        if (feedbackEnd == nullptr)
        {
            if (onTopEnd != nullptr)
                return std::nullopt;
            looks.push_back({});
            continue;
        }
        const Chain feedback = ReadChain(graph, feedbackEnd);
        const auto feedbackLook = LookOf(feedback);
        if (!feedbackLook || feedback.root == nullptr || (root != nullptr && feedback.root != root) ||
            (invert && *invert != feedbackLook->invert))
            return std::nullopt;
        root = feedback.root;
        invert = feedbackLook->invert;
        VideoLook look = feedbackLook->look;
        if (onTopEnd != nullptr)
        {
            const Chain onTop = ReadChain(graph, onTopEnd);
            const auto onTopLook = LookOf(onTop);
            if (!onTopLook || onTop.root != root || onTopLook->look != look || onTopLook->invert != *invert)
                return std::nullopt;
            look.overlay = true;
            look.softness = onTopLook->softness;
        }
        looks.push_back(look);
        withVideo++;
    }
    if (withVideo != 0 && withVideo != static_cast<int>(presets.size()))
        return std::nullopt;

    if (scene.playlist.enabled)
    {
        scene.playlist.looks = looks;
        scene.presetPath = scene.playlist.presets.front();
    }
    else
    {
        scene.presetPath = ParamsOf<MilkdropPresetParams>(*presets.front()).path;
    }
    if (withVideo == 0)
        return scene;

    // The video: one source, or a Video switcher of sources.
    auto& video = scene.video;
    video.enabled = true;
    video.invertLuma = invert.value_or(false);
    video.look = looks.front();
    if (root->type == NodeType::VideoSource)
    {
        const auto& source = ParamsOf<VideoSourceParams>(*root);
        video.source = source.source;
        video.restartOnEnter = source.restartOnEnter;
        video.loop = source.loop;
    }
    else if (root->type == NodeType::Switcher && ParamsOf<SwitcherParams>(*root).kind == PortKind::Video)
    {
        const auto& params = ParamsOf<SwitcherParams>(*root);
        video.playlist.enabled = true;
        video.playlist.unit = params.unit;
        video.playlist.intervalSeconds = params.intervalSeconds;
        video.playlist.intervalCount = params.intervalCount;
        video.playlist.shuffle = params.shuffle;
        const VideoSourceParams* first = nullptr;
        for (int i = 0; i < params.inputCount; ++i)
        {
            const Node* input = graph.Upstream(root->id, i);
            if (input == nullptr)
                continue;
            if (input->type != NodeType::VideoSource || ParamsOf<VideoSourceParams>(*input).source.type != VideoSourceType::File)
                return std::nullopt;
            if (first == nullptr)
                first = &ParamsOf<VideoSourceParams>(*input);
            video.playlist.videos.push_back(ParamsOf<VideoSourceParams>(*input).source.videoId);
        }
        if (first == nullptr)
            return std::nullopt;
        video.source = {VideoSourceType::File, video.playlist.videos.front(), {}, {}, {}};
        video.restartOnEnter = first->restartOnEnter;
        video.loop = first->loop;
    }
    else
    {
        return std::nullopt;
    }
    return scene;
}

auto RuntimeViewFromGraph(const Graph& graph) -> ClassicScene
{
    ClassicScene scene;
    const Node* viewport = graph.Viewport();
    const Node* top = viewport != nullptr ? graph.Upstream(viewport->id, 0) : nullptr;
    if (top == nullptr)
        return scene;

    std::vector<const Node*> presets;
    if (top->type == NodeType::MilkdropPreset)
    {
        presets.push_back(top);
        scene.presetPath = ParamsOf<MilkdropPresetParams>(*top).path;
    }
    else if (top->type == NodeType::Switcher)
    {
        const auto& params = ParamsOf<SwitcherParams>(*top);
        for (int i = 0; i < params.inputCount; ++i)
        {
            const Node* input = graph.Upstream(top->id, i);
            if (input != nullptr && input->type == NodeType::MilkdropPreset)
                presets.push_back(input);
        }
        if (presets.empty())
            return scene;
        scene.playlist.enabled = true;
        scene.playlist.unit = params.unit;
        scene.playlist.intervalSeconds = params.intervalSeconds;
        scene.playlist.intervalCount = params.intervalCount;
        scene.playlist.shuffle = params.shuffle;
        for (const Node* preset : presets)
            scene.playlist.presets.push_back(ParamsOf<MilkdropPresetParams>(*preset).path);
        scene.playlist.looks.assign(presets.size(), VideoLook{});
        scene.presetPath = scene.playlist.presets.front();
    }
    else
    {
        return scene;
    }

    // The video: whatever the first wired preset input comes from, through its effects.
    const Node* root = nullptr;
    for (const Node* preset : presets)
    {
        for (const auto& wire : graph.wires)
        {
            if (wire.toNode != preset->id || root != nullptr)
                continue;
            const Chain chain = ReadChain(graph, graph.Find(wire.fromNode));
            if (chain.root != nullptr && (chain.root->type == NodeType::VideoSource || chain.root->type == NodeType::Switcher))
                root = chain.root;
        }
    }
    if (root == nullptr)
        return scene;
    auto& video = scene.video;
    if (root->type == NodeType::VideoSource)
    {
        const auto& source = ParamsOf<VideoSourceParams>(*root);
        video.enabled = true;
        video.source = source.source;
        video.restartOnEnter = source.restartOnEnter;
        video.loop = source.loop;
        return scene;
    }
    const auto& params = ParamsOf<SwitcherParams>(*root);
    const VideoSourceParams* first = nullptr;
    for (int i = 0; i < params.inputCount; ++i)
    {
        const Node* input = graph.Upstream(root->id, i);
        if (input == nullptr || input->type != NodeType::VideoSource || ParamsOf<VideoSourceParams>(*input).source.type != VideoSourceType::File)
            continue;
        if (first == nullptr)
            first = &ParamsOf<VideoSourceParams>(*input);
        video.playlist.videos.push_back(ParamsOf<VideoSourceParams>(*input).source.videoId);
    }
    if (first == nullptr)
        return scene;
    video.enabled = true;
    video.playlist.enabled = true;
    video.playlist.unit = params.unit;
    video.playlist.intervalSeconds = params.intervalSeconds;
    video.playlist.intervalCount = params.intervalCount;
    video.playlist.shuffle = params.shuffle;
    video.source = {VideoSourceType::File, video.playlist.videos.front(), {}, {}, {}};
    video.restartOnEnter = first->restartOnEnter;
    video.loop = first->loop;
    return scene;
}

} // namespace gv::model
