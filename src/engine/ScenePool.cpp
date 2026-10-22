#include "engine/ScenePool.hpp"

#include <algorithm>
#include <limits>

namespace gv::engine {

ScenePool::ScenePool(PoolConfig config)
    : m_config(config)
{
    m_config.lanes = std::max(2, m_config.lanes);
}

auto ScenePool::Reset(int sceneCount, const std::vector<bool>& unusable) -> std::vector<PoolCommand>
{
    std::vector<PoolCommand> commands;
    for (const auto& [id, instance] : m_instances)
    {
        commands.push_back({PoolCommand::Type::Release, instance.scene, instance.lane, id, false});
    }
    m_instances.clear();
    m_sceneCount = std::max(0, sceneCount);
    m_unusable = unusable;
    m_unusable.resize(static_cast<size_t>(m_sceneCount), false);
    m_failed.assign(static_cast<size_t>(m_sceneCount), false);
    m_live = -1;
    m_liveInstance = 0;
    m_pending = -1;
    Rebalance(commands);
    return commands;
}

auto ScenePool::Trigger(int scene) -> std::vector<PoolCommand>
{
    std::vector<PoolCommand> commands;
    if (scene < 0 || scene >= m_sceneCount || scene == m_live)
    {
        return commands;
    }
    if (m_unusable[static_cast<size_t>(scene)] || m_failed[static_cast<size_t>(scene)])
    {
        return commands;
    }

    m_pending = scene;
    if (auto id = InstanceFor(scene))
    {
        if (m_instances[*id].ready)
        {
            return Activate(*id);
        }
        // Already preparing: activated in OnPrepared.
        return commands;
    }
    commands.push_back(Prepare(scene, true));
    return commands;
}

auto ScenePool::OnPrepared(InstanceId instance, bool ok) -> std::vector<PoolCommand>
{
    std::vector<PoolCommand> commands;
    auto it = m_instances.find(instance);
    if (it == m_instances.end())
    {
        return commands; // Released before it finished.
    }
    if (!ok)
    {
        const int scene = it->second.scene;
        commands.push_back({PoolCommand::Type::Release, scene, it->second.lane, instance, false});
        m_failed[static_cast<size_t>(scene)] = true;
        m_instances.erase(it);
        if (m_pending == scene)
        {
            m_pending = -1;
        }
        return commands;
    }
    it->second.ready = true;
    if (m_pending == it->second.scene)
    {
        return Activate(instance);
    }
    return commands;
}

auto ScenePool::OnCancelled(const std::vector<InstanceId>& instances) -> std::vector<PoolCommand>
{
    std::vector<PoolCommand> commands;
    bool pendingLost = false;
    for (InstanceId id : instances)
    {
        auto it = m_instances.find(id);
        if (it == m_instances.end() || it->second.ready)
        {
            continue;
        }
        pendingLost = pendingLost || it->second.scene == m_pending;
        m_instances.erase(it);
    }
    if (pendingLost)
    {
        commands.push_back(Prepare(m_pending, true));
    }
    Rebalance(commands);
    return commands;
}

auto ScenePool::LiveLane() const -> int
{
    auto it = m_instances.find(m_liveInstance);
    return it == m_instances.end() ? -1 : it->second.lane;
}

auto ScenePool::StatusOf(int scene) const -> Status
{
    if (scene < 0 || scene >= m_sceneCount)
    {
        return {};
    }
    if (m_unusable[static_cast<size_t>(scene)])
    {
        return {State::Unusable, -1};
    }
    if (m_failed[static_cast<size_t>(scene)])
    {
        return {State::Failed, -1};
    }
    if (scene == m_live)
    {
        return {State::Live, LiveLane()};
    }
    if (auto id = InstanceFor(scene))
    {
        const auto& instance = m_instances.at(*id);
        return {instance.ready ? State::Ready : State::Preparing, instance.lane};
    }
    return {};
}

auto ScenePool::InstanceFor(int scene) const -> std::optional<InstanceId>
{
    for (const auto& [id, instance] : m_instances)
    {
        if (instance.scene == scene && id != m_liveInstance)
        {
            return id;
        }
    }
    return std::nullopt;
}

auto ScenePool::InWindow(int scene) const -> bool
{
    if (m_live < 0)
    {
        return scene >= 0 && scene <= m_config.ahead;
    }
    return scene >= m_live - m_config.behind && scene <= m_live + m_config.ahead;
}

auto ScenePool::PickLane(bool priority) const -> int
{
    const int liveLane = LiveLane();
    std::vector<int> hosted(static_cast<size_t>(m_config.lanes), 0);
    std::vector<int> preparing(static_cast<size_t>(m_config.lanes), 0);
    for (const auto& [id, instance] : m_instances)
    {
        hosted[static_cast<size_t>(instance.lane)]++;
        if (!instance.ready)
        {
            preparing[static_cast<size_t>(instance.lane)]++;
        }
    }

    // Priority work goes to the least busy lane; background work spreads instances evenly.
    int best = -1;
    int bestScore = std::numeric_limits<int>::max();
    for (int lane = 0; lane < m_config.lanes; ++lane)
    {
        if (lane == liveLane)
        {
            continue;
        }
        const int score = priority ? preparing[static_cast<size_t>(lane)] * 1000 + hosted[static_cast<size_t>(lane)]
                                   : hosted[static_cast<size_t>(lane)] * 1000 + preparing[static_cast<size_t>(lane)];
        if (score < bestScore)
        {
            best = lane;
            bestScore = score;
        }
    }
    return best;
}

auto ScenePool::Prepare(int scene, bool priority) -> PoolCommand
{
    const InstanceId id = m_nextId++;
    const int lane = PickLane(priority);
    m_instances[id] = {scene, lane, false};
    return {PoolCommand::Type::Prepare, scene, lane, id, priority};
}

auto ScenePool::Activate(InstanceId instance) -> std::vector<PoolCommand>
{
    std::vector<PoolCommand> commands;
    const auto& next = m_instances.at(instance);

    // Fresh on entry: the scene we leave never resumes; it's re-prepared below if it's still in the window.
    if (m_liveInstance != 0)
    {
        const auto& previous = m_instances.at(m_liveInstance);
        commands.push_back({PoolCommand::Type::Release, previous.scene, previous.lane, m_liveInstance, false});
        m_instances.erase(m_liveInstance);
    }

    m_live = next.scene;
    m_liveInstance = instance;
    m_pending = -1;
    commands.push_back({PoolCommand::Type::Activate, m_live, next.lane, instance, false});
    Rebalance(commands);
    return commands;
}

void ScenePool::Rebalance(std::vector<PoolCommand>& commands)
{
    // Release instances outside the window (never the live one or the pending trigger).
    for (auto it = m_instances.begin(); it != m_instances.end();)
    {
        const auto& instance = it->second;
        if (it->first != m_liveInstance && instance.scene != m_pending && !InWindow(instance.scene))
        {
            commands.push_back({PoolCommand::Type::Release, instance.scene, instance.lane, it->first, false});
            it = m_instances.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Prepare missing scenes in priority order: next, previous, then further ahead.
    std::vector<int> order;
    const int anchor = m_live < 0 ? -1 : m_live;
    order.push_back(anchor + 1);
    for (int back = 1; back <= m_config.behind; ++back)
    {
        order.push_back(anchor - back);
    }
    for (int ahead = 2; ahead <= m_config.ahead + (m_live < 0 ? 1 : 0); ++ahead)
    {
        order.push_back(anchor + ahead);
    }
    for (int scene : order)
    {
        if (scene < 0 || scene >= m_sceneCount || scene == m_live || !InWindow(scene))
        {
            continue;
        }
        if (m_unusable[static_cast<size_t>(scene)] || m_failed[static_cast<size_t>(scene)] || InstanceFor(scene))
        {
            continue;
        }
        commands.push_back(Prepare(scene, false));
    }
}

} // namespace gv::engine
