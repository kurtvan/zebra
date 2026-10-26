#include "thumbnails/ThumbnailService.hpp"

#include "common/log.hpp"

#include <projectM-4/projectM.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace gv::thumbnails {

namespace {

constexpr const char* kLogCategory = "thumbnails";
constexpr int kMaxInstances = 40;                 //!< About two screens of tiles.
constexpr auto kRefreshInterval = std::chrono::milliseconds(1000);
constexpr int kWarmUpFrames = 45;                 //!< Most presets start dark; render a bit before the first image.
constexpr auto kWarmUpBudget = std::chrono::milliseconds(500);
constexpr auto kTextureGrace = std::chrono::seconds(2); //!< The editor may still draw a dropped tile's texture this long.
constexpr double kDarkLuma = 0.02;
constexpr uintmax_t kCacheLimitBytes = 500ull * 1024 * 1024;
constexpr uintmax_t kCacheTrimTargetBytes = 400ull * 1024 * 1024;

using Clock = std::chrono::steady_clock;

struct LoadFailure
{
    bool failed{false};
    std::string message;
};

void OnPresetFailed(const char*, const char* message, void* userData)
{
    auto* failure = static_cast<LoadFailure*>(userData);
    failure->failed = true;
    failure->message = message != nullptr ? message : "unknown error";
}

/// The same beating two-tone signal for every preview, so audio-driven presets move.
void FeedSyntheticAudio(projectm_handle instance, int frame)
{
    static thread_local std::vector<float> samples(735 * 2);
    for (size_t i = 0; i < samples.size() / 2; ++i)
    {
        const double t = (frame * 735.0 + static_cast<double>(i)) / 44100.0;
        const double envelope = 0.5 + 0.5 * std::sin(2.0 * M_PI * 2.0 * t);
        const auto value = static_cast<float>(envelope * (0.6 * std::sin(2.0 * M_PI * 110.0 * t) + 0.3 * std::sin(2.0 * M_PI * 1760.0 * t)));
        samples[i * 2] = value;
        samples[i * 2 + 1] = value;
    }
    projectm_pcm_add_float(instance, samples.data(), static_cast<unsigned int>(samples.size() / 2), PROJECTM_STEREO);
}

/// Flips rows in place (GL is bottom-up, PNG top-down).
void FlipRows(std::vector<unsigned char>& pixels, int width, int height)
{
    const size_t row = static_cast<size_t>(width) * 4;
    std::vector<unsigned char> temp(row);
    for (int y = 0; y < height / 2; ++y)
    {
        unsigned char* a = &pixels[static_cast<size_t>(y) * row];
        unsigned char* b = &pixels[static_cast<size_t>(height - 1 - y) * row];
        std::copy(a, a + row, temp.begin());
        std::copy(b, b + row, a);
        std::copy(temp.begin(), temp.end(), b);
    }
}

} // namespace

struct ThumbnailService::Slot
{
    Request request;
    projectm_handle instance{nullptr};
    GLuint texture{0};
    GLuint fbo{0};
    TileState state{TileState::Loading};
    bool warmed{false};
    Clock::time_point lastRender{};
};

ThumbnailService::ThumbnailService() = default;

ThumbnailService::~ThumbnailService()
{
    Stop();
}

auto ThumbnailService::Start(const std::filesystem::path& cacheDir, std::vector<std::string> texturePaths) -> bool
{
    SDL_Window* shareWindow = SDL_GL_GetCurrentWindow();
    SDL_GLContext shareContext = SDL_GL_GetCurrentContext();
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    m_window = SDL_CreateWindow("thumbnails", 0, 0, 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    m_context = m_window != nullptr ? SDL_GL_CreateContext(m_window) : nullptr;
    SDL_GL_MakeCurrent(shareWindow, shareContext);
    if (m_context == nullptr)
    {
        GV_LOG_ERROR(kLogCategory, "could not create the thumbnail GL context: %s", SDL_GetError());
        if (m_window != nullptr)
            SDL_DestroyWindow(m_window);
        m_window = nullptr;
        return false;
    }

    m_cacheDir = cacheDir;
    std::error_code ec;
    std::filesystem::create_directories(m_cacheDir, ec);
    m_texturePaths = std::move(texturePaths);
    m_thread = std::thread([this] { Run(); });
    return true;
}

void ThumbnailService::Stop()
{
    if (!m_thread.joinable())
        return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_quit = true;
    }
    m_wake.notify_one();
    m_thread.join();
    SDL_GL_DeleteContext(m_context);
    SDL_DestroyWindow(m_window);
    m_context = nullptr;
    m_window = nullptr;
}

void ThumbnailService::SetTexturePaths(std::vector<std::string> texturePaths)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_texturePaths = std::move(texturePaths);
}

void ThumbnailService::SetWanted(std::vector<Request> visible, std::vector<Request> prefetch)
{
    auto sameIds = [](const std::vector<Request>& a, const std::vector<Request>& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const Request& x, const Request& y) { return x.id == y.id; });
    };
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (sameIds(visible, m_visible) && sameIds(prefetch, m_prefetch))
            return;
        m_visible = std::move(visible);
        m_prefetch = std::move(prefetch);
        m_wantedVersion++;
    }
    m_wake.notify_one();
}

void ThumbnailService::SetPaused(bool paused)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_paused = paused;
    }
    m_wake.notify_one();
}

auto ThumbnailService::Get(const std::string& id) const -> std::optional<Tile>
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_published.find(id);
    if (it == m_published.end())
        return std::nullopt;
    return it->second;
}

auto ThumbnailService::LatestFence() const -> GLsync
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_latestFence;
}

auto ThumbnailService::TakeFindings() -> std::vector<Finding>
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Finding> findings;
    findings.swap(m_findings);
    return findings;
}

void ThumbnailService::Run()
{
    SDL_GL_MakeCurrent(m_window, m_context);
    TrimCache();
    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_quit)
                break;
        }
        Work();
    }

    for (auto& [id, slot] : m_slots)
    {
        if (slot->instance != nullptr)
            projectm_destroy(slot->instance);
        glDeleteFramebuffers(1, &slot->fbo);
        glDeleteTextures(1, &slot->texture);
    }
    m_slots.clear();
    for (auto& [time, texture] : m_retiredTextures)
        glDeleteTextures(1, &texture);
    for (GLsync fence : m_oldFences)
        glDeleteSync(fence);
    glFinish();
    SDL_GL_MakeCurrent(m_window, nullptr);
}

void ThumbnailService::Work()
{
    std::vector<Request> visible;
    std::vector<Request> prefetch;
    bool paused = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        visible = m_visible;
        prefetch = m_prefetch;
        paused = m_paused;
    }

    if (paused)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_wake.wait_for(lock, std::chrono::milliseconds(100), [this] { return m_quit || !m_paused; });
        return;
    }

    bool drew = false;
    const auto now = Clock::now();

    // Drop what's no longer wanted.
    std::set<std::string> wanted;
    for (const auto& request : visible)
        wanted.insert(request.id);
    for (const auto& request : prefetch)
        wanted.insert(request.id);
    std::vector<std::string> unwanted;
    for (const auto& [id, slot] : m_slots)
    {
        if (wanted.count(id) == 0)
            unwanted.push_back(id);
    }
    for (const auto& id : unwanted)
        DestroySlot(id);

    // Make slots (showing cached images at once), visible first.
    for (const auto* list : {&visible, &prefetch})
    {
        for (const auto& request : *list)
        {
            if (m_slots.count(request.id) == 0)
            {
                CreateSlot(request, true);
                drew = true;
            }
        }
    }

    // Start at most one instance per pass (loading costs ~20 ms); visible tiles first, then prefetch.
    int instances = 0;
    for (const auto& [id, slot] : m_slots)
        instances += slot->instance != nullptr ? 1 : 0;
    bool started = false;
    for (const auto* list : {&visible, &prefetch})
    {
        for (const auto& request : *list)
        {
            if (started || instances >= kMaxInstances)
                break;
            auto& slot = *m_slots.at(request.id);
            if (slot.instance == nullptr && slot.state != TileState::Broken)
            {
                StartInstance(slot);
                if (slot.instance != nullptr)
                    WarmUp(slot);
                Publish(request.id, slot);
                started = true;
                drew = true;
                instances++;
            }
        }
    }
    m_liveInstances = instances;

    // Refresh live tiles about once a second.
    for (auto& [id, slot] : m_slots)
    {
        if (slot->instance != nullptr && slot->warmed && now - slot->lastRender >= kRefreshInterval)
        {
            RenderFrame(*slot);
            drew = true;
        }
    }

    if (drew)
    {
        GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glFlush();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_latestFence = fence;
        }
        m_oldFences.push_back(fence);
        while (m_oldFences.size() > 8)
        {
            glDeleteSync(m_oldFences.front());
            m_oldFences.erase(m_oldFences.begin());
        }
    }

    // Free textures of dropped tiles once the editor can no longer be drawing them.
    for (auto it = m_retiredTextures.begin(); it != m_retiredTextures.end();)
    {
        if (now - it->first > kTextureGrace)
        {
            glDeleteTextures(1, &it->second);
            it = m_retiredTextures.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (!started)
    {
        // Nothing to start: sleep until something changes or the next refresh is due.
        std::unique_lock<std::mutex> lock(m_mutex);
        const uint64_t version = m_wantedVersion;
        m_wake.wait_for(lock, std::chrono::milliseconds(50), [&] { return m_quit || m_paused || m_wantedVersion != version; });
    }
}

auto ThumbnailService::CreateSlot(const Request& request, bool /*wantLive*/) -> Slot*
{
    auto slot = std::make_unique<Slot>();
    slot->request = request;

    glGenTextures(1, &slot->texture);
    glBindTexture(GL_TEXTURE_2D, slot->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kWidth, kHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &slot->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, slot->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, slot->texture, 0);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // A saved image shows instantly while the live preview starts.
    const auto cacheFile = CacheFile(request);
    int width = 0;
    int height = 0;
    int channels = 0;
    if (unsigned char* data = stbi_load(cacheFile.c_str(), &width, &height, &channels, 4))
    {
        if (width == kWidth && height == kHeight)
        {
            std::vector<unsigned char> pixels(data, data + static_cast<size_t>(width) * height * 4);
            FlipRows(pixels, width, height);
            glBindTexture(GL_TEXTURE_2D, slot->texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            slot->state = TileState::Cached;
            std::error_code ec;
            std::filesystem::last_write_time(cacheFile, std::filesystem::file_time_type::clock::now(), ec); // for LRU trimming
        }
        stbi_image_free(data);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    Slot* raw = slot.get();
    m_slots[request.id] = std::move(slot);
    Publish(request.id, *raw);
    return raw;
}

void ThumbnailService::StartInstance(Slot& slot)
{
    std::vector<std::string> texturePaths;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        texturePaths = m_texturePaths;
    }
    projectm_handle instance = projectm_create();
    if (instance == nullptr)
        return;
    projectm_set_window_size(instance, kWidth, kHeight);
    projectm_set_preset_locked(instance, true);
    projectm_set_hard_cut_enabled(instance, false);
    projectm_set_soft_cut_duration(instance, 0.0);
    projectm_set_preset_duration(instance, 1.0e9);
    if (!texturePaths.empty())
    {
        std::vector<const char*> paths;
        for (const auto& path : texturePaths)
            paths.push_back(path.c_str());
        projectm_set_texture_search_paths(instance, paths.data(), paths.size());
    }

    LoadFailure failure;
    projectm_set_preset_switch_failed_event_callback(instance, &OnPresetFailed, &failure);
    projectm_load_preset_file(instance, slot.request.path.c_str(), false);
    projectm_set_preset_switch_failed_event_callback(instance, nullptr, nullptr);
    if (failure.failed)
    {
        projectm_destroy(instance);
        slot.state = TileState::Broken;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_findings.push_back({slot.request.path, failure.message});
        return;
    }
    slot.instance = instance;
}

void ThumbnailService::WarmUp(Slot& slot)
{
    const auto start = Clock::now();
    for (int i = 0; i < kWarmUpFrames && Clock::now() - start < kWarmUpBudget; ++i)
    {
        FeedSyntheticAudio(slot.instance, m_audioFrame++);
        glBindFramebuffer(GL_FRAMEBUFFER, slot.fbo);
        projectm_opengl_render_frame_fbo(slot.instance, slot.fbo);
    }
    slot.lastRender = Clock::now();
    slot.warmed = true;

    std::vector<unsigned char> pixels(static_cast<size_t>(kWidth) * kHeight * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, slot.fbo);
    glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    double luma = 0.0;
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        pixels[i + 3] = 255;
        luma += 0.299 * pixels[i] + 0.587 * pixels[i + 1] + 0.114 * pixels[i + 2];
    }
    luma /= 255.0 * (pixels.size() / 4);
    slot.state = luma < kDarkLuma ? TileState::Dark : TileState::Live;

    FlipRows(pixels, kWidth, kHeight);
    stbi_write_png(CacheFile(slot.request).c_str(), kWidth, kHeight, 4, pixels.data(), kWidth * 4);
}

void ThumbnailService::RenderFrame(Slot& slot)
{
    FeedSyntheticAudio(slot.instance, m_audioFrame++);
    glBindFramebuffer(GL_FRAMEBUFFER, slot.fbo);
    projectm_opengl_render_frame_fbo(slot.instance, slot.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    slot.lastRender = Clock::now();
}

void ThumbnailService::DestroySlot(const std::string& id)
{
    auto it = m_slots.find(id);
    if (it == m_slots.end())
        return;
    auto& slot = *it->second;
    if (slot.instance != nullptr)
        projectm_destroy(slot.instance);
    glDeleteFramebuffers(1, &slot.fbo);
    m_retiredTextures.emplace_back(Clock::now(), slot.texture);
    m_slots.erase(it);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_published.erase(id);
}

void ThumbnailService::Publish(const std::string& id, const Slot& slot)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_published[id] = {slot.state == TileState::Broken ? 0u : slot.texture, slot.state};
}

auto ThumbnailService::CacheFile(const Request& request) const -> std::filesystem::path
{
    char name[32];
    std::snprintf(name, sizeof(name), "%016zx.png", std::hash<std::string>{}(request.id + "|" + request.version));
    return m_cacheDir / name;
}

void ThumbnailService::TrimCache()
{
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;
    uintmax_t total = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(m_cacheDir, ec))
    {
        if (!entry.is_regular_file(ec))
            continue;
        total += entry.file_size(ec);
        files.emplace_back(entry.last_write_time(ec), entry.path());
    }
    if (total <= kCacheLimitBytes)
        return;
    std::sort(files.begin(), files.end());
    for (const auto& [time, path] : files)
    {
        if (total <= kCacheTrimTargetBytes)
            break;
        const auto size = std::filesystem::file_size(path, ec);
        if (std::filesystem::remove(path, ec))
            total -= size;
    }
    GV_LOG_INFO(kLogCategory, "trimmed the thumbnail cache to %.0f MB", total / 1048576.0);
}

} // namespace gv::thumbnails
