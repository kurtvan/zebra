#include "model/Performance.hpp"

namespace gv::model {

void Scene::Refresh()
{
    presetEntries.clear();
    presetSwitcher.clear();
    const Node* viewport = graph.Viewport();
    const Node* top = viewport != nullptr ? graph.Upstream(viewport->id, 0) : nullptr;
    if (top != nullptr && top->type == NodeType::MilkdropPreset)
    {
        presetEntries.push_back({top->id, ParamsOf<MilkdropPresetParams>(*top).path, -1});
    }
    else if (top != nullptr && top->type == NodeType::Switcher)
    {
        for (int input = 0; input < ParamsOf<SwitcherParams>(*top).inputCount; ++input)
        {
            const Node* preset = graph.Upstream(top->id, input);
            if (preset != nullptr && preset->type == NodeType::MilkdropPreset)
                presetEntries.push_back({preset->id, ParamsOf<MilkdropPresetParams>(*preset).path, input});
        }
        if (!presetEntries.empty())
            presetSwitcher = top->id;
    }
    if (presetEntries.empty())
        presetEntries.emplace_back(); // nothing reaches the viewport yet
}

auto Performance::SceneForCc20(int value) const -> std::optional<size_t>
{
    for (size_t i = 0; i < scenes.size(); ++i)
    {
        if (scenes[i].cc20Value == value)
        {
            return i;
        }
    }
    return std::nullopt;
}

auto Performance::FindVideo(const std::string& id) const -> const PoolVideo*
{
    for (const auto& video : videos)
    {
        if (video.id == id)
            return &video;
    }
    return nullptr;
}

auto Performance::FreeCc20Value() const -> std::optional<int>
{
    std::vector<bool> used(kMaxScenes, false);
    for (const auto& scene : scenes)
    {
        if (scene.cc20Value >= 0 && scene.cc20Value < kMaxScenes)
        {
            used[static_cast<size_t>(scene.cc20Value)] = true;
        }
    }
    for (int value = 0; value < kMaxScenes; ++value)
    {
        if (!used[static_cast<size_t>(value)])
        {
            return value;
        }
    }
    return std::nullopt;
}

} // namespace gv::model
