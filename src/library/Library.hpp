#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace gv::library {

/// One preset in the library index.
struct PresetInfo
{
    std::string id; //!< Path relative to the library root, with '/' separators. Unique.
    std::string category;
    std::string subcategory;
    std::string author;
    std::string name;
    int64_t modified{0}; //!< File modification time (for incremental re-indexing).
    uint64_t size{0};
    std::vector<std::string> textures; //!< Image textures the preset samples (see ExtractTextureNames).

    // Learned while playing (engine measurements), kept across launches.
    bool broken{false};
    std::string error;
    double frameMs{0.0}; //!< Measured render time per frame; 0 if never measured.

    std::string searchText; //!< Lowercase name + author + tags, for search. Not saved.
};

/// The indexed preset library: load/save, scanning, search.
class Library
{
public:
    auto Root() const -> const std::filesystem::path&
    {
        return m_root;
    }
    auto Presets() const -> const std::vector<PresetInfo>&
    {
        return m_presets;
    }
    auto Find(const std::string& id) const -> const PresetInfo*;

    /// Id of an absolute preset path inside the root, or empty.
    auto IdForPath(const std::filesystem::path& path) const -> std::string;
    auto PathForId(const std::string& id) const -> std::filesystem::path;

    /// Reads a saved index. Returns false (and leaves the library empty) if it's missing or unreadable.
    auto Load(const std::filesystem::path& file) -> bool;
    auto Save(const std::filesystem::path& file) const -> std::string;

    /// Scans `root` (blocking), reusing entries of `previous` whose file is unchanged. `progress` counts files.
    static auto Scan(const std::filesystem::path& root, const Library& previous, std::atomic<int>* progress = nullptr) -> Library;

    /// Records an engine measurement for the preset at `path`. Returns true if anything changed.
    auto Record(const std::filesystem::path& path, double frameMs, const std::string& error) -> bool;

    // --- Categories --------------------------------------------------------------------------

    struct Category
    {
        std::string name;
        int count{0};
        std::map<std::string, int> subcategories; //!< name -> count
    };
    auto Categories() const -> std::vector<Category>;

private:
    void Finish(); //!< Sorts, builds search text and the id map.

    std::filesystem::path m_root;
    std::vector<PresetInfo> m_presets;
    std::map<std::string, size_t> m_byId;
};

/// Which presets to show.
struct Query
{
    std::string text;        //!< Words that must all appear in name, author or tags (case-insensitive).
    std::string category;    //!< Empty: all.
    std::string subcategory; //!< Empty: all in the category.
    bool favouritesOnly{false};
    bool hideBroken{false};

    auto operator==(const Query&) const -> bool = default;
};

/// Indices into library.Presets() matching `query`, in library order.
auto Search(const Library& library, const Query& query, const std::set<std::string>& favourites) -> std::vector<int>;

/// Lowercase file stems of the images in `folders` (recursively): the textures projectM can find.
auto AvailableTextures(const std::vector<std::filesystem::path>& folders) -> std::set<std::string>;

/// The textures `preset` needs that aren't in `available`.
auto MissingTextures(const PresetInfo& preset, const std::set<std::string>& available) -> std::vector<std::string>;

/// Favourite presets, by id. Stored in favourites.json.
class Favourites
{
public:
    auto Load(const std::filesystem::path& file) -> bool;
    auto Save(const std::filesystem::path& file) const -> std::string;
    auto Contains(const std::string& id) const -> bool
    {
        return m_ids.count(id) > 0;
    }
    void Toggle(const std::string& id);
    auto Ids() const -> const std::set<std::string>&
    {
        return m_ids;
    }

private:
    std::set<std::string> m_ids;
};

/// Scans the library on a background thread.
class Indexer
{
public:
    ~Indexer();

    /// Starts scanning `root`, reusing what `previous` already knows. Ignored while a scan is running.
    void Start(const std::filesystem::path& root, const Library& previous);
    auto Running() const -> bool
    {
        return m_running.load();
    }
    auto Progress() const -> int
    {
        return m_progress.load();
    }
    /// The finished scan, once (then empty until the next Start).
    auto TakeResult() -> std::unique_ptr<Library>;

private:
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<int> m_progress{0};
    std::mutex m_mutex;
    std::unique_ptr<Library> m_result;
};

} // namespace gv::library
