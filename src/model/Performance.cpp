#include "model/Performance.hpp"

namespace gv::model {

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
