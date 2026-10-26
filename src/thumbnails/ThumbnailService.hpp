#pragma once

#include <SDL.h>

#include <OpenGL/gl3.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace gv::thumbnails {

/// A preset the browser wants a preview of.
struct Request
{
    std::string id;              //!< Library id.
    std::filesystem::path path;  //!< Absolute preset file.
    std::string version;         //!< Changes when the file changes (e.g. mtime + size); part of the cache key.
};

enum class TileState
{
    Loading, //!< No image yet.
    Cached,  //!< Showing the saved image while the live preview starts.
    Live,    //!< Rendered by a running instance (updates about once a second).
    Dark,    //!< Live, but nearly black after warming up (some presets start dark).
    Broken,  //!< The preset failed to load.
};

struct Tile
{
    GLuint texture{0}; //!< 0 while Loading or Broken.
    TileState state{TileState::Loading};
};

/// Something learned about a preset while previewing it.
struct Finding
{
    std::filesystem::path path;
    std::string error; //!< Load error (the preset is broken).
};

/// Live mini-previews of presets for the browser.
///
/// Runs on its own thread and GL context (sharing textures with the editor): tiny projectM instances
/// (kWidth x kHeight) for the tiles on screen, fed a synthetic audio signal (previews don't react to the live
/// input), each warmed up once and then re-rendered about once a second. The last frame of each preset is
/// saved as a PNG so revisited tiles show instantly.
class ThumbnailService
{
public:
    static constexpr int kWidth = 160;
    static constexpr int kHeight = 90;

    ThumbnailService(); // out of line: Slot is only defined in the .cpp
    ~ThumbnailService();

    ThumbnailService(const ThumbnailService&) = delete;
    auto operator=(const ThumbnailService&) -> ThumbnailService& = delete;

    /// Starts the thread with a context sharing with the current GL context. Call on the main thread.
    auto Start(const std::filesystem::path& cacheDir, std::vector<std::string> texturePaths) -> bool;
    void Stop();

    void SetTexturePaths(std::vector<std::string> texturePaths);

    /// The tiles to show, most important first, plus ones to prepare quietly (e.g. just off screen).
    /// Everything else is dropped. Cheap to call every frame.
    void SetWanted(std::vector<Request> visible, std::vector<Request> prefetch);

    /// Pauses all work (e.g. during a fullscreen performance).
    void SetPaused(bool paused);

    auto Get(const std::string& id) const -> std::optional<Tile>;

    /// Fence after the newest renders; the editor waits on it (GPU-side) before sampling tiles.
    auto LatestFence() const -> GLsync;

    auto TakeFindings() -> std::vector<Finding>;

    /// Live instances right now (for the status panel).
    auto LiveInstances() const -> int
    {
        return m_liveInstances.load();
    }

private:
    struct Slot;

    void Run();
    void Work();
    auto CreateSlot(const Request& request, bool wantLive) -> Slot*;
    void StartInstance(Slot& slot);
    void WarmUp(Slot& slot);
    void RenderFrame(Slot& slot);
    void DestroySlot(const std::string& id);
    void Publish(const std::string& id, const Slot& slot);
    auto CacheFile(const Request& request) const -> std::filesystem::path;
    void TrimCache();

    SDL_Window* m_window{nullptr};
    SDL_GLContext m_context{nullptr};
    std::thread m_thread;
    std::filesystem::path m_cacheDir;

    // Shared with the main thread.
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_quit{false};
    bool m_paused{false};
    uint64_t m_wantedVersion{0};
    std::vector<Request> m_visible;
    std::vector<Request> m_prefetch;
    std::vector<std::string> m_texturePaths;
    std::map<std::string, Tile> m_published;
    GLsync m_latestFence{nullptr};
    std::vector<Finding> m_findings;
    std::atomic<int> m_liveInstances{0};

    // Worker thread only.
    std::map<std::string, std::unique_ptr<Slot>> m_slots;
    std::vector<std::pair<std::chrono::steady_clock::time_point, GLuint>> m_retiredTextures;
    std::vector<GLsync> m_oldFences;
    int m_audioFrame{0};
};

} // namespace gv::thumbnails
