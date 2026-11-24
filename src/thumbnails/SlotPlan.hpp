#pragma once

// Which preview slots ThumbnailService keeps, stops and makes each pass. Pure, so it can be tested without GL.

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace gv::thumbnails {

/// What to do with the slots this pass.
struct SlotPlan
{
    std::vector<std::string> destroy; //!< No longer wanted at all: free the slot and its picture.
    std::vector<std::string> stop;    //!< Kept but paused: drop the running instance, keep the picture.
    std::vector<std::string> create;  //!< Wanted but without a slot yet, live ones first.
};

/// `slots`: each existing slot's id, and whether it has a running instance. `live`: the tiles to keep
/// rendering (visible, then prefetch). `kept`: tiles to keep showing without rendering (e.g. too small to
/// see); a tile in both is live.
inline auto PlanSlots(const std::map<std::string, bool>& slots, const std::vector<std::string>& live, const std::vector<std::string>& kept) -> SlotPlan
{
    auto contains = [](const std::vector<std::string>& ids, const std::string& id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };
    SlotPlan plan;
    for (const auto& [id, running] : slots)
    {
        if (contains(live, id))
            continue;
        if (!contains(kept, id))
            plan.destroy.push_back(id);
        else if (running)
            plan.stop.push_back(id);
    }
    for (const auto* list : {&live, &kept})
    {
        for (const auto& id : *list)
        {
            if (slots.count(id) == 0 && !contains(plan.create, id))
                plan.create.push_back(id);
        }
    }
    return plan;
}

} // namespace gv::thumbnails
