#include "engine/RenderPlan.hpp"

#include "model/nodes/IsfShaderNode.hpp"

#include <algorithm>
#include <tuple>

namespace gv::engine {

using model::Graph;
using model::Node;
using model::NodeType;

namespace {

/// The input a switcher passes: the selected one if it's wired, else its first wired input. -1 if none is.
auto PassingInput(const Graph& graph, const Node& switcher, const SwitcherSelection& selection) -> int
{
    const int count = model::ParamsOf<model::SwitcherParams>(switcher).inputCount;
    if (auto it = selection.find(switcher.id); it != selection.end() && graph.WireInto(switcher.id, it->second) != nullptr)
        return it->second;
    for (int i = 0; i < count; ++i)
    {
        if (graph.WireInto(switcher.id, i) != nullptr)
            return i;
    }
    return -1;
}

/// Follows switchers to the node that actually feeds them.
auto ThroughSwitchers(const Graph& graph, const Node* node, const SwitcherSelection& selection) -> const Node*
{
    for (int steps = 0; node != nullptr && node->type == NodeType::Switcher && steps <= static_cast<int>(graph.nodes.size()); ++steps)
    {
        const int input = PassingInput(graph, *node, selection);
        node = input >= 0 ? graph.Upstream(node->id, input) : nullptr;
    }
    return node;
}

/// Appends `node` to `steps`: an effect joins the effects before it, an ISF shader is a step of its own. A
/// shader whose file is missing, broken with no earlier version, or uses a feature that can't run yet
/// (model::IsfFileAt) is left out, so the video passes through it and it's never compiled.
void AddStep(std::vector<VideoStep>& steps, const Graph& graph, const Node& node)
{
    if (node.type == NodeType::IsfShader)
    {
        const auto& params = model::ParamsOf<model::IsfShaderParams>(node);
        if (auto file = model::IsfFileAt(params.path); file.shader != nullptr && file.unsupported.empty())
            steps.push_back({{}, IsfStep{node.id, params.path, std::move(file.shader), params.values}});
        return;
    }
    if (steps.empty() || steps.back().isf)
        steps.emplace_back();
    steps.back().effects.push_back(model::Def(node.type).pass(node));
    PassModulation modulation{steps.back().effects.size() - 1, {node.id, node.type, 0.f, 0.f, true, node.params, node.depths}, {}};
    for (int param = 0; param < static_cast<int>(model::Def(node.type).params.size()); ++param)
    {
        if (const Node* from = graph.Upstream(node.id, model::ParamInput(node, param)); from != nullptr)
            modulation.signals.emplace_back(param, from->id);
    }
    if (!modulation.signals.empty())
        steps.back().modulations.push_back(std::move(modulation));
}

/// True if `node` is an ISF shader with no image input: it makes a picture of its own.
auto IsGenerator(const Node& node) -> bool
{
    if (node.type != NodeType::IsfShader)
        return false;
    const auto file = model::IsfFileAt(model::ParamsOf<model::IsfShaderParams>(node).path);
    return file.shader != nullptr && model::IsfImageInputs(*file.shader).empty();
}

/// A picture's path, walked back from the node feeding a preset input or the viewport.
struct Walked
{
    std::vector<VideoStep> steps;
    const Node* source{nullptr}; //!< The video source it starts at; null when a generator starts it.
};

/// Walks back from `node` through effects, ISF shaders and switchers (following each one's selected input) to
/// the video source the picture starts at or, with `generators`, a generator. Nullopt: the chain is unfinished.
auto Walk(const Graph& graph, const Node* node, const SwitcherSelection& selection, bool generators) -> std::optional<Walked>
{
    std::vector<const Node*> filters; // effects and ISF shaders, the last one first
    bool generator = false;
    for (int steps = 0; node != nullptr && steps <= static_cast<int>(graph.nodes.size()); ++steps)
    {
        if (generators && IsGenerator(*node))
        {
            filters.push_back(node);
            generator = true;
            break;
        }
        if (model::IsEffect(node->type) || node->type == NodeType::IsfShader)
        {
            filters.push_back(node);
            node = graph.Upstream(node->id, 0); // an ISF shader's other image inputs read black for now
        }
        else if (node->type == NodeType::Switcher)
        {
            node = ThroughSwitchers(graph, node, selection);
        }
        else
        {
            break;
        }
    }
    if (!generator && (node == nullptr || node->type != NodeType::VideoSource))
        return std::nullopt; // an unfinished chain carries no picture
    Walked walked;
    walked.source = generator ? nullptr : node;
    std::for_each(filters.rbegin(), filters.rend(), [&](const Node* filter) { AddStep(walked.steps, graph, *filter); });
    return walked;
}

} // namespace

auto CompilePlan(const Graph& graph, const SwitcherSelection& selection) -> RenderPlan
{
    RenderPlan plan;
    const Node* viewport = graph.Viewport();
    if (viewport == nullptr)
        return plan;
    const Node* top = ThroughSwitchers(graph, graph.Upstream(viewport->id, 0), selection);
    if (top == nullptr)
        return plan;
    auto use = [&](const Walked& walked, VideoBranch branch) {
        branch.steps = walked.steps;
        if (walked.source != nullptr)
        {
            plan.sourceNode = walked.source->id;
            plan.source = model::ParamsOf<model::VideoSourceParams>(*walked.source).source;
        }
        plan.branches.push_back(std::move(branch));
    };
    if (top->type != NodeType::MilkdropPreset)
    {
        // No preset: the picture goes straight onto the viewport.
        if (const auto walked = Walk(graph, top, selection, true))
            use(*walked, {});
        return plan;
    }
    const Node* preset = top;
    plan.presetNode = preset->id;
    plan.presetPath = model::ParamsOf<model::MilkdropPresetParams>(*preset).path;

    // Each wired video input, in input order (texture inputs by name).
    std::vector<const model::Wire*> inputs;
    for (const auto& wire : graph.wires)
    {
        if (wire.toNode == preset->id)
            inputs.push_back(&wire);
    }
    std::sort(inputs.begin(), inputs.end(), [](const model::Wire* a, const model::Wire* b) {
        return std::tie(a->toInput, a->textureName) < std::tie(b->toInput, b->textureName);
    });
    for (const model::Wire* wire : inputs)
    {
        VideoBranch branch;
        branch.layer = static_cast<model::PresetLayer>(wire->toInput);
        branch.textureName = wire->textureName;
        if (const auto walked = Walk(graph, graph.Find(wire->fromNode), selection, false))
            use(*walked, std::move(branch));
    }
    return plan;
}

auto Modulated(const std::vector<VideoStep>& steps, const model::SignalValues& signals) -> std::vector<VideoStep>
{
    std::vector<VideoStep> modulated = steps;
    for (auto& step : modulated)
    {
        for (const auto& modulation : step.modulations)
        {
            Node node = modulation.node;
            const auto& params = model::Def(node.type).params;
            for (const auto& [param, from] : modulation.signals)
            {
                if (const auto it = signals.find(from); it != signals.end())
                    params[static_cast<size_t>(param)].set(node, model::Modulate(modulation.node, param, it->second));
            }
            step.effects.at(modulation.effect) = model::Def(node.type).pass(node);
        }
    }
    return modulated;
}

auto HasModulations(const std::vector<VideoStep>& steps) -> bool
{
    return std::any_of(steps.begin(), steps.end(), [](const VideoStep& step) { return !step.modulations.empty(); });
}

auto Playable(const model::Scene& scene) -> bool
{
    if (scene.presetMissing)
        return false;
    return !scene.FirstPreset().empty() || CompilePlan(scene.graph, SelectionForItem(scene.graph, 0)).ShowsPicture();
}

auto NeedsNewInstances(const model::Scene& before, const model::Scene& after) -> bool
{
    if (before.FirstPreset() != after.FirstPreset())
        return true;
    // Same (or no) preset: a picture appearing or going away changes what kind of instance shows it.
    auto showsPicture = [](const model::Scene& scene) { return CompilePlan(scene.graph, SelectionForItem(scene.graph, 0)).ShowsPicture(); };
    return showsPicture(before) != showsPicture(after);
}

auto SelectionForItem(const Graph& graph, int item) -> SwitcherSelection
{
    const Node* viewport = graph.Viewport();
    const Node* top = viewport != nullptr ? graph.Upstream(viewport->id, 0) : nullptr;
    if (top == nullptr || top->type != NodeType::Switcher)
        return {};
    const int count = model::ParamsOf<model::SwitcherParams>(*top).inputCount;
    int wired = 0;
    for (int i = 0; i < count; ++i)
    {
        if (graph.WireInto(top->id, i) == nullptr)
            continue;
        if (wired++ == item)
            return {{top->id, i}};
    }
    return {};
}

} // namespace gv::engine
