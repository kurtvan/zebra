#include "engine/Engine.hpp"

#include "common/log.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace gv::engine {

namespace {

constexpr const char* kLogCategory = "engine";
constexpr double kFrameBudgetMs = 1000.0 / 60.0;

} // namespace

Engine::Engine()
    : m_pool(PoolConfig{})
{
}

Engine::~Engine()
{
    Shutdown();
}

auto Engine::Start(int laneCount, PoolConfig poolConfig) -> bool
{
    SDL_Window* shareWindow = SDL_GL_GetCurrentWindow();
    SDL_GLContext shareContext = SDL_GL_GetCurrentContext();
    if (shareContext == nullptr)
    {
        GV_LOG_ERROR(kLogCategory, "Start() needs a current GL context to share with");
        return false;
    }

    poolConfig.lanes = laneCount;
    m_pool = ScenePool(poolConfig);

    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    for (int i = 0; i < laneCount; ++i)
    {
        SDL_Window* window = SDL_CreateWindow("lane", 0, 0, 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        SDL_GLContext context = window != nullptr ? SDL_GL_CreateContext(window) : nullptr;
        SDL_GL_MakeCurrent(shareWindow, shareContext);
        if (context == nullptr)
        {
            GV_LOG_ERROR(kLogCategory, "could not create lane %d's GL context: %s", i, SDL_GetError());
            if (window != nullptr)
                SDL_DestroyWindow(window);
            m_lanes.clear();
            return false;
        }
        m_lanes.push_back(std::make_unique<Lane>(i, window, context, [this](LaneEvent event) {
            std::lock_guard<std::mutex> lock(m_eventMutex);
            m_events.push_back(std::move(event));
        }));
        m_lanes.back()->Configure(m_width, m_height, m_texturePaths);
    }

    CreatePresentTargets();
    GV_LOG_INFO(kLogCategory, "started %d lanes, render size %dx%d", laneCount, m_width, m_height);
    return true;
}

void Engine::Shutdown()
{
    m_lanes.clear(); // joins the lane threads
    if (m_presentFence != nullptr)
    {
        glDeleteSync(m_presentFence);
        m_presentFence = nullptr;
    }
    if (m_presentFbo != 0)
    {
        glDeleteFramebuffers(1, &m_presentFbo);
        glDeleteFramebuffers(1, &m_readFbo);
        glDeleteTextures(1, &m_presentedTexture);
        m_presentFbo = m_readFbo = m_presentedTexture = 0;
    }
}

void Engine::SetRenderSize(int width, int height)
{
    if (width <= 0 || height <= 0 || (width == m_width && height == m_height))
    {
        return;
    }
    m_width = width;
    m_height = height;
    for (auto& lane : m_lanes)
    {
        lane->Configure(m_width, m_height, m_texturePaths);
    }
    if (m_presentFbo != 0)
    {
        glDeleteFramebuffers(1, &m_presentFbo);
        glDeleteFramebuffers(1, &m_readFbo);
        glDeleteTextures(1, &m_presentedTexture);
        CreatePresentTargets();
    }
    GV_LOG_INFO(kLogCategory, "render size %dx%d", m_width, m_height);
}

void Engine::SetTexturePaths(std::vector<std::string> paths)
{
    m_texturePaths = std::move(paths);
    for (auto& lane : m_lanes)
    {
        lane->Configure(m_width, m_height, m_texturePaths);
    }
}

void Engine::LoadPerformance(const model::Performance& performance)
{
    m_performance = performance;
    std::vector<bool> unusable;
    for (const auto& scene : m_performance.scenes)
    {
        unusable.push_back(scene.presetMissing || scene.presetPath.empty());
    }

    for (auto& lane : m_lanes)
    {
        lane->CancelQueued();
        lane->SetLive(0);
    }
    m_handover = {};
    m_handoverRelease = {};
    m_liveLane = -1;
    m_liveInstance = 0;
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        m_events.clear();
    }

    Execute(m_pool.Reset(static_cast<int>(m_performance.scenes.size()), unusable));

    // Pre-warm every other usable preset once so the driver's shader cache is hot (M1 results).
    std::set<std::string> queued;
    m_warmDone = 0;
    m_warmTotal = 0;
    for (int i = 0; i < static_cast<int>(m_performance.scenes.size()); ++i)
    {
        const auto& scene = m_performance.scenes[static_cast<size_t>(i)];
        const auto status = m_pool.StatusOf(i).state;
        if (unusable[static_cast<size_t>(i)] || status != ScenePool::State::None)
        {
            continue;
        }
        if (queued.insert(scene.presetPath.string()).second && m_frameMsByPreset.count(scene.presetPath.string()) == 0)
        {
            QueueWarm(i, scene.presetPath.string(), -1);
            m_warmTotal++;
        }
    }

    // Black until the first trigger.
    if (m_presentFbo != 0)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_presentFbo);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glFlush();
    }
    GV_LOG_INFO(kLogCategory, "loaded performance \"%s\": %zu scenes, pre-warming %d presets",
                m_performance.name.c_str(), m_performance.scenes.size(), m_warmTotal);
}

void Engine::UpdatePerformance(const model::Performance& performance)
{
    std::map<std::string, int> newIndexById;
    for (int i = 0; i < static_cast<int>(performance.scenes.size()); ++i)
        newIndexById[performance.scenes[static_cast<size_t>(i)].id] = i;

    std::vector<int> oldToNew(m_performance.scenes.size(), -1);
    std::vector<bool> presetChanged(performance.scenes.size(), true); // new scenes count as changed
    std::vector<bool> unusable;
    for (const auto& scene : performance.scenes)
        unusable.push_back(scene.presetMissing || scene.presetPath.empty());

    for (int old = 0; old < static_cast<int>(m_performance.scenes.size()); ++old)
    {
        const auto& before = m_performance.scenes[static_cast<size_t>(old)];
        auto it = newIndexById.find(before.id);
        if (it == newIndexById.end())
            continue;
        oldToNew[static_cast<size_t>(old)] = it->second;
        const auto& after = performance.scenes[static_cast<size_t>(it->second)];
        presetChanged[static_cast<size_t>(it->second)] = before.presetPath != after.presetPath;

        // Video blend settings apply to the existing instances (including the live one) right away.
        if (!presetChanged[static_cast<size_t>(it->second)] &&
            (before.video.enabled != after.video.enabled || before.video.mix != after.video.mix || before.video.lumaMin != after.video.lumaMin ||
             before.video.contrast != after.video.contrast || before.video.saturation != after.video.saturation))
        {
            const VideoSettings video{after.video.enabled, after.video.mix, after.video.lumaMin, after.video.contrast, after.video.saturation};
            for (const auto& hosted : m_pool.InstancesOf(old))
                m_lanes[static_cast<size_t>(hosted.lane)]->UpdateVideo(hosted.instance, video);
            if (m_handover.instance != 0 && m_handover.scene == old)
                m_lanes[static_cast<size_t>(m_handover.lane)]->UpdateVideo(m_handover.instance, video);
        }
    }

    if (m_handover.scene >= 0)
        m_handover.scene = oldToNew[static_cast<size_t>(m_handover.scene)];
    m_performance = performance;
    Execute(m_pool.Remap(static_cast<int>(performance.scenes.size()), oldToNew, unusable, presetChanged));

    // Pre-warm presets that are new to this session.
    for (int i = 0; i < static_cast<int>(performance.scenes.size()); ++i)
    {
        const auto& path = performance.scenes[static_cast<size_t>(i)].presetPath.string();
        if (presetChanged[static_cast<size_t>(i)] && !unusable[static_cast<size_t>(i)] && m_frameMsByPreset.count(path) == 0 &&
            m_pool.StatusOf(i).state == ScenePool::State::None)
        {
            QueueWarm(i, path, -1);
            m_warmTotal++;
        }
    }
}

void Engine::Trigger(int index)
{
    if (index < 0 || index >= static_cast<int>(m_performance.scenes.size()))
    {
        return;
    }
    const auto status = m_pool.StatusOf(index).state;
    if (status == ScenePool::State::Unusable || status == ScenePool::State::Failed)
    {
        GV_LOG_WARN(kLogCategory, "scene %d (%s) can't be played: %s", index,
                    m_performance.scenes[static_cast<size_t>(index)].DisplayName().c_str(),
                    status == ScenePool::State::Unusable ? "preset file missing" : "preset failed to load");
        return;
    }
    if (status != ScenePool::State::Ready && index != m_pool.LiveScene())
    {
        GV_LOG_INFO(kLogCategory, "scene %d isn't prepared yet; holding the current frame until it is", index);
    }
    Execute(m_pool.Trigger(index));
}

void Engine::Tick(const float* samples, size_t frames, float gain)
{
    std::deque<LaneEvent> events;
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        events.swap(m_events);
    }
    for (const auto& event : events)
    {
        HandleEvent(event);
    }

    PcmChunk chunk;
    if (frames > 0)
    {
        auto buffer = std::make_shared<std::vector<float>>(samples, samples + frames * 2);
        double sum = 0.0;
        for (auto& value : *buffer)
        {
            value *= gain;
            sum += static_cast<double>(value) * value;
        }
        m_audioLevel = static_cast<float>(std::sqrt(sum / static_cast<double>(buffer->size())));
        chunk = std::move(buffer);
    }

    // Video for the live scene and, during a handover, the outgoing one; other lanes only take PCM.
    const int liveScene = m_pool.LiveScene();
    for (int i = 0; i < static_cast<int>(m_lanes.size()); ++i)
    {
        VideoFrame video;
        if (m_videoProvider != nullptr)
        {
            if (i == m_liveLane && liveScene >= 0)
                video = m_videoProvider->FrameFor(liveScene);
            else if (i == m_handover.lane && m_handover.scene >= 0)
                video = m_videoProvider->FrameFor(m_handover.scene);
        }
        m_lanes[static_cast<size_t>(i)]->Tick(chunk, m_tick, video);
    }

    // Once a scene has been live for half a second, its lane's average render time replaces the
    // (noisy, single-frame) warm-up estimate.
    const int live = m_pool.LiveScene();
    if (live >= 0)
    {
        m_liveTicksByScene[live]++;
    }
    if (m_liveLane >= 0 && live >= 0 && m_tick - m_lastActivateTick > 30)
    {
        const auto& path = m_performance.scenes[static_cast<size_t>(live)].presetPath.string();
        const auto& lane = *m_lanes[static_cast<size_t>(m_liveLane)];
        const double ms = std::max(lane.LiveRenderMs(), lane.LiveGpuMs());
        const bool wasHeavy = m_liveMsByPreset.count(path) > 0 && m_liveMsByPreset[path] > kFrameBudgetMs;
        m_liveMsByPreset[path] = ms;
        if (ms > kFrameBudgetMs && !wasHeavy)
        {
            GV_LOG_WARN(kLogCategory, "heavy preset live: %.0f ms/frame, can't reach 60 fps: %s", ms, path.c_str());
        }
    }
    m_tick++;
}

auto Engine::Present() -> GLuint
{
    if (m_liveLane < 0 || m_presentFbo == 0)
    {
        return m_presentedTexture;
    }

    PublishedFrame frame = m_lanes[static_cast<size_t>(m_liveLane)]->TakeLatest();
    if (frame.tick >= 0 && frame.instance == m_liveInstance)
    {
        // The new scene's first frame is here: the outgoing scene can stop.
        FinishHandover();
    }
    else
    {
        if (frame.fence != nullptr)
            glDeleteSync(frame.fence);
        frame = {};
        // During a handover the outgoing scene keeps rendering, so a cut never repeats a frame.
        if (m_handover.instance != 0)
        {
            frame = m_lanes[static_cast<size_t>(m_handover.lane)]->TakeLatest();
            if (frame.tick < 0 || frame.instance != m_handover.instance)
            {
                if (frame.fence != nullptr)
                    glDeleteSync(frame.fence);
                frame = {};
            }
        }
        if (frame.tick < 0)
        {
            m_heldTicks++;
            if (m_pool.PendingScene() >= 0)
                m_heldWaitingTicks++;
            else if (m_tick - m_lastActivateTick < 30)
                m_heldNearCutTicks++;
            m_heldByScene[m_pool.LiveScene()]++;
            return m_presentedTexture;
        }
    }

    glWaitSync(frame.fence, 0, GL_TIMEOUT_IGNORED);
    glDeleteSync(frame.fence);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_readFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, frame.texture, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_presentFbo);
    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    if (m_presentFence != nullptr)
        glDeleteSync(m_presentFence);
    m_presentFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();

    m_lastPresentedTick = frame.tick;
    m_presentedFrames++;
    return m_presentedTexture;
}

auto Engine::Scene(int index) const -> SceneInfo
{
    SceneInfo info;
    const auto status = m_pool.StatusOf(index);
    info.state = status.state;
    info.lane = status.lane;
    if (index >= 0 && index < static_cast<int>(m_performance.scenes.size()))
    {
        const auto path = m_performance.scenes[static_cast<size_t>(index)].presetPath.string();
        if (auto it = m_liveMsByPreset.find(path); it != m_liveMsByPreset.end())
        {
            info.frameMs = it->second;
            info.measuredLive = true;
        }
        else if (auto warm = m_frameMsByPreset.find(path); warm != m_frameMsByPreset.end())
        {
            info.frameMs = warm->second;
        }
        info.heavy = info.frameMs > kFrameBudgetMs;
        if (auto ticks = m_liveTicksByScene.find(index); ticks != m_liveTicksByScene.end() && ticks->second > 60)
        {
            const auto held = m_heldByScene.find(index);
            info.droppedFraction = held == m_heldByScene.end() ? 0.0 : static_cast<double>(held->second) / static_cast<double>(ticks->second);
        }
        if (auto it = m_errorByPreset.find(path); it != m_errorByPreset.end())
        {
            info.error = it->second;
        }
    }
    return info;
}

auto Engine::PresetMeasurements() const -> std::vector<PresetMeasurement>
{
    std::map<std::string, PresetMeasurement> byPath;
    for (const auto& [path, ms] : m_frameMsByPreset)
        byPath[path] = {path, ms, {}};
    for (const auto& [path, ms] : m_liveMsByPreset)
        byPath[path] = {path, ms, {}};
    for (const auto& [path, error] : m_errorByPreset)
        byPath[path] = {path, 0.0, error};
    std::vector<PresetMeasurement> result;
    for (auto& [path, measurement] : byPath)
        result.push_back(std::move(measurement));
    return result;
}

auto Engine::GetStats() const -> Stats
{
    Stats stats;
    stats.ticks = m_tick;
    stats.presentedFrames = m_presentedFrames;
    stats.heldTicks = m_heldTicks;
    stats.heldWaitingTicks = m_heldWaitingTicks;
    stats.heldNearCutTicks = m_heldNearCutTicks;
    stats.heldByScene = m_heldByScene;
    stats.warmDone = m_warmDone;
    stats.warmTotal = m_warmTotal;
    stats.audioLevel = m_audioLevel;
    for (const auto& lane : m_lanes)
    {
        stats.laneQueue.push_back(lane->QueuedJobs());
        stats.laneRenderMs.push_back(lane->LiveRenderMs());
        stats.laneGpuMs.push_back(lane->LiveGpuMs());
    }
    return stats;
}

void Engine::Execute(std::vector<PoolCommand> commands)
{
    for (size_t i = 0; i < commands.size(); ++i)
    {
        const auto command = commands[i];
        if (command.lane < 0 || command.lane >= static_cast<int>(m_lanes.size()))
        {
            continue;
        }
        Lane& lane = *m_lanes[static_cast<size_t>(command.lane)];
        switch (command.type)
        {
            case PoolCommand::Type::Prepare:
            {
                const auto& scene = m_performance.scenes[static_cast<size_t>(command.scene)];
                VideoSettings video{scene.video.enabled, scene.video.mix, scene.video.lumaMin, scene.video.contrast, scene.video.saturation};
                lane.Prepare(command.instance, command.scene, scene.presetPath.string(), video, command.priority);
            }
                break;
            case PoolCommand::Type::Release:
                if (command.instance == m_liveInstance)
                {
                    // The scene being left: it keeps rendering until the next one's first frame is presented.
                    m_handoverRelease = {command.lane, command.instance, command.scene};
                    break;
                }
                lane.Release(command.instance);
                break;
            case PoolCommand::Type::Activate: {
                FinishHandover(); // a cut during a handover: the older scene stops now
                if (m_handoverRelease.instance != 0 && m_handoverRelease.lane != command.lane)
                {
                    m_handover = m_handoverRelease; // keeps rendering on its own lane
                }
                else if (m_handoverRelease.instance != 0)
                {
                    // Same lane: it can only render one instance, so no handover.
                    lane.Release(m_handoverRelease.instance);
                }
                m_handoverRelease = {};
                lane.SetLive(command.instance);
                m_lastActivateTick = m_tick;
                m_liveLane = command.lane;
                m_liveInstance = command.instance;
                GV_LOG_INFO(kLogCategory, "scene %d (%s) live on lane %d", command.scene,
                            m_performance.scenes[static_cast<size_t>(command.scene)].DisplayName().c_str(), command.lane);

                // The live lane must only render: move its queued preparation work elsewhere.
                auto [prepares, warms] = lane.CancelQueued();
                for (const auto& [scene, path] : warms)
                {
                    QueueWarm(scene, path, command.lane);
                }
                if (!prepares.empty())
                {
                    auto replanned = m_pool.OnCancelled(prepares);
                    commands.insert(commands.end(), replanned.begin(), replanned.end());
                }
                break;
            }
        }
    }
}

void Engine::HandleEvent(const LaneEvent& event)
{
    if (event.ok)
    {
        const bool firstMeasurement = m_frameMsByPreset.count(event.presetPath) == 0;
        m_frameMsByPreset[event.presetPath] = event.frameMs;
        m_errorByPreset.erase(event.presetPath);
        if (firstMeasurement && event.frameMs > kFrameBudgetMs)
        {
            GV_LOG_WARN(kLogCategory, "heavy preset (%.0f ms/frame, can't reach 60 fps): %s", event.frameMs, event.presetPath.c_str());
        }
    }
    else
    {
        m_errorByPreset[event.presetPath] = event.error;
    }

    if (event.type == LaneEvent::Type::Warmed)
    {
        m_warmDone++;
        if (m_warmDone == m_warmTotal)
        {
            GV_LOG_INFO(kLogCategory, "pre-warm finished (%d presets)", m_warmTotal);
        }
        return;
    }
    Execute(m_pool.OnPrepared(event.instance, event.ok));
}

void Engine::FinishHandover()
{
    if (m_handover.instance == 0)
    {
        return;
    }
    Lane& lane = *m_lanes[static_cast<size_t>(m_handover.lane)];
    lane.SetLive(0);
    lane.Release(m_handover.instance);
    m_handover = {};
}

void Engine::QueueWarm(int scene, const std::string& path, int excludeLane)
{
    const int lanes = static_cast<int>(m_lanes.size());
    for (int attempt = 0; attempt < lanes; ++attempt)
    {
        const int lane = (m_nextWarmLane + attempt) % lanes;
        if (lane != excludeLane && lane != m_liveLane)
        {
            m_lanes[static_cast<size_t>(lane)]->Warm(scene, path);
            m_nextWarmLane = (lane + 1) % lanes;
            return;
        }
    }
}

void Engine::CreatePresentTargets()
{
    glGenTextures(1, &m_presentedTexture);
    glBindTexture(GL_TEXTURE_2D, m_presentedTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &m_presentFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_presentFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_presentedTexture, 0);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glGenFramebuffers(1, &m_readFbo);
    glFlush();
}

} // namespace gv::engine
