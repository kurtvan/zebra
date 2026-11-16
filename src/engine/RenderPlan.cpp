#include "engine/RenderPlan.hpp"

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

auto PassFor(const Node& node) -> EffectPass
{
    switch (node.type)
    {
    case NodeType::Contrast: return {EffectPass::Type::Contrast, model::ParamsOf<model::ContrastParams>(node).contrast, 0.0f, false};
    case NodeType::Saturation: return {EffectPass::Type::Saturation, model::ParamsOf<model::SaturationParams>(node).saturation, 0.0f, false};
    case NodeType::LumaGate:
    {
        const auto& gate = model::ParamsOf<model::LumaGateParams>(node);
        return {EffectPass::Type::LumaGate, gate.threshold, gate.softness, gate.invert};
    }
    default: return {EffectPass::Type::Mix, model::ParamsOf<model::MixParams>(node).opacity, 0.0f, false};
    }
}

} // namespace

auto CompilePlan(const Graph& graph, const SwitcherSelection& selection) -> RenderPlan
{
    RenderPlan plan;
    const Node* viewport = graph.Viewport();
    if (viewport == nullptr)
        return plan;
    const Node* preset = ThroughSwitchers(graph, graph.Upstream(viewport->id, 0), selection);
    if (preset == nullptr || preset->type != NodeType::Preset)
        return plan;
    plan.presetNode = preset->id;
    plan.presetPath = model::ParamsOf<model::PresetParams>(*preset).path;

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
        const Node* node = graph.Find(wire->fromNode);
        for (int steps = 0; node != nullptr && steps <= static_cast<int>(graph.nodes.size()); ++steps)
        {
            if (model::IsEffect(node->type))
            {
                branch.passes.push_back(PassFor(*node));
                node = graph.Upstream(node->id, 0);
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
        if (node == nullptr || node->type != NodeType::VideoSource)
            continue; // an unfinished chain carries no video
        std::reverse(branch.passes.begin(), branch.passes.end());
        plan.sourceNode = node->id;
        plan.source = model::ParamsOf<model::VideoSourceParams>(*node).source;
        plan.branches.push_back(std::move(branch));
    }
    return plan;
}

auto VideoSettingsFromPlan(const RenderPlan& plan) -> VideoSettings
{
    VideoSettings settings;
    for (const auto& branch : plan.branches)
    {
        const bool feedback = branch.layer == model::PresetLayer::Feedback;
        if (!feedback && branch.layer != model::PresetLayer::OnTop)
            continue;
        for (const auto& pass : branch.passes)
        {
            switch (pass.type)
            {
            case EffectPass::Type::Contrast: settings.contrast = pass.a; break;
            case EffectPass::Type::Saturation: settings.saturation = pass.a; break;
            case EffectPass::Type::LumaGate:
                settings.lumaMin = pass.a;
                settings.lumaInvert = pass.flag;
                if (!feedback)
                    settings.softness = pass.b;
                break;
            case EffectPass::Type::Mix: settings.mix = pass.a; break;
            }
        }
        if (feedback)
            settings.enabled = true;
        else
            settings.overlay = true;
    }
    if (!settings.enabled)
        return {};
    return settings;
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
