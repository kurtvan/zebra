#include "engine/ScenePool.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

using namespace gv::engine;
using Type = PoolCommand::Type;

namespace {

auto Count(const std::vector<PoolCommand>& commands, Type type) -> size_t
{
    return static_cast<size_t>(std::count_if(commands.begin(), commands.end(), [&](const auto& c) { return c.type == type; }));
}

auto Find(const std::vector<PoolCommand>& commands, Type type, int scene) -> const PoolCommand*
{
    for (const auto& command : commands)
    {
        if (command.type == type && command.scene == scene)
            return &command;
    }
    return nullptr;
}

/// Plays the role of the engine: completes every Prepare it is given.
struct Harness
{
    ScenePool pool;
    std::vector<PoolCommand> log;

    explicit Harness(PoolConfig config = {})
        : pool(config)
    {
    }

    void Run(std::vector<PoolCommand> commands)
    {
        // Complete prepares one by one, feeding follow-up commands back in.
        while (!commands.empty())
        {
            const auto command = commands.front();
            commands.erase(commands.begin());
            log.push_back(command);
            if (command.type == Type::Prepare)
            {
                auto more = pool.OnPrepared(command.instance, true);
                commands.insert(commands.end(), more.begin(), more.end());
            }
        }
    }
};

} // namespace

TEST_CASE("before the first trigger, the first scenes are prepared on different lanes")
{
    ScenePool pool({3, 1, 3});
    const auto commands = pool.Reset(10, std::vector<bool>(10, false));
    REQUIRE(Count(commands, Type::Prepare) == 4); // scenes 0..3
    std::set<int> scenes;
    std::set<int> lanes;
    for (const auto& c : commands)
    {
        scenes.insert(c.scene);
        lanes.insert(c.lane);
    }
    CHECK(scenes == std::set<int>{0, 1, 2, 3});
    CHECK(lanes.size() == 3);
    CHECK(pool.LiveScene() == -1);
}

TEST_CASE("triggering a ready scene activates it immediately and shifts the window")
{
    Harness h({3, 1, 3});
    h.Run(h.pool.Reset(10, std::vector<bool>(10, false)));

    auto commands = h.pool.Trigger(0);
    const auto* activate = Find(commands, Type::Activate, 0);
    REQUIRE(activate != nullptr);
    CHECK(h.pool.LiveScene() == 0);
    CHECK(Count(commands, Type::Prepare) == 0); // window [-1..3]: scenes 1-3 are already ready
    CHECK(Count(commands, Type::Release) == 0); // nothing was live before

    commands = h.pool.Trigger(1);
    CHECK(Find(commands, Type::Activate, 1) != nullptr);
    CHECK(Find(commands, Type::Prepare, 4) != nullptr); // window moved to [0..4]
}

TEST_CASE("sequential playback keeps prev 1 / next 3 prepared and never prepares on the live lane")
{
    Harness h({3, 1, 3});
    h.Run(h.pool.Reset(20, std::vector<bool>(20, false)));
    for (int scene = 0; scene < 20; ++scene)
    {
        h.log.clear();
        h.Run(h.pool.Trigger(scene));
        REQUIRE(h.pool.LiveScene() == scene);
        const int liveLane = h.pool.LiveLane();
        for (const auto& c : h.log)
        {
            if (c.type == Type::Prepare)
                CHECK(c.lane != liveLane);
        }
        for (int s = scene - 1; s <= scene + 3; ++s)
        {
            if (s < 0 || s >= 20 || s == scene)
                continue;
            CHECK(h.pool.StatusOf(s).state == ScenePool::State::Ready);
        }
        CHECK(h.pool.StatusOf(scene + 4).state == ScenePool::State::None);
        if (scene >= 2)
            CHECK(h.pool.StatusOf(scene - 2).state == ScenePool::State::None);
    }
}

TEST_CASE("leaving a scene releases its instance and re-prepares it fresh")
{
    Harness h({3, 1, 3});
    h.Run(h.pool.Reset(10, std::vector<bool>(10, false)));
    h.Run(h.pool.Trigger(0));
    h.log.clear();
    h.Run(h.pool.Trigger(1));
    const auto* released = Find(h.log, Type::Release, 0);
    const auto* reprepared = Find(h.log, Type::Prepare, 0);
    REQUIRE(released != nullptr);
    REQUIRE(reprepared != nullptr);
    CHECK(reprepared->instance != released->instance);
}

TEST_CASE("an out-of-order jump prepares with priority and activates when ready")
{
    ScenePool pool({3, 1, 3});
    auto initial = pool.Reset(30, std::vector<bool>(30, false));
    for (const auto& c : initial)
        pool.OnPrepared(c.instance, true);
    pool.Trigger(0);

    auto commands = pool.Trigger(20);
    const auto* prepare = Find(commands, Type::Prepare, 20);
    REQUIRE(prepare != nullptr);
    CHECK(prepare->priority);
    CHECK(pool.LiveScene() == 0); // holds the old scene until ready
    CHECK(pool.PendingScene() == 20);

    auto after = pool.OnPrepared(prepare->instance, true);
    CHECK(Find(after, Type::Activate, 20) != nullptr);
    CHECK(pool.LiveScene() == 20);
    CHECK(pool.PendingScene() == -1);
    CHECK(Find(after, Type::Release, 1) != nullptr); // old window released
}

TEST_CASE("the most recent trigger wins while preparing")
{
    ScenePool pool({3, 1, 3});
    pool.Reset(30, std::vector<bool>(30, false));
    auto a = pool.Trigger(10);
    auto b = pool.Trigger(20);
    const auto* prepare10 = Find(a, Type::Prepare, 10);
    const auto* prepare20 = Find(b, Type::Prepare, 20);
    REQUIRE(prepare10 != nullptr);
    REQUIRE(prepare20 != nullptr);
    CHECK(pool.OnPrepared(prepare10->instance, true).empty()); // no longer wanted, not activated
    CHECK(Find(pool.OnPrepared(prepare20->instance, true), Type::Activate, 20) != nullptr);
}

TEST_CASE("triggers for the live, unusable or failed scenes are ignored")
{
    std::vector<bool> unusable(5, false);
    unusable[2] = true;
    ScenePool pool({3, 1, 3});
    auto initial = pool.Reset(5, unusable);
    CHECK(Find(initial, Type::Prepare, 2) == nullptr);
    CHECK(pool.StatusOf(2).state == ScenePool::State::Unusable);

    // Scene 1 fails to load.
    for (const auto& c : initial)
        pool.OnPrepared(c.instance, c.scene != 1);
    CHECK(pool.StatusOf(1).state == ScenePool::State::Failed);

    pool.Trigger(0);
    CHECK(pool.Trigger(0).empty());
    CHECK(pool.Trigger(1).empty());
    CHECK(pool.Trigger(2).empty());
    CHECK(pool.Trigger(99).empty());
    CHECK(pool.LiveScene() == 0);
}

TEST_CASE("cancelled prepares are planned again on other lanes")
{
    ScenePool pool({3, 1, 3});
    auto initial = pool.Reset(10, std::vector<bool>(10, false));
    std::vector<InstanceId> cancelled;
    for (const auto& c : initial)
    {
        if (c.scene == 3)
            cancelled.push_back(c.instance);
        else
            pool.OnPrepared(c.instance, true);
    }
    auto commands = pool.OnCancelled(cancelled);
    CHECK(Find(commands, Type::Prepare, 3) != nullptr);
}

TEST_CASE("windows are clipped at both ends of the performance")
{
    Harness h({3, 1, 3});
    h.Run(h.pool.Reset(3, std::vector<bool>(3, false)));
    h.Run(h.pool.Trigger(2));
    CHECK(h.pool.LiveScene() == 2);
    CHECK(h.pool.StatusOf(1).state == ScenePool::State::Ready);
    CHECK(h.pool.StatusOf(0).state == ScenePool::State::None);
}

TEST_CASE("reset releases everything from the previous performance")
{
    Harness h({3, 1, 3});
    h.Run(h.pool.Reset(10, std::vector<bool>(10, false)));
    h.Run(h.pool.Trigger(0));
    auto commands = h.pool.Reset(2, std::vector<bool>(2, false));
    CHECK(Count(commands, Type::Release) >= 4);
    CHECK(h.pool.LiveScene() == -1);
    CHECK(Count(commands, Type::Prepare) == 2);
}

TEST_CASE("remap after reordering keeps prepared instances and the live scene")
{
    Harness h({3, 1, 3});
    h.Run(h.pool.Reset(6, std::vector<bool>(6, false)));
    h.Run(h.pool.Trigger(1));
    const auto liveInstance = h.pool.InstancesOf(1);
    REQUIRE(liveInstance.size() == 1);

    // Move scene 1 to the end: old order 0 1 2 3 4 5 -> new order 0 2 3 4 5 1.
    const std::vector<int> oldToNew = {0, 5, 1, 2, 3, 4};
    h.log.clear();
    h.Run(h.pool.Remap(6, oldToNew, std::vector<bool>(6, false), std::vector<bool>(6, false)));
    CHECK(h.pool.LiveScene() == 5);
    CHECK(h.pool.InstancesOf(5).front().instance == liveInstance.front().instance); // still the same instance
    // Window is now [4..8] around scene 5: scene 4 ready, scenes far before released.
    CHECK(h.pool.StatusOf(4).state == ScenePool::State::Ready);
    CHECK(h.pool.StatusOf(0).state == ScenePool::State::None);
}

TEST_CASE("remap releases removed and changed scenes but keeps a removed live scene on screen")
{
    Harness h({3, 1, 3});
    h.Run(h.pool.Reset(5, std::vector<bool>(5, false)));
    h.Run(h.pool.Trigger(0));
    const auto scene2 = h.pool.InstancesOf(2);
    REQUIRE(scene2.size() == 1);

    // Remove the live scene 0; scene 2's preset changed (now index 1).
    const std::vector<int> oldToNew = {-1, 0, 1, 2, 3};
    std::vector<bool> changed(4, false);
    changed[1] = true;
    auto commands = h.pool.Remap(4, oldToNew, std::vector<bool>(4, false), changed);
    CHECK(h.pool.LiveScene() == -1);
    CHECK(h.pool.LiveLane() >= 0); // its instance still renders
    bool releasedStale = false;
    for (const auto& c : commands)
        releasedStale = releasedStale || (c.type == Type::Release && c.instance == scene2.front().instance);
    CHECK(releasedStale);
    CHECK(Find(commands, Type::Prepare, 1) != nullptr); // re-prepared with the new preset
}

TEST_CASE("changing the live scene's preset prepares a replacement and swaps it in")
{
    Harness h({3, 1, 3});
    h.Run(h.pool.Reset(4, std::vector<bool>(4, false)));
    h.Run(h.pool.Trigger(1));
    const auto old = h.pool.InstancesOf(1).front().instance;

    std::vector<bool> changed(4, false);
    changed[1] = true;
    auto commands = h.pool.Remap(4, {0, 1, 2, 3}, std::vector<bool>(4, false), changed);
    const auto* prepare = Find(commands, Type::Prepare, 1);
    REQUIRE(prepare != nullptr);
    CHECK(prepare->priority);
    CHECK(h.pool.LiveScene() == 1); // still showing the old preset meanwhile

    auto after = h.pool.OnPrepared(prepare->instance, true);
    CHECK(Find(after, Type::Activate, 1) != nullptr);
    bool releasedOld = false;
    for (const auto& c : after)
        releasedOld = releasedOld || (c.type == Type::Release && c.instance == old);
    CHECK(releasedOld);
}

TEST_CASE("playlist entries: the next entry is prepared beside the live one and swapped in")
{
    Harness h({3, 1, 3});
    h.Run(h.pool.Reset(5, std::vector<bool>(5, false)));
    h.Run(h.pool.Trigger(1));
    const auto first = h.pool.LiveInstance();
    CHECK(h.pool.LiveItem() == 0);

    auto commands = h.pool.PrepareItem(1, 2);
    const auto* prepare = Find(commands, Type::Prepare, 1);
    REQUIRE(prepare != nullptr);
    CHECK(prepare->item == 2);
    CHECK(prepare->lane != h.pool.LiveLane());
    CHECK(h.pool.ActivateItem(prepare->instance).empty()); // not ready yet

    h.pool.OnPrepared(prepare->instance, true);
    CHECK(h.pool.LiveScene() == 1); // still the first entry until activated
    auto activated = h.pool.ActivateItem(prepare->instance);
    CHECK(Find(activated, Type::Activate, 1) != nullptr);
    bool releasedFirst = false;
    for (const auto& c : activated)
        releasedFirst = releasedFirst || (c.type == Type::Release && c.instance == first);
    CHECK(releasedFirst);
    CHECK(h.pool.LiveItem() == 2);
    CHECK(h.pool.LiveScene() == 1);
}

TEST_CASE("playlist entries are dropped when the scene stops being live, and scenes re-enter at entry 0")
{
    Harness h({3, 1, 3});
    h.Run(h.pool.Reset(5, std::vector<bool>(5, false)));
    h.Run(h.pool.Trigger(1));
    auto commands = h.pool.PrepareItem(1, 1);
    const auto itemInstance = commands.back().instance;
    h.pool.OnPrepared(itemInstance, true);

    h.log.clear();
    h.Run(h.pool.Trigger(2));
    bool releasedItem = false;
    for (const auto& c : h.log)
        releasedItem = releasedItem || (c.type == Type::Release && c.instance == itemInstance);
    CHECK(releasedItem);
    CHECK(h.pool.NextItemInstance() == 0);

    // Scene 1 is back in the window: it's re-prepared at entry 0.
    const auto* reprepare = Find(h.log, Type::Prepare, 1);
    REQUIRE(reprepare != nullptr);
    CHECK(reprepare->item == 0);
    h.Run(h.pool.Trigger(1));
    CHECK(h.pool.LiveItem() == 0);
}

TEST_CASE("an item instance finishing first doesn't hijack a pending live-preset swap")
{
    Harness h({3, 1, 3});
    h.Run(h.pool.Reset(4, std::vector<bool>(4, false)));
    h.Run(h.pool.Trigger(1));
    auto item = h.pool.PrepareItem(1, 1);
    std::vector<bool> changed(4, false);
    changed[1] = true;
    auto remap = h.pool.Remap(4, {0, 1, 2, 3}, std::vector<bool>(4, false), changed);
    // The stale next-entry instance is released and forgotten (the engine plans a new one).
    bool released = false;
    for (const auto& c : remap)
        released = released || (c.type == Type::Release && c.instance == item.back().instance);
    CHECK(released);
    CHECK(h.pool.NextItemInstance() == 0);

    // A new next entry prepared while the replacement is pending doesn't get activated as the replacement.
    auto again = h.pool.PrepareItem(1, 1);
    REQUIRE(again.back().type == Type::Prepare);
    CHECK(Find(h.pool.OnPrepared(again.back().instance, true), Type::Activate, 1) == nullptr);
    CHECK(h.pool.LiveItem() == 0);
}

#include "engine/BeatGrid.hpp"

TEST_CASE("beat and bar cycles start on the grid line, or the next one when well past it")
{
    using gv::engine::CycleStart;
    CHECK(CycleStart(0, 96) == 0);
    CHECK(CycleStart(96 * 3, 96) == 96 * 3);      // exactly on a bar line
    CHECK(CycleStart(96 * 3 + 4, 96) == 96 * 3);  // a moment after it
    CHECK(CycleStart(96 * 3 + 30, 96) == 96 * 4); // mid-bar: the next bar line
    CHECK(CycleStart(24 * 5 + 20, 24) == 24 * 6); // late in a beat: the next beat
}

TEST_CASE("beat and bar playlists only switch on bar lines")
{
    using gv::engine::NextBarLine;
    using gv::engine::NextSwitch;
    CHECK(NextBarLine(0) == 0);
    CHECK(NextBarLine(96 * 2) == 96 * 2);
    CHECK(NextBarLine(96 * 2 + 1) == 96 * 3);
    CHECK(NextSwitch(96 * 3, 96 * 4) == 96 * 7);     // every 4 bars
    CHECK(NextSwitch(96 * 3, 24 * 8) == 96 * 5);     // every 8 beats = 2 bars
    CHECK(NextSwitch(96 * 3, 24 * 6) == 96 * 5);     // every 6 beats: rounded up to 2 bars
    CHECK(NextSwitch(96 * 3, 24) == 96 * 4);         // every beat: every bar
}

TEST_CASE("jumping to an entry of the live playlist scene cuts to it when it's ready")
{
    Harness h({3, 1, 3});
    h.Run(h.pool.Reset(5, std::vector<bool>(5, false)));
    h.Run(h.pool.Trigger(1));
    h.pool.PrepareItem(1, 1); // the cycle's next entry, replaced by the jump

    auto commands = h.pool.TriggerItem(1, 3);
    const auto* prepare = Find(commands, Type::Prepare, 1);
    REQUIRE(prepare != nullptr);
    CHECK(prepare->item == 3);
    CHECK(prepare->priority);
    CHECK(Count(commands, Type::Release) == 1); // the planned next entry
    CHECK(h.pool.JumpPending());
    CHECK(h.pool.PendingScene() == -1); // the live scene keeps playing meanwhile
    CHECK(h.pool.ClearItem().empty());  // playlist housekeeping doesn't cancel it

    auto activated = h.pool.OnPrepared(prepare->instance, true);
    CHECK(Find(activated, Type::Activate, 1) != nullptr);
    CHECK(h.pool.LiveScene() == 1);
    CHECK(h.pool.LiveItem() == 3);
    CHECK_FALSE(h.pool.JumpPending());
    CHECK(h.pool.NextItemInstance() == 0);
}

TEST_CASE("jumping to an entry of another scene enters that scene there")
{
    Harness h({3, 1, 3});
    h.Run(h.pool.Reset(6, std::vector<bool>(6, false)));
    h.Run(h.pool.Trigger(0));

    auto commands = h.pool.TriggerItem(4, 2);
    CHECK(h.pool.PendingScene() == 4);
    const auto* prepare = Find(commands, Type::Prepare, 4);
    REQUIRE(prepare != nullptr);
    CHECK(prepare->item == 2);

    // Window housekeeping (e.g. another cancel) doesn't drop it.
    h.log.clear();
    h.Run(h.pool.OnCancelled({}));
    for (const auto& c : h.log)
        CHECK_FALSE((c.type == Type::Release && c.instance == prepare->instance));

    h.log.clear();
    h.Run(h.pool.OnPrepared(prepare->instance, true));
    CHECK(h.pool.LiveScene() == 4);
    CHECK(h.pool.LiveItem() == 2);

    // Entry 0 of a scene that isn't live is an ordinary trigger.
    h.Run(h.pool.TriggerItem(5, 0));
    CHECK(h.pool.LiveScene() == 5);
    CHECK(h.pool.LiveItem() == 0);
}

TEST_CASE("a trigger cancels a pending jump, and a cancelled jump is prepared again")
{
    Harness h({3, 1, 3});
    h.Run(h.pool.Reset(8, std::vector<bool>(8, false)));
    h.Run(h.pool.Trigger(0));

    auto commands = h.pool.TriggerItem(6, 1);
    const auto jump = Find(commands, Type::Prepare, 6)->instance;

    // Cancelled by its lane before it started: planned again, still a jump.
    auto again = h.pool.OnCancelled({jump});
    const auto* reprepare = Find(again, Type::Prepare, 6);
    REQUIRE(reprepare != nullptr);
    CHECK(reprepare->item == 1);
    CHECK(h.pool.JumpPending());

    h.log.clear();
    h.Run(h.pool.Trigger(1));
    CHECK(h.pool.LiveScene() == 1);
    CHECK_FALSE(h.pool.JumpPending());
    bool releasedJump = false;
    for (const auto& c : h.log)
        releasedJump = releasedJump || (c.type == Type::Release && c.instance == reprepare->instance);
    CHECK(releasedJump);
    CHECK(h.pool.OnPrepared(reprepare->instance, true).empty()); // finishing late does nothing
    CHECK(h.pool.LiveScene() == 1);
}

namespace {

auto Released(const std::vector<PoolCommand>& commands, InstanceId instance) -> bool
{
    return std::any_of(commands.begin(), commands.end(), [&](const auto& c) { return c.type == Type::Release && c.instance == instance; });
}

/// Scene 1 of 5 is live on preset entry `item` (a switcher input double-clicked, or its clock moved on).
void GoLiveOnEntry(Harness& h, int item)
{
    h.Run(h.pool.Reset(5, std::vector<bool>(5, false)));
    h.Run(h.pool.Trigger(1));
    h.Run(h.pool.TriggerItem(1, item));
    REQUIRE(h.pool.LiveItem() == item);
}

} // namespace

TEST_CASE("changing the preset of the live entry reloads that entry and cuts to it when ready")
{
    Harness h({3, 1, 3});
    GoLiveOnEntry(h, 1);
    const auto old = h.pool.LiveInstance();

    auto commands = h.pool.ReloadItems({false, true, false});
    const auto* prepare = Find(commands, Type::Prepare, 1);
    REQUIRE(prepare != nullptr);
    CHECK(prepare->item == 1);
    CHECK(prepare->priority);
    CHECK(prepare->lane != h.pool.LiveLane());
    CHECK(h.pool.LiveInstance() == old); // the old preset holds meanwhile
    CHECK(h.pool.PendingScene() == -1);

    auto after = h.pool.OnPrepared(prepare->instance, true);
    CHECK(Find(after, Type::Activate, 1) != nullptr);
    CHECK(Released(after, old));
    CHECK(h.pool.LiveScene() == 1);
    CHECK(h.pool.LiveItem() == 1); // same switcher input, not back to entry 0
    CHECK_FALSE(h.pool.JumpPending());
}

TEST_CASE("changing a prepared entry that isn't live prepares it again and leaves the live one alone")
{
    Harness h({3, 1, 3});
    GoLiveOnEntry(h, 1);
    const auto live = h.pool.LiveInstance();
    auto next = h.pool.PrepareItem(1, 2);
    const auto stale = next.back().instance;
    h.pool.OnPrepared(stale, true);

    auto commands = h.pool.ReloadItems({false, false, true});
    CHECK(Released(commands, stale));
    CHECK_FALSE(Released(commands, live));
    CHECK(Find(commands, Type::Activate, 1) == nullptr);
    const auto* prepare = Find(commands, Type::Prepare, 1);
    REQUIRE(prepare != nullptr);
    CHECK(prepare->item == 2);
    CHECK_FALSE(prepare->priority); // in the background
    CHECK(h.pool.NextItemInstance() == prepare->instance);
    CHECK_FALSE(h.pool.JumpPending());

    // Ready again: the switcher can still cut to it straight away.
    CHECK(h.pool.OnPrepared(prepare->instance, true).empty());
    CHECK(h.pool.LiveInstance() == live);
    CHECK(Find(h.pool.ActivateItem(prepare->instance), Type::Activate, 1) != nullptr);
    CHECK(h.pool.LiveItem() == 2);
}

TEST_CASE("changing an entry nothing has prepared does nothing")
{
    Harness h({3, 1, 3});
    GoLiveOnEntry(h, 1);
    CHECK(h.pool.ReloadItems({false, false, true}).empty());
    CHECK(h.pool.LiveItem() == 1);
}

TEST_CASE("changing entry 0 while it's live is swapped in once, by Remap")
{
    Harness h({3, 1, 3});
    h.Run(h.pool.Reset(4, std::vector<bool>(4, false)));
    h.Run(h.pool.Trigger(1));

    std::vector<bool> changed(4, false);
    changed[1] = true;
    auto commands = h.pool.Remap(4, {0, 1, 2, 3}, std::vector<bool>(4, false), changed);
    REQUIRE(Find(commands, Type::Prepare, 1) != nullptr);
    CHECK(h.pool.ReloadItems({true, false}).empty()); // not a second replacement
    h.Run(commands);
    CHECK(h.pool.LiveScene() == 1);
    CHECK(h.pool.LiveItem() == 0);
}

TEST_CASE("changing entry 0 while another entry is live keeps that entry on screen")
{
    Harness h({3, 1, 3});
    GoLiveOnEntry(h, 2);
    const auto live = h.pool.LiveInstance();

    std::vector<bool> changed(5, false);
    changed[1] = true;
    h.log.clear();
    h.Run(h.pool.Remap(5, {0, 1, 2, 3, 4}, std::vector<bool>(5, false), changed));
    h.Run(h.pool.ReloadItems({true, false, false}));
    CHECK_FALSE(Released(h.log, live));
    CHECK(Find(h.log, Type::Activate, 1) == nullptr);
    CHECK(h.pool.LiveInstance() == live);
    CHECK(h.pool.LiveItem() == 2);
}

TEST_CASE("the live entry was removed from the switcher: the scene cuts to entry 0")
{
    Harness h({3, 1, 3});
    GoLiveOnEntry(h, 2);
    h.Run(h.pool.ReloadItems({false, false})); // only two entries left
    CHECK(h.pool.LiveScene() == 1);
    CHECK(h.pool.LiveItem() == 0);
}
