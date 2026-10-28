#include "engine/Lane.hpp"

#include "common/log.hpp"
#include "engine/VideoOverlay.hpp"

#include <algorithm>
#include <chrono>

namespace gv::engine {

namespace {

constexpr const char* kLogCategory = "lane";

using Clock = std::chrono::steady_clock;

auto MillisecondsSince(Clock::time_point start) -> double
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct LoadFailure
{
    bool failed{false};
    std::string message;
};

void OnPresetFailed(const char* /*filename*/, const char* message, void* userData)
{
    auto* failure = static_cast<LoadFailure*>(userData);
    failure->failed = true;
    failure->message = message != nullptr ? message : "unknown error";
    // projectM reports unreadable files as a bare "std::exception".
    if (failure->message == "std::exception")
        failure->message = "the preset file couldn't be read";
}

void CreateTarget(GLuint texture, GLuint fbo, int width, int height)
{
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void AddPcm(projectm_handle instance, const std::vector<float>& samples)
{
    const size_t maxFrames = projectm_pcm_get_max_samples();
    const size_t frames = samples.size() / 2;
    for (size_t offset = 0; offset < frames; offset += maxFrames)
    {
        const size_t count = std::min(maxFrames, frames - offset);
        projectm_pcm_add_float(instance, samples.data() + offset * 2, static_cast<unsigned int>(count), PROJECTM_STEREO);
    }
}

} // namespace

Lane::Lane(int id, SDL_Window* window, SDL_GLContext context, EventSink sink)
    : m_id(id)
    , m_window(window)
    , m_context(context)
    , m_sink(std::move(sink))
{
    m_thread = std::thread([this] { Run(); });
}

Lane::~Lane()
{
    Job shutdown;
    shutdown.type = Job::Type::Shutdown;
    Push(std::move(shutdown), false);
    m_thread.join();
    SDL_GL_DeleteContext(m_context);
    SDL_DestroyWindow(m_window);
}

void Lane::Configure(int width, int height, std::vector<std::string> texturePaths)
{
    Job job;
    job.type = Job::Type::Configure;
    job.width = width;
    job.height = height;
    job.texturePaths = std::move(texturePaths);
    Push(std::move(job), true);
}

void Lane::Prepare(InstanceId instance, int scene, std::string presetPath, VideoSettings video, bool priority)
{
    Job job;
    job.type = Job::Type::Prepare;
    job.instance = instance;
    job.scene = scene;
    job.presetPath = std::move(presetPath);
    job.video = video;
    Push(std::move(job), priority);
}

void Lane::Warm(int scene, std::string presetPath)
{
    Job job;
    job.type = Job::Type::Warm;
    job.scene = scene;
    job.presetPath = std::move(presetPath);
    Push(std::move(job), false);
}

void Lane::Release(InstanceId instance)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // A queued prepare is simply dropped; one in progress is destroyed when it finishes.
        auto queued = std::find_if(m_jobs.begin(), m_jobs.end(), [&](const Job& job) {
            return job.type == Job::Type::Prepare && job.instance == instance;
        });
        if (queued != m_jobs.end())
        {
            m_jobs.erase(queued);
            return;
        }
        m_released.insert(instance);
        if (m_live == instance)
        {
            m_live = 0;
        }
    }
    Job job;
    job.type = Job::Type::Release;
    job.instance = instance;
    Push(std::move(job), true);
}

void Lane::UpdateVideo(InstanceId instance, VideoSettings video)
{
    Job job;
    job.type = Job::Type::UpdateVideo;
    job.instance = instance;
    job.video = video;
    Push(std::move(job), true);
}

auto Lane::CancelQueued() -> std::pair<std::vector<InstanceId>, std::vector<std::pair<int, std::string>>>
{
    std::pair<std::vector<InstanceId>, std::vector<std::pair<int, std::string>>> cancelled;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_jobs.begin(); it != m_jobs.end();)
    {
        if (it->type == Job::Type::Prepare)
        {
            cancelled.first.push_back(it->instance);
            it = m_jobs.erase(it);
        }
        else if (it->type == Job::Type::Warm)
        {
            cancelled.second.emplace_back(it->scene, it->presetPath);
            it = m_jobs.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return cancelled;
}

void Lane::SetLive(InstanceId instance)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_live = instance;
}

void Lane::Tick(PcmChunk pcm, long long tick, VideoFrame video)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingTick.has = true;
        if (pcm && !pcm->empty())
        {
            m_pendingTick.pcm.push_back(std::move(pcm));
        }
        m_pendingTick.tick = tick;
        m_pendingTick.video = video;
    }
    m_cv.notify_one();
}

auto Lane::TakeLatest() -> PublishedFrame
{
    std::lock_guard<std::mutex> lock(m_publishMutex);
    if (m_publishedTaken)
    {
        return {};
    }
    m_publishedTaken = true;
    return m_published;
}

auto Lane::QueuedJobs() const -> size_t
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_jobs.size();
}

auto Lane::LiveRenderMs() const -> double
{
    std::lock_guard<std::mutex> lock(m_publishMutex);
    return m_liveRenderMs;
}

auto Lane::LiveGpuMs() const -> double
{
    std::lock_guard<std::mutex> lock(m_publishMutex);
    return m_liveGpuMs;
}

void Lane::Push(Job job, bool front)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (front)
            m_jobs.push_front(std::move(job));
        else
            m_jobs.push_back(std::move(job));
    }
    m_cv.notify_one();
}

void Lane::Run()
{
    if (SDL_GL_MakeCurrent(m_window, m_context) != 0)
    {
        GV_LOG_ERROR(kLogCategory, "lane %d: could not make its GL context current: %s", m_id, SDL_GetError());
    }

    while (true)
    {
        PendingTick tick;
        Job job;
        bool haveJob = false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_pendingTick.has || !m_jobs.empty(); });
            // Ticks first: the live scene must never wait behind preparation work.
            if (m_pendingTick.has)
            {
                tick = std::move(m_pendingTick);
                m_pendingTick = {};
            }
            else
            {
                job = std::move(m_jobs.front());
                m_jobs.pop_front();
                haveJob = true;
            }
        }

        if (!haveJob)
        {
            DoTick(std::move(tick));
            continue;
        }

        switch (job.type)
        {
            case Job::Type::Prepare:
                DoPrepare(job, true);
                break;
            case Job::Type::Warm:
                DoPrepare(job, false);
                break;
            case Job::Type::Release:
                DoRelease(job.instance);
                break;
            case Job::Type::Configure:
                DoConfigure(job);
                break;
            case Job::Type::UpdateVideo:
                if (auto hosted = m_instances.find(job.instance); hosted != m_instances.end())
                {
                    hosted->second.video = job.video;
                    if (job.video.enabled && job.video.overlay && !m_overlay)
                        m_overlay = std::make_unique<VideoOverlay>();
                    projectm_set_feedback_mix(hosted->second.handle, job.video.mix);
                    projectm_set_feedback_luma_gate(hosted->second.handle, job.video.lumaMin);
                    projectm_set_feedback_contrast(hosted->second.handle, job.video.contrast);
                    projectm_set_feedback_saturation(hosted->second.handle, job.video.saturation);
                }
                break;
            case Job::Type::Shutdown:
                for (auto& [id, hosted] : m_instances)
                    projectm_destroy(hosted.handle);
                m_instances.clear();
                if (m_placeholderVideo != 0)
                    glDeleteTextures(1, &m_placeholderVideo);
                m_overlay.reset();
                {
                    std::lock_guard<std::mutex> lock(m_publishMutex);
                    if (m_published.fence != nullptr && !m_publishedTaken)
                        glDeleteSync(m_published.fence);
                    m_published = {};
                }
                DestroyTargets();
                if (m_timerQuery[0] != 0)
                    glDeleteQueries(kSlots, m_timerQuery);
                glFinish();
                SDL_GL_MakeCurrent(m_window, nullptr);
                return;
        }
    }
}

auto Lane::CreateInstance() -> projectm_handle
{
    projectm_handle instance = projectm_create();
    if (instance == nullptr)
    {
        return nullptr;
    }
    projectm_set_window_size(instance, static_cast<size_t>(m_width), static_cast<size_t>(m_height));
    projectm_set_preset_locked(instance, true);
    projectm_set_hard_cut_enabled(instance, false);
    projectm_set_soft_cut_duration(instance, 0.0);
    projectm_set_preset_duration(instance, 1.0e9);
    if (!m_texturePaths.empty())
    {
        std::vector<const char*> paths;
        for (const auto& path : m_texturePaths)
            paths.push_back(path.c_str());
        projectm_set_texture_search_paths(instance, paths.data(), paths.size());
    }
    return instance;
}

void Lane::DoPrepare(const Job& job, bool keep)
{
    if (keep)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_released.erase(job.instance) > 0)
        {
            return;
        }
    }

    const auto start = Clock::now();
    LaneEvent event;
    event.type = keep ? LaneEvent::Type::Prepared : LaneEvent::Type::Warmed;
    event.lane = m_id;
    event.instance = job.instance;
    event.scene = job.scene;
    event.presetPath = job.presetPath;

    projectm_handle instance = CreateInstance();
    if (instance == nullptr)
    {
        event.error = "projectm_create failed";
        m_sink(std::move(event));
        return;
    }

    LoadFailure failure;
    projectm_set_preset_switch_failed_event_callback(instance, &OnPresetFailed, &failure);
    projectm_load_preset_file(instance, job.presetPath.c_str(), false);
    projectm_set_preset_switch_failed_event_callback(instance, nullptr, nullptr);

    if (job.video.enabled)
    {
        projectm_set_feedback_mix(instance, job.video.mix);
        projectm_set_feedback_luma_gate(instance, job.video.lumaMin);
        projectm_set_feedback_contrast(instance, job.video.contrast);
        projectm_set_feedback_saturation(instance, job.video.saturation);
        // Warm up with a placeholder so the blend shader is compiled now, not on the first live frame.
        if (m_placeholderVideo == 0)
        {
            const unsigned char black[4] = {0, 0, 0, 255};
            glGenTextures(1, &m_placeholderVideo);
            glBindTexture(GL_TEXTURE_2D, m_placeholderVideo);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
            // Without mipmaps the default minification filter leaves the texture incomplete.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        projectm_set_feedback_texture(instance, m_placeholderVideo);
        // The overlay's shader is compiled now too, not on the first live frame.
        if (job.video.overlay && !m_overlay)
            m_overlay = std::make_unique<VideoOverlay>();
    }

    if (failure.failed)
    {
        projectm_destroy(instance);
        event.error = failure.message;
        event.prepareMs = MillisecondsSince(start);
        GV_LOG_WARN(kLogCategory, "lane %d: preset failed to load: %s: %s", m_id, job.presetPath.c_str(), failure.message.c_str());
        m_sink(std::move(event));
        return;
    }

    // Two warm-up draws absorb the driver's deferred first-draw compile (M1 results); the second is
    // close to the steady frame time and flags heavy presets.
    glBindFramebuffer(GL_FRAMEBUFFER, m_scratchFbo);
    projectm_opengl_render_frame_fbo(instance, m_scratchFbo);
    glFinish();
    const auto second = Clock::now();
    projectm_opengl_render_frame_fbo(instance, m_scratchFbo);
    glFinish();
    event.frameMs = MillisecondsSince(second);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    projectm_set_feedback_texture(instance, 0);

    event.ok = true;
    event.prepareMs = MillisecondsSince(start);

    if (!keep)
    {
        projectm_destroy(instance);
        m_sink(std::move(event));
        return;
    }

    bool releasedMeanwhile = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        releasedMeanwhile = m_released.erase(job.instance) > 0;
    }
    if (releasedMeanwhile)
    {
        projectm_destroy(instance);
        return;
    }
    m_instances[job.instance] = {instance, job.video};
    m_sink(std::move(event));
}

void Lane::DoRelease(InstanceId id)
{
    auto it = m_instances.find(id);
    if (it != m_instances.end())
    {
        projectm_destroy(it->second.handle);
        m_instances.erase(it);
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_released.erase(id);
}

void Lane::DoConfigure(const Job& job)
{
    m_texturePaths = job.texturePaths;
    if (job.width == m_width && job.height == m_height && m_slotFbo[0] != 0)
    {
        return;
    }
    m_width = job.width;
    m_height = job.height;
    DestroyTargets();
    CreateTargets();
    for (auto& [id, hosted] : m_instances)
    {
        projectm_set_window_size(hosted.handle, static_cast<size_t>(m_width), static_cast<size_t>(m_height));
    }
    glFinish();
}

void Lane::DoTick(PendingTick tick)
{
    for (auto& [id, hosted] : m_instances)
    {
        for (const auto& chunk : tick.pcm)
        {
            AddPcm(hosted.handle, *chunk);
        }
    }

    InstanceId live = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        live = m_live;
    }
    auto it = m_instances.find(live);
    if (live != m_renderedLive)
    {
        // A different instance went live (or none): an untaken frame belongs to the old one.
        std::lock_guard<std::mutex> lock(m_publishMutex);
        if (m_published.fence != nullptr && !m_publishedTaken)
            glDeleteSync(m_published.fence);
        m_published = {};
        m_publishedTaken = true;
        m_renderedLive = live;
    }
    if (it == m_instances.end())
    {
        return;
    }

    const auto start = Clock::now();
    const int slot = static_cast<int>(tick.tick % kSlots);

    // GPU time of earlier frames, read without waiting once the result is available.
    for (int i = 0; i < kSlots; ++i)
    {
        if (!m_queryPending[i])
            continue;
        GLint available = 0;
        glGetQueryObjectiv(m_timerQuery[i], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available)
        {
            GLuint64 nanoseconds = 0;
            glGetQueryObjectui64v(m_timerQuery[i], GL_QUERY_RESULT, &nanoseconds);
            m_queryPending[i] = false;
            std::lock_guard<std::mutex> lock(m_publishMutex);
            m_liveGpuMs = m_liveGpuMs * 0.9 + (static_cast<double>(nanoseconds) / 1.0e6) * 0.1;
        }
    }
    const bool timeThisFrame = !m_queryPending[slot];
    // Video: wait (on the GPU) until the producer finished writing this frame, then blend it.
    const VideoSettings& settings = it->second.video;
    const GLuint video = settings.enabled ? tick.video.texture : 0;
    if (video != 0 && tick.video.fence != nullptr && glIsSync(tick.video.fence))
    {
        glWaitSync(tick.video.fence, 0, GL_TIMEOUT_IGNORED);
    }
    projectm_set_feedback_texture(it->second.handle, video);

    if (timeThisFrame)
        glBeginQuery(GL_TIME_ELAPSED, m_timerQuery[slot]);

    glBindFramebuffer(GL_FRAMEBUFFER, m_slotFbo[slot]);
    glViewport(0, 0, m_width, m_height);
    projectm_opengl_render_frame_fbo(it->second.handle, m_slotFbo[slot]);
    // Overlay: the video again, on top of the finished frame (it's not fed back; the blend above is).
    if (video != 0 && settings.overlay)
    {
        if (!m_overlay)
            m_overlay = std::make_unique<VideoOverlay>();
        glBindFramebuffer(GL_FRAMEBUFFER, m_slotFbo[slot]);
        glViewport(0, 0, m_width, m_height);
        m_overlay->Draw(video, settings);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (timeThisFrame)
    {
        glEndQuery(GL_TIME_ELAPSED);
        m_queryPending[slot] = true;
    }
    GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    const double ms = MillisecondsSince(start);

    std::lock_guard<std::mutex> lock(m_publishMutex);
    if (m_published.fence != nullptr && !m_publishedTaken)
        glDeleteSync(m_published.fence);
    m_published = {m_slotTexture[slot], fence, tick.tick, live};
    m_publishedTaken = false;
    m_liveRenderMs = m_liveRenderMs * 0.9 + ms * 0.1;
}

void Lane::CreateTargets()
{
    glGenTextures(kSlots, m_slotTexture);
    glGenFramebuffers(kSlots, m_slotFbo);
    for (int i = 0; i < kSlots; ++i)
    {
        CreateTarget(m_slotTexture[i], m_slotFbo[i], m_width, m_height);
    }
    glGenTextures(1, &m_scratchTexture);
    glGenFramebuffers(1, &m_scratchFbo);
    CreateTarget(m_scratchTexture, m_scratchFbo, m_width, m_height);
    if (m_timerQuery[0] == 0)
        glGenQueries(kSlots, m_timerQuery);
}

void Lane::DestroyTargets()
{
    if (m_slotFbo[0] != 0)
    {
        glDeleteFramebuffers(kSlots, m_slotFbo);
        glDeleteTextures(kSlots, m_slotTexture);
        glDeleteFramebuffers(1, &m_scratchFbo);
        glDeleteTextures(1, &m_scratchTexture);
        std::fill(std::begin(m_slotFbo), std::end(m_slotFbo), 0u);
        std::fill(std::begin(m_slotTexture), std::end(m_slotTexture), 0u);
        m_scratchFbo = 0;
        m_scratchTexture = 0;
    }
}

} // namespace gv::engine
