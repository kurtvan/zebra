#include "library/PresetStepping.hpp"

#include <vector>

namespace gv::library {

namespace {

auto FolderOfId(const std::string& id) -> std::string
{
    const auto slash = id.rfind('/');
    return slash == std::string::npos ? std::string() : id.substr(0, slash);
}

/// Steps `step` places from `current` (an id; empty or absent: before the first) through `ids`, wrapping.
auto StepThrough(const Library& library, const std::vector<const std::string*>& ids, const std::string& current, int step)
    -> std::optional<std::filesystem::path>
{
    const int count = static_cast<int>(ids.size());
    int at = -1;
    for (int i = 0; i < count; ++i)
    {
        if (*ids[static_cast<size_t>(i)] == current)
            at = i;
    }
    if (step == 0 || count == 0)
        return std::nullopt;
    const int from = at >= 0 ? at : step > 0 ? -1 : count;
    const int to = ((from + step) % count + count) % count;
    if (to == at)
        return std::nullopt;
    return library.PathForId(*ids[static_cast<size_t>(to)]);
}

} // namespace

auto FolderOf(const Library& library, const std::filesystem::path& preset) -> std::optional<std::string>
{
    const std::string id = library.IdForPath(preset);
    if (id.empty() || library.Find(id) == nullptr)
        return std::nullopt;
    return FolderOfId(id);
}

auto StepPreset(const Library& library, const std::string& folder, const std::filesystem::path& current, int step)
    -> std::optional<std::filesystem::path>
{
    const std::string currentId = current.empty() ? std::string() : library.IdForPath(current);
    std::vector<const std::string*> ids;
    for (const auto& preset : library.Presets())
    {
        // The current preset keeps its place even if it's broken, so stepping moves on from it.
        if (FolderOfId(preset.id) == folder && (!preset.broken || preset.id == currentId))
            ids.push_back(&preset.id);
    }
    return StepThrough(library, ids, currentId, step);
}

auto StepPreset(const Library& library, const std::filesystem::path& current, int step) -> std::optional<std::filesystem::path>
{
    if (!current.empty())
    {
        const auto folder = FolderOf(library, current);
        return folder ? StepPreset(library, *folder, current, step) : std::nullopt;
    }
    std::vector<const std::string*> ids;
    for (const auto& preset : library.Presets())
    {
        if (!preset.broken)
            ids.push_back(&preset.id);
    }
    return StepThrough(library, ids, {}, step);
}

auto StepPresetUnder(const Library& library, const std::string& folder, const std::filesystem::path& current, int step)
    -> std::optional<std::filesystem::path>
{
    const std::string currentId = current.empty() ? std::string() : library.IdForPath(current);
    const std::string prefix = folder.empty() ? std::string() : folder + "/";
    std::vector<const std::string*> ids;
    for (const auto& preset : library.Presets())
    {
        if (preset.id.compare(0, prefix.size(), prefix) == 0 && (!preset.broken || preset.id == currentId))
            ids.push_back(&preset.id);
    }
    return StepThrough(library, ids, currentId, step);
}

auto FolderName(const std::string& category, const std::string& subcategory) -> std::string
{
    return subcategory.empty() ? category : category + "/" + subcategory;
}

auto FolderQuery(const std::string& folder) -> Query
{
    Query query;
    const auto slash = folder.find('/');
    query.category = folder.substr(0, slash);
    if (slash != std::string::npos)
        query.subcategory = folder.substr(slash + 1, folder.find('/', slash + 1) - slash - 1);
    return query;
}

auto FolderRows(const Library& library, const std::string& open) -> std::vector<FolderRow>
{
    std::vector<FolderRow> rows{{{}, {}, 0, static_cast<int>(library.Presets().size())}};
    for (const auto& category : library.Categories())
    {
        if (category.name.empty())
            continue; // presets at the top of the library: "All presets" has them
        rows.push_back({category.name, {}, 1, category.count});
        if (category.name == open)
        {
            for (const auto& [name, count] : category.subcategories)
                rows.push_back({category.name, name, 2, count});
        }
    }
    return rows;
}

auto RandomPreset(const Library& library, const std::filesystem::path& current, std::mt19937& random) -> std::optional<std::filesystem::path>
{
    const std::string currentId = current.empty() ? std::string() : library.IdForPath(current);
    std::vector<const std::string*> ids;
    for (const auto& preset : library.Presets())
    {
        if (!preset.broken && preset.id != currentId)
            ids.push_back(&preset.id);
    }
    if (ids.empty())
        return std::nullopt;
    std::uniform_int_distribution<size_t> pick(0, ids.size() - 1);
    return library.PathForId(*ids[pick(random)]);
}

} // namespace gv::library
