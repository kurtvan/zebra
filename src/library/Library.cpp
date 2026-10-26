#include "library/Library.hpp"

#include "common/log.hpp"
#include "library/PresetParsing.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>

namespace gv::library {

using nlohmann::json;

namespace {

constexpr const char* kLogCategory = "library";
constexpr int kIndexFormat = 2; //!< Bump when parsing changes, so saved entries are re-parsed (2: author heuristic).

auto ModifiedTime(const std::filesystem::path& path) -> int64_t
{
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    return ec ? 0 : static_cast<int64_t>(time.time_since_epoch().count());
}

auto WriteAtomically(const std::filesystem::path& file, const std::string& text) -> std::string
{
    const auto temp = std::filesystem::path(file.string() + ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            return "could not write " + temp.string();
        out << text;
    }
    std::error_code ec;
    std::filesystem::rename(temp, file, ec);
    return ec ? "could not replace " + file.string() + ": " + ec.message() : std::string();
}

} // namespace

auto Library::Find(const std::string& id) const -> const PresetInfo*
{
    auto it = m_byId.find(id);
    return it == m_byId.end() ? nullptr : &m_presets[it->second];
}

auto Library::IdForPath(const std::filesystem::path& path) const -> std::string
{
    if (m_root.empty())
        return {};
    const auto relative = path.lexically_normal().lexically_relative(m_root.lexically_normal());
    if (relative.empty() || *relative.begin() == "..")
        return {};
    return relative.generic_string();
}

auto Library::PathForId(const std::string& id) const -> std::filesystem::path
{
    return m_root / std::filesystem::path(id);
}

void Library::Finish()
{
    std::sort(m_presets.begin(), m_presets.end(), [](const PresetInfo& a, const PresetInfo& b) { return a.id < b.id; });
    m_byId.clear();
    for (size_t i = 0; i < m_presets.size(); ++i)
    {
        auto& preset = m_presets[i];
        preset.searchText = ToLower(preset.name + "\n" + preset.author + "\n" + preset.category + "\n" + preset.subcategory);
        m_byId[preset.id] = i;
    }
}

auto Library::Load(const std::filesystem::path& file) -> bool
{
    std::ifstream in(file);
    if (!in)
        return false;
    try
    {
        const json root = json::parse(in);
        if (root.value("format", 0) != kIndexFormat)
            return false;
        Library loaded;
        loaded.m_root = root.value("root", std::string());
        for (const auto& item : root.at("presets"))
        {
            PresetInfo preset;
            preset.id = item.value("id", std::string());
            preset.category = item.value("category", std::string());
            preset.subcategory = item.value("subcategory", std::string());
            preset.author = item.value("author", std::string());
            preset.name = item.value("name", std::string());
            preset.modified = item.value("modified", int64_t{0});
            preset.size = item.value("size", uint64_t{0});
            preset.textures = item.value("textures", std::vector<std::string>());
            preset.broken = item.value("broken", false);
            preset.error = item.value("error", std::string());
            preset.frameMs = item.value("frameMs", 0.0);
            if (!preset.id.empty())
                loaded.m_presets.push_back(std::move(preset));
        }
        loaded.Finish();
        *this = std::move(loaded);
        return true;
    }
    catch (const json::exception& error)
    {
        GV_LOG_WARN(kLogCategory, "ignoring unreadable library index %s: %s", file.c_str(), error.what());
        return false;
    }
}

auto Library::Save(const std::filesystem::path& file) const -> std::string
{
    json root;
    root["format"] = kIndexFormat;
    root["root"] = m_root.string();
    root["presets"] = json::array();
    for (const auto& preset : m_presets)
    {
        json item = {{"id", preset.id}, {"category", preset.category}, {"subcategory", preset.subcategory}, {"author", preset.author},
                     {"name", preset.name}, {"modified", preset.modified}, {"size", preset.size}};
        if (!preset.textures.empty())
            item["textures"] = preset.textures;
        if (preset.broken)
            item["broken"] = true;
        if (!preset.error.empty())
            item["error"] = preset.error;
        if (preset.frameMs > 0.0)
            item["frameMs"] = preset.frameMs;
        root["presets"].push_back(std::move(item));
    }
    return WriteAtomically(file, root.dump());
}

auto Library::Scan(const std::filesystem::path& root, const Library& previous, std::atomic<int>* progress) -> Library
{
    const auto start = std::chrono::steady_clock::now();
    Library library;
    library.m_root = root;
    const bool sameRoot = previous.m_root == root;
    int reused = 0;

    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
    {
        if (!it->is_regular_file(ec) || ToLower(it->path().extension().string()) != ".milk")
            continue;
        const auto id = it->path().lexically_relative(root).generic_string();
        const auto modified = ModifiedTime(it->path());
        const auto size = static_cast<uint64_t>(it->file_size(ec));

        if (const PresetInfo* known = sameRoot ? previous.Find(id) : nullptr; known != nullptr && known->modified == modified && known->size == size)
        {
            library.m_presets.push_back(*known);
            reused++;
        }
        else
        {
            PresetInfo preset;
            preset.id = id;
            const auto parsed = ParsePresetPath(id);
            preset.category = parsed.category;
            preset.subcategory = parsed.subcategory;
            preset.author = parsed.author;
            preset.name = parsed.name;
            preset.modified = modified;
            preset.size = size;
            std::ifstream in(it->path(), std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            for (const auto& texture : ExtractTextureNames(text.str()))
                preset.textures.push_back(texture);
            // A changed file may load fine now: forget what was learned about the old one.
            library.m_presets.push_back(std::move(preset));
        }
        if (progress != nullptr)
            progress->fetch_add(1);
    }
    if (ec)
        GV_LOG_WARN(kLogCategory, "scanning %s stopped early: %s", root.c_str(), ec.message().c_str());

    library.Finish();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    GV_LOG_INFO(kLogCategory, "indexed %zu presets in %s (%d unchanged) in %.0f ms", library.m_presets.size(), root.c_str(), reused, ms);
    return library;
}

auto Library::Record(const std::filesystem::path& path, double frameMs, const std::string& error) -> bool
{
    auto it = m_byId.find(IdForPath(path));
    if (it == m_byId.end())
        return false;
    auto& preset = m_presets[it->second];
    const bool broken = !error.empty();
    if (preset.broken == broken && preset.error == error && std::abs(preset.frameMs - frameMs) < 0.5)
        return false;
    preset.broken = broken;
    preset.error = error;
    if (!broken)
        preset.frameMs = frameMs;
    return true;
}

auto Library::Categories() const -> std::vector<Category>
{
    std::map<std::string, Category> byName;
    for (const auto& preset : m_presets)
    {
        auto& category = byName[preset.category];
        category.name = preset.category;
        category.count++;
        if (!preset.subcategory.empty())
            category.subcategories[preset.subcategory]++;
    }
    std::vector<Category> categories;
    for (auto& [name, category] : byName)
        categories.push_back(std::move(category));
    return categories;
}

auto Search(const Library& library, const Query& query, const std::set<std::string>& favourites) -> std::vector<int>
{
    // Split the text into lowercase words; every word must match.
    std::vector<std::string> words;
    std::istringstream stream(ToLower(query.text));
    for (std::string word; stream >> word;)
        words.push_back(word);

    std::vector<int> result;
    const auto& presets = library.Presets();
    for (int i = 0; i < static_cast<int>(presets.size()); ++i)
    {
        const auto& preset = presets[static_cast<size_t>(i)];
        if (!query.category.empty() && preset.category != query.category)
            continue;
        if (!query.subcategory.empty() && preset.subcategory != query.subcategory)
            continue;
        if (query.favouritesOnly && favourites.count(preset.id) == 0)
            continue;
        if (query.hideBroken && preset.broken)
            continue;
        bool matches = true;
        for (const auto& word : words)
        {
            if (preset.searchText.find(word) == std::string::npos)
            {
                matches = false;
                break;
            }
        }
        if (matches)
            result.push_back(i);
    }
    return result;
}

auto AvailableTextures(const std::vector<std::filesystem::path>& folders) -> std::set<std::string>
{
    std::set<std::string> names;
    for (const auto& folder : folders)
    {
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        {
            const auto extension = ToLower(it->path().extension().string());
            if (extension == ".jpg" || extension == ".jpeg" || extension == ".png" || extension == ".bmp" || extension == ".tga" || extension == ".dds")
                names.insert(ToLower(it->path().stem().string()));
        }
    }
    return names;
}

auto MissingTextures(const PresetInfo& preset, const std::set<std::string>& available) -> std::vector<std::string>
{
    std::vector<std::string> missing;
    for (const auto& texture : preset.textures)
    {
        if (available.count(texture) == 0)
            missing.push_back(texture);
    }
    return missing;
}

auto Favourites::Load(const std::filesystem::path& file) -> bool
{
    std::ifstream in(file);
    if (!in)
        return false;
    try
    {
        const json root = json::parse(in);
        m_ids.clear();
        for (const auto& id : root.value("favourites", std::vector<std::string>()))
            m_ids.insert(id);
        return true;
    }
    catch (const json::exception&)
    {
        return false;
    }
}

auto Favourites::Save(const std::filesystem::path& file) const -> std::string
{
    json root;
    root["favourites"] = std::vector<std::string>(m_ids.begin(), m_ids.end());
    return WriteAtomically(file, root.dump(2) + "\n");
}

void Favourites::Toggle(const std::string& id)
{
    if (!m_ids.erase(id))
        m_ids.insert(id);
}

Indexer::~Indexer()
{
    if (m_thread.joinable())
        m_thread.join();
}

void Indexer::Start(const std::filesystem::path& root, const Library& previous)
{
    if (m_running.load())
        return;
    if (m_thread.joinable())
        m_thread.join();
    m_running = true;
    m_progress = 0;
    m_thread = std::thread([this, root, previous] {
        auto library = std::make_unique<Library>(Library::Scan(root, previous, &m_progress));
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_result = std::move(library);
        }
        m_running = false;
    });
}

auto Indexer::TakeResult() -> std::unique_ptr<Library>
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::move(m_result);
}

} // namespace gv::library
