#include "engine/Lane.hpp"

#include "common/log.hpp"

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

/// Destroys a hosted projectM instance; a picture with no preset has none.
void Destroy(projectm_handle instance)
{
    if (instance != nullptr)
        projectm_destroy(instance);
}

auto DrawsVideo(const LanePlan& plan) -> bool
{
    return !plan.branches.empty();
}

auto DrawsTexture(const LanePlan& plan, const std::string& name) -> bool
{
    return std::any_of(plan.branches.begin(), plan.branches.end(), [&](const VideoBranch& branch) {
        return branch.layer == model::PresetLayer::Texture && branch.textureName == name;
    });
}


auto AttachedFbo(GLuint texture) -> GLuint
{
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete)
    {
        glDeleteFramebuffers(1, &fbo);
        return 0;
    }
    return fbo;
}

/// Copies all of `from` into all of `to` (1:1 when the sizes match).
void Blit(GLuint from, int fromWidth, int fromHeight, GLuint to, int toWidth, int toHeight, bool flip)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, from);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, to);
    glBlitFramebuffer(0, 0, fromWidth, fromHeight, 0, flip ? toHeight : 0, toWidth, flip ? 0 : toHeight, GL_COLOR_BUFFER_BIT,
                      fromWidth == toWidth && fromHeight == toHeight ? GL_NEAREST : GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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

void Lane::Prepare(InstanceId instance, int scene, std::string presetPath, LanePlan plan, bool priority)
{
    Job job;
    job.type = Job::Type::Prepare;
    job.instance = instance;
    job.scene = scene;
    job.presetPath = std::move(presetPath);
    job.plan = std::move(plan);
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

void Lane::SetPlan(InstanceId instance, LanePlan plan)
{
    Job job;
    job.type = Job::Type::SetPlan;
    job.instance = instance;
    job.plan = std::move(plan);
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

void Lane::Tick(PcmChunk pcm, long long tick, VideoFrame video, PcmChunk livePcm, model::SignalValues signals)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingTick.has = true;
        if (livePcm && !livePcm->empty())
            m_pendingTick.livePcm.push_back(std::move(livePcm));
        else if (pcm && !pcm->empty())
            m_pendingTick.livePcm.push_back(pcm);
        if (pcm && !pcm->empty())
        {
            m_pendingTick.pcm.push_back(std::move(pcm));
        }
        m_pendingTick.tick = tick;
        m_pendingTick.video = video;
        m_pendingTick.signals = std::move(signals);
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
    m_chain = std::make_unique<VideoChain>();

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
            case Job::Type::SetPlan:
                if (auto hosted = m_instances.find(job.instance); hosted != m_instances.end())
                {
                    CompileShaders(job.plan);
                    hosted->second.plan = job.plan;
                    RestoreTextures(job.instance, job.plan, hosted->second.textures);
                    DropUnusedPrograms();
                }
                break;
            case Job::Type::Shutdown:
                for (auto& [id, hosted] : m_instances)
                {
                    for (auto& [name, override] : hosted.textures)
                        DeleteOverride(override);
                    Destroy(hosted.handle);
                }
                m_instances.clear();
                m_programs.clear();
                if (m_placeholderVideo != 0)
                    glDeleteTextures(1, &m_placeholderVideo);
                m_chain.reset();
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

    // A picture with no preset has no projectM instance: only its shaders are compiled and warmed up.
    const bool picture = job.plan.picture;
    projectm_handle instance = picture ? nullptr : CreateInstance();
    if (!picture && instance == nullptr)
    {
        event.error = "projectm_create failed";
        m_sink(std::move(event));
        return;
    }

    LoadFailure failure;
    if (!picture)
    {
        projectm_set_preset_switch_failed_event_callback(instance, &OnPresetFailed, &failure);
        projectm_load_preset_file(instance, job.presetPath.c_str(), false);
        projectm_set_preset_switch_failed_event_callback(instance, nullptr, nullptr);
    }

    // Warm up with a placeholder video so the chain and blend shaders are compiled now, not on the first
    // live frame.
    std::vector<GLuint> onTop;
    if (DrawsVideo(job.plan))
    {
        CompileShaders(job.plan);
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
        if (!picture)
            onTop = RunBranches(instance, job.plan, m_placeholderVideo, nullptr);
    }

    if (failure.failed)
    {
        projectm_destroy(instance);
        DropUnusedPrograms();
        event.error = failure.message;
        event.prepareMs = MillisecondsSince(start);
        GV_LOG_WARN(kLogCategory, "lane %d: preset failed to load: %s: %s", m_id, job.presetPath.c_str(), failure.message.c_str());
        m_sink(std::move(event));
        return;
    }

    // Two warm-up draws absorb the driver's deferred first-draw compile (M1 results); the second is
    // close to the steady frame time and flags heavy presets.
    auto warmFrame = [&] {
        if (picture)
        {
            DrawPicture(job.plan, m_placeholderVideo, {}, m_scratchFbo);
            glFinish();
            return;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, m_scratchFbo);
        glViewport(0, 0, m_width, m_height);
        projectm_opengl_render_frame_fbo(instance, m_scratchFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_scratchFbo);
        glViewport(0, 0, m_width, m_height);
        for (GLuint texture : onTop)
            m_chain->Composite(texture);
        glFinish();
    };
    warmFrame();
    const auto second = Clock::now();
    warmFrame();
    event.frameMs = MillisecondsSince(second);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!picture)
    {
        projectm_set_layer_texture(instance, PROJECTM_LAYER_FEEDBACK, 0);
        projectm_set_layer_texture(instance, PROJECTM_LAYER_BEFORE_COMPOSITE, 0);
    }

    event.ok = true;
    event.prepareMs = MillisecondsSince(start);

    if (!keep)
    {
        Destroy(instance);
        DropUnusedPrograms();
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
        Destroy(instance);
        DropUnusedPrograms();
        return;
    }
    m_instances[job.instance] = {instance, job.plan, {}};
    m_sink(std::move(event));
}

void Lane::DoRelease(InstanceId id)
{
    auto it = m_instances.find(id);
    if (it != m_instances.end())
    {
        for (auto& [name, override] : it->second.textures)
            DeleteOverride(override);
        Destroy(it->second.handle);
        m_instances.erase(it);
        DropUnusedPrograms();
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
    DestroyTargets(); // branch targets too: they're recreated at the new size on first use
    CreateTargets();
    for (auto& [id, hosted] : m_instances)
    {
        if (hosted.handle != nullptr)
            projectm_set_window_size(hosted.handle, static_cast<size_t>(m_width), static_cast<size_t>(m_height));
    }
    glFinish();
}

void Lane::DoTick(PendingTick tick)
{
    InstanceId live = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        live = m_live;
    }
    for (auto& [id, hosted] : m_instances)
    {
        if (hosted.handle == nullptr)
            continue; // no preset: nothing hears the audio
        for (const auto& chunk : id == live ? tick.livePcm : tick.pcm)
        {
            AddPcm(hosted.handle, *chunk);
        }
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
    const float now = std::chrono::duration<float>(start - m_isfStart).count();
    m_isfClock.timeDelta = m_isfClock.frameIndex == 0 ? 0.f : now - m_isfClock.time;
    m_isfClock.time = now;
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
    // Video: wait (on the GPU) until the producer finished writing this frame, then draw it through the plan.
    const LanePlan& plan = it->second.plan;
    const GLuint video = DrawsVideo(plan) ? tick.video.texture : 0;
    if (video != 0 && tick.video.fence != nullptr && glIsSync(tick.video.fence))
    {
        glWaitSync(tick.video.fence, 0, GL_TIMEOUT_IGNORED);
    }

    if (timeThisFrame)
        glBeginQuery(GL_TIME_ELAPSED, m_timerQuery[slot]);

    if (it->second.handle == nullptr) // prepared with no preset (an edit may have left it nothing to show)
    {
        DrawPicture(plan, video, tick.signals, m_slotFbo[slot]);
        m_isfClock.frameIndex++;
    }
    else
    {
        const std::vector<GLuint> onTop = RunBranches(it->second.handle, plan, video, &it->second.textures, tick.signals);
        m_isfClock.frameIndex++;
        glBindFramebuffer(GL_FRAMEBUFFER, m_slotFbo[slot]);
        glViewport(0, 0, m_width, m_height);
        projectm_opengl_render_frame_fbo(it->second.handle, m_slotFbo[slot]);
        // On top: over the finished frame (not fed back; the feedback layer is).
        if (!onTop.empty())
        {
            glBindFramebuffer(GL_FRAMEBUFFER, m_slotFbo[slot]);
            glViewport(0, 0, m_width, m_height);
            for (GLuint texture : onTop)
                m_chain->Composite(texture);
        }
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

void Lane::DrawPicture(const LanePlan& plan, GLuint video, const model::SignalValues& signals, GLuint fbo)
{
    if (plan.branches.empty())
    {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }
    const VideoBranch& branch = plan.branches.front();
    const RenderTarget picture = BranchTarget(m_branchTargets, 0);
    const RenderTarget scratch = BranchTarget(m_branchScratch, 0);
    if (HasModulations(branch.steps))
        m_chain->DrawPicture(video, Modulated(branch.steps, signals), m_programs, m_isfClock, picture, scratch, fbo, m_width, m_height);
    else
        m_chain->DrawPicture(video, branch.steps, m_programs, m_isfClock, picture, scratch, fbo, m_width, m_height);
}

void Lane::DeleteOverride(TextureOverride& override)
{
    if (override.fbo != 0)
        glDeleteFramebuffers(1, &override.fbo);
    if (override.backupFbo != 0)
        glDeleteFramebuffers(1, &override.backupFbo);
    if (override.backup != 0)
        glDeleteTextures(1, &override.backup);
    override = {};
}

auto Lane::RunBranches(projectm_handle instance, const LanePlan& plan, GLuint video, TextureOverrides* overrides,
                       const model::SignalValues& signals) -> std::vector<GLuint>
{
    GLuint feedback = 0;
    GLuint beforeComposite = 0;
    std::vector<GLuint> onTop;
    if (video != 0)
    {
        for (size_t i = 0; i < plan.branches.size(); ++i)
        {
            const VideoBranch& branch = plan.branches[i];
            if (branch.layer == model::PresetLayer::Texture && overrides == nullptr)
                continue; // warm-ups: blits need no compiling
            const RenderTarget picture = BranchTarget(m_branchTargets, i);
            const bool ping = std::any_of(branch.steps.begin(), branch.steps.end(), [](const VideoStep& step) { return step.isf.has_value(); });
            const RenderTarget scratch = ping ? BranchTarget(m_branchScratch, i) : RenderTarget{};
            if (HasModulations(branch.steps))
                m_chain->RunSteps(video, Modulated(branch.steps, signals), m_programs, m_isfClock, picture, scratch, m_width, m_height);
            else
                m_chain->RunSteps(video, branch.steps, m_programs, m_isfClock, picture, scratch, m_width, m_height);
            const GLuint texture = picture.texture;
            const GLuint fbo = picture.fbo;
            switch (branch.layer)
            {
                case model::PresetLayer::Feedback: feedback = texture; break;
                case model::PresetLayer::BeforeComposite: beforeComposite = texture; break;
                case model::PresetLayer::OnTop: onTop.push_back(texture); break;
                case model::PresetLayer::Texture:
                    // Image rows are stored top first, video bottom up: flip so the video reads as the image would.
                    if (TextureOverride* target = Override(m_renderedLive, instance, branch.textureName, *overrides))
                        Blit(fbo, m_width, m_height, target->fbo, target->width, target->height, true);
                    break;
            }
        }
    }
    projectm_set_layer_texture(instance, PROJECTM_LAYER_FEEDBACK, feedback);
    projectm_set_layer_texture(instance, PROJECTM_LAYER_BEFORE_COMPOSITE, beforeComposite);
    return onTop;
}

auto Lane::Override(InstanceId id, projectm_handle instance, const std::string& name, TextureOverrides& overrides) -> TextureOverride*
{
    if (auto it = overrides.find(name); it != overrides.end())
        return it->second.texture != 0 ? &it->second : nullptr;

    TextureOverride& override = overrides[name];
    uint32_t texture = 0;
    if (!projectm_get_preset_texture(instance, name.c_str(), &texture, &override.width, &override.height) || override.width <= 0 ||
        override.height <= 0)
    {
        GV_LOG_WARN(kLogCategory, "lane %d: instance %llu: the preset has no texture \"%s\" to put video into", m_id,
                    static_cast<unsigned long long>(id), name.c_str());
        override = {};
        return nullptr;
    }
    override.texture = texture;
    override.fbo = AttachedFbo(texture);
    glGenTextures(1, &override.backup);
    glBindTexture(GL_TEXTURE_2D, override.backup);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, override.width, override.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    override.backupFbo = AttachedFbo(override.backup);
    if (override.fbo == 0 || override.backupFbo == 0)
    {
        GV_LOG_WARN(kLogCategory, "lane %d: instance %llu: texture \"%s\" can't be drawn into", m_id, static_cast<unsigned long long>(id),
                    name.c_str());
        DeleteOverride(override);
        return nullptr;
    }
    Blit(override.fbo, override.width, override.height, override.backupFbo, override.width, override.height, false);
    GV_LOG_INFO(kLogCategory, "lane %d: instance %llu: video replaces texture \"%s\" (%dx%d)", m_id, static_cast<unsigned long long>(id),
                name.c_str(), override.width, override.height);
    return &override;
}

void Lane::RestoreTextures(InstanceId id, const LanePlan& plan, TextureOverrides& overrides)
{
    for (auto it = overrides.begin(); it != overrides.end();)
    {
        if (DrawsTexture(plan, it->first))
        {
            ++it;
            continue;
        }
        TextureOverride& override = it->second;
        if (override.texture != 0)
        {
            Blit(override.backupFbo, override.width, override.height, override.fbo, override.width, override.height, false);
            GV_LOG_INFO(kLogCategory, "lane %d: instance %llu: texture \"%s\" is the preset's image again", m_id,
                        static_cast<unsigned long long>(id), it->first.c_str());
        }
        DeleteOverride(override);
        it = overrides.erase(it);
    }
}

auto Lane::BranchTarget(std::vector<RenderTarget>& targets, size_t index) -> RenderTarget
{
    while (targets.size() <= index)
    {
        RenderTarget target;
        glGenTextures(1, &target.texture);
        glGenFramebuffers(1, &target.fbo);
        CreateTarget(target.texture, target.fbo, m_width, m_height);
        targets.push_back(target);
    }
    return targets[index];
}

void Lane::CompileShaders(const LanePlan& plan)
{
    for (const auto& branch : plan.branches)
    {
        for (const auto& step : branch.steps)
        {
            if (!step.isf || m_programs.contains(step.isf->shader))
                continue;
            const auto start = Clock::now();
            LaneEvent event;
            event.type = LaneEvent::Type::ShaderCompiled;
            event.lane = m_id;
            event.shaderPath = step.isf->path.string();
            event.error = CompileIsf(m_programs, step.isf->shader, LastGoodShader(step.isf->path));
            event.ok = event.error.empty();
            event.prepareMs = MillisecondsSince(start);
            if (!event.ok)
                GV_LOG_WARN(kLogCategory, "lane %d: ISF shader didn't compile: %s: %s", m_id, event.shaderPath.c_str(), event.error.c_str());
            m_sink(std::move(event));
        }
    }
}

auto Lane::LastGoodShader(const std::filesystem::path& path) const -> std::shared_ptr<const isf::IsfShader>
{
    for (const auto& [id, hosted] : m_instances)
    {
        for (const auto& branch : hosted.plan.branches)
        {
            for (const auto& step : branch.steps)
            {
                const auto program = step.isf && step.isf->path == path ? m_programs.find(step.isf->shader) : m_programs.end();
                if (program != m_programs.end() && program->second->Ok())
                    return step.isf->shader;
            }
        }
    }
    return nullptr;
}

void Lane::DropUnusedPrograms()
{
    std::erase_if(m_programs, [&](const auto& entry) {
        return std::none_of(m_instances.begin(), m_instances.end(), [&](const auto& hosted) {
            return std::any_of(hosted.second.plan.branches.begin(), hosted.second.plan.branches.end(), [&](const VideoBranch& branch) {
                return std::any_of(branch.steps.begin(), branch.steps.end(),
                                   [&](const VideoStep& step) { return step.isf && step.isf->shader == entry.first; });
            });
        });
    });
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
    for (auto* targets : {&m_branchTargets, &m_branchScratch})
    {
        for (auto& target : *targets)
        {
            glDeleteFramebuffers(1, &target.fbo);
            glDeleteTextures(1, &target.texture);
        }
        targets->clear();
    }
}

} // namespace gv::engine
