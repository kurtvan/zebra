#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace gv::engine {

using InstanceId = uint64_t; //!< Unique per prepared instance; never reused.

struct PoolConfig
{
    int lanes{3};  //!< Lanes available for hosting instances.
    int behind{1}; //!< Scenes kept ready before the live one.
    int ahead{3};  //!< Scenes kept ready after the live one.
};

/// What the engine must do on the lanes. Commands are executed in order.
struct PoolCommand
{
    enum class Type
    {
        Prepare,  //!< Create + load + warm up `instance` for `scene` on `lane`.
        Release,  //!< Destroy `instance` on `lane` (cancels it if not yet prepared).
        Activate, //!< Make `instance` on `lane` the live one from the next tick.
    };

    Type type{Type::Prepare};
    int scene{-1};
    int lane{-1};
    InstanceId instance{0};
    bool priority{false}; //!< Prepare: run before other queued work on that lane.
};

/// Decides which scenes are prepared on which lane, and when the live scene switches.
///
/// Pure scheduling logic with no GL or threads, so it can be unit-tested. Rules (technical doc section 4):
/// - Keep a window of scenes [live - behind, live + ahead] prepared; before the first trigger, [0, ahead].
/// - Prepare only on lanes that aren't live; spread instances across those lanes.
/// - Every scene entry is fresh: the instance of the scene being left is released, and re-prepared if
///   the scene is still in the window.
/// - A trigger for a ready scene activates it; otherwise it's prepared with priority and activated when ready
///   (the engine holds the last frame meanwhile). The most recent trigger wins.
/// - Triggers for the live scene, unusable scenes (missing preset) and failed scenes are ignored.
class ScenePool
{
public:
    enum class State
    {
        None,
        Preparing,
        Ready,
        Live,
        Failed,   //!< The preset failed to load.
        Unusable, //!< Not preparable (e.g. preset file missing).
    };

    struct Status
    {
        State state{State::None};
        int lane{-1};
    };

    explicit ScenePool(PoolConfig config);

    /// Starts over with a new performance. Returns releases for every existing instance, then the initial prepares.
    auto Reset(int sceneCount, const std::vector<bool>& unusable) -> std::vector<PoolCommand>;

    auto Trigger(int scene) -> std::vector<PoolCommand>;

    /// Called when a Prepare finished. `ok` is false when the preset failed to load.
    auto OnPrepared(InstanceId instance, bool ok) -> std::vector<PoolCommand>;

    /// Called when a lane dropped not-yet-started Prepares (e.g. because it went live). They are re-planned.
    auto OnCancelled(const std::vector<InstanceId>& instances) -> std::vector<PoolCommand>;

    auto LiveScene() const -> int
    {
        return m_live;
    }
    auto PendingScene() const -> int
    {
        return m_pending;
    }
    auto LiveLane() const -> int;
    auto StatusOf(int scene) const -> Status;
    auto SceneCount() const -> int
    {
        return m_sceneCount;
    }

private:
    struct Instance
    {
        int scene{-1};
        int lane{-1};
        bool ready{false};
    };

    auto InstanceFor(int scene) const -> std::optional<InstanceId>;
    auto InWindow(int scene) const -> bool;
    auto PickLane(bool priority) const -> int;
    auto Prepare(int scene, bool priority) -> PoolCommand;
    auto Activate(InstanceId instance) -> std::vector<PoolCommand>;
    void Rebalance(std::vector<PoolCommand>& commands);

    PoolConfig m_config;
    int m_sceneCount{0};
    std::vector<bool> m_unusable;
    std::vector<bool> m_failed;
    std::map<InstanceId, Instance> m_instances;
    InstanceId m_nextId{1};
    int m_live{-1};
    InstanceId m_liveInstance{0};
    int m_pending{-1};
};

} // namespace gv::engine
