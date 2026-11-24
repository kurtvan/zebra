#include "model/ClassicView.hpp"

#include <algorithm>

namespace gv::test {

using namespace model;

namespace {

auto IsPresetPlaylist(const ScenePlaylist& playlist) -> bool
{
    return playlist.enabled && !playlist.presets.empty();
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

auto Classic(const Scene& scene) -> ClassicScene
{
    auto view = ClassicFromGraph(scene.graph);
    return view ? *view : ClassicScene{};
}

auto LookFor(const Scene& scene, int item) -> VideoLook
{
    const ClassicScene classic = Classic(scene);
    if (IsPresetPlaylist(classic.playlist) && item >= 0 && item < static_cast<int>(classic.playlist.looks.size()))
        return classic.playlist.looks[static_cast<size_t>(item)];
    return classic.video.look;
}

} // namespace gv::test
