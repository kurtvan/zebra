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
    m_liveItem = 0;
    m_itemInstance = 0;
    m_jump = false;
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

    // The most recent trigger wins over a pending jump to a preset entry.
    if (m_jump)
    {
        m_jump = false;
        commands = ClearItem();
    }

    m_pending = scene;
    if (auto id = InstanceFor(scene))
    {
        if (m_instances[*id].ready)
        {
            auto activate = Activate(*id);
            commands.insert(commands.end(), activate.begin(), activate.end());
            return commands;
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
    if (instance == m_itemInstance && m_jump)
    {
        m_jump = false;
        m_itemInstance = 0;
        return Activate(instance);
    }
    if (m_pending == it->second.scene && it->second.item == 0)
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
        pendingLost = pendingLost || (it->second.scene == m_pending && it->second.item == 0 && id != m_itemInstance);
        const Instance lost = it->second;
        m_instances.erase(it);
        if (id == m_itemInstance && m_jump)
        {
            commands.push_back(Prepare(lost.scene, true, lost.item)); // a jump is still wanted
            m_itemInstance = commands.back().instance;
        }
        else if (id == m_itemInstance)
        {
            m_itemInstance = 0; // re-planned by the engine
        }
    }
    if (pendingLost)
    {
        commands.push_back(Prepare(m_pending, true));
    }
    Rebalance(commands);
    return commands;
}

auto ScenePool::Remap(int sceneCount, const std::vector<int>& oldToNew, const std::vector<bool>& unusable,
                      const std::vector<bool>& presetChanged) -> std::vector<PoolCommand>
{
    std::vector<PoolCommand> commands;
    auto mapScene = [&](int old) {
        return old >= 0 && old < static_cast<int>(oldToNew.size()) ? oldToNew[static_cast<size_t>(old)] : -1;
    };
    auto changed = [&](int scene) {
        return scene >= 0 && scene < static_cast<int>(presetChanged.size()) && presetChanged[static_cast<size_t>(scene)];
    };

    std::vector<bool> failed(static_cast<size_t>(std::max(0, sceneCount)), false);
    for (int old = 0; old < m_sceneCount; ++old)
    {
        const int scene = mapScene(old);
        if (scene >= 0 && scene < sceneCount && !changed(scene))
            failed[static_cast<size_t>(scene)] = m_failed[static_cast<size_t>(old)];
    }

    const int oldLive = m_live;
    m_live = mapScene(m_live); // -1 if removed: its instance stays on screen until the next trigger
    m_pending = mapScene(m_pending);

    for (auto it = m_instances.begin(); it != m_instances.end();)
    {
        const int scene = mapScene(it->second.scene);
        if (it->first == m_liveInstance)
        {
            it->second.scene = scene;
            ++it;
        }
        else if (scene < 0 || changed(scene))
        {
            commands.push_back({PoolCommand::Type::Release, it->second.scene, it->second.lane, it->first, false, it->second.item});
            if (it->first == m_itemInstance)
                m_itemInstance = 0; // stale next entry: the engine plans a new one
            it = m_instances.erase(it);
        }
        else
        {
            it->second.scene = scene;
            ++it;
        }
    }

    m_sceneCount = std::max(0, sceneCount);
    m_unusable = unusable;
    m_unusable.resize(static_cast<size_t>(m_sceneCount), false);
    m_failed = std::move(failed);
    if (m_jump && m_itemInstance == 0)
    {
        // The jump's scene was removed or changed: drop it.
        m_jump = false;
        if (m_pending >= 0 && !InstanceFor(m_pending))
            m_pending = -1;
    }
    if (m_pending >= 0 && (m_unusable[static_cast<size_t>(m_pending)]))
        m_pending = -1;

    // The live scene's first preset changed while it's on entry 0: prepare the new one and swap it in when
    // ready. (On another entry it keeps that entry: ReloadItems.)
    if (oldLive >= 0 && m_live >= 0 && m_liveItem == 0 && changed(m_live) && !m_unusable[static_cast<size_t>(m_live)])
    {
        m_pending = m_live;
        commands.push_back(Prepare(m_live, true));
    }
    Rebalance(commands);
    return commands;
}

auto ScenePool::PrepareItem(int scene, int item) -> std::vector<PoolCommand>
{
    std::vector<PoolCommand> commands = ClearItem();
    if (scene != m_live || scene < 0)
        return commands;
    commands.push_back(Prepare(scene, false, item));
    m_itemInstance = commands.back().instance;
    return commands;
}

auto ScenePool::ActivateItem(InstanceId instance) -> std::vector<PoolCommand>
{
    auto it = m_instances.find(instance);
    if (instance == 0 || instance != m_itemInstance || it == m_instances.end() || !it->second.ready || it->second.scene != m_live)
        return {};
    m_itemInstance = 0;
    return Activate(instance);
}

auto ScenePool::TriggerItem(int scene, int item) -> std::vector<PoolCommand>
{
    if (scene < 0 || scene >= m_sceneCount || item < 0)
        return {};
    if (scene != m_live && item == 0)
        return Trigger(scene);
    if (m_unusable[static_cast<size_t>(scene)] || m_failed[static_cast<size_t>(scene)])
        return {};
    m_jump = false;
    std::vector<PoolCommand> commands = ClearItem();
    m_pending = scene == m_live ? -1 : scene;
    commands.push_back(Prepare(scene, true, item));
    m_itemInstance = commands.back().instance;
    m_jump = true;
    return commands;
}

auto ScenePool::ReloadItems(const std::vector<bool>& changed) -> std::vector<PoolCommand>
{
    std::vector<PoolCommand> commands;
    if (m_live < 0 || changed.empty() || m_unusable[static_cast<size_t>(m_live)] || m_failed[static_cast<size_t>(m_live)])
        return commands;
    auto stale = [&](int item) { return item >= static_cast<int>(changed.size()) || changed[static_cast<size_t>(item)]; };

    // The entry on screen: re-prepare it (or entry 0, if it's gone) and cut to it, unless the scene is being
    // left or replaced anyway.
    if (!m_jump && m_pending < 0 && m_liveItem != 0 && stale(m_liveItem))
        return TriggerItem(m_live, m_liveItem < static_cast<int>(changed.size()) ? m_liveItem : 0);

    // The prepared next entry (or a jump to an entry of the live scene): prepare it again in the background,
    // so switching to it stays instant.
    auto it = m_instances.find(m_itemInstance);
    if (it == m_instances.end() || it->second.scene != m_live || !stale(it->second.item))
        return commands;
    const int item = it->second.item < static_cast<int>(changed.size()) ? it->second.item : 0;
    commands.push_back({PoolCommand::Type::Release, it->second.scene, it->second.lane, it->first, false, it->second.item});
    m_instances.erase(it);
    commands.push_back(Prepare(m_live, m_jump, item));
    m_itemInstance = commands.back().instance;
    return commands;
}

auto ScenePool::ClearItem() -> std::vector<PoolCommand>
{
    if (m_jump)
        return {}; // a jump is cancelled by a trigger, not by the preset switcher's housekeeping
    std::vector<PoolCommand> commands;
    if (auto it = m_instances.find(m_itemInstance); it != m_instances.end())
    {
        commands.push_back({PoolCommand::Type::Release, it->second.scene, it->second.lane, it->first, false, it->second.item});
        m_instances.erase(it);
    }
    m_itemInstance = 0;
    return commands;
}

auto ScenePool::IsReady(InstanceId instance) const -> bool
{
    auto it = m_instances.find(instance);
    return it != m_instances.end() && it->second.ready;
}

auto ScenePool::InstancesOf(int scene) const -> std::vector<HostedInstance>
{
    std::vector<HostedInstance> result;
    for (const auto& [id, instance] : m_instances)
    {
        if (instance.scene == scene)
            result.push_back({id, instance.lane, instance.item});
    }
    return result;
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
        if (instance.scene == scene && id != m_liveInstance && instance.item == 0)
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

auto ScenePool::Prepare(int scene, bool priority, int item) -> PoolCommand
{
    const InstanceId id = m_nextId++;
    const int lane = PickLane(priority);
    m_instances[id] = {scene, lane, false, item};
    return {PoolCommand::Type::Prepare, scene, lane, id, priority, item};
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
    m_liveItem = next.item;
    m_pending = -1;
    commands.push_back({PoolCommand::Type::Activate, m_live, next.lane, instance, false});
    Rebalance(commands);
    return commands;
}

void ScenePool::Rebalance(std::vector<PoolCommand>& commands)
{
    // A next-entry instance only makes sense while its scene is live.
    // (A jump to another scene's entry is waiting to become live, so it stays.)
    if (auto it = m_instances.find(m_itemInstance); it != m_instances.end() && it->second.scene != m_live && !m_jump)
    {
        commands.push_back({PoolCommand::Type::Release, it->second.scene, it->second.lane, it->first, false, it->second.item});
        m_instances.erase(it);
        m_itemInstance = 0;
    }

    // Release instances outside the window (never the live one or the pending trigger), and stray preset entries.
    for (auto it = m_instances.begin(); it != m_instances.end();)
    {
        const auto& instance = it->second;
        const bool strayItem = instance.item != 0 && it->first != m_itemInstance && it->first != m_liveInstance;
        if (it->first != m_liveInstance && it->first != m_itemInstance &&
            (strayItem || (instance.scene != m_pending && !InWindow(instance.scene))))
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
