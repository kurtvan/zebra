#include "engine/Engine.hpp"

#include "common/log.hpp"
#include "engine/BeatGrid.hpp"

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
        unusable.push_back(scene.presetMissing || scene.FirstPreset().empty());
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
        if (unusable[static_cast<size_t>(i)])
        {
            continue;
        }
        const auto presets = scene.Presets();
        for (size_t item = 0; item < presets.size(); ++item)
        {
            // Entry 0 of scenes in the initial window is being prepared already.
            if (item == 0 && m_pool.StatusOf(i).state != ScenePool::State::None)
                continue;
            if (item < scene.playlistMissing.size() && scene.IsPlaylist() && scene.playlistMissing[item])
                continue;
            const auto path = presets[item].string();
            if (queued.insert(path).second && m_frameMsByPreset.count(path) == 0)
            {
                QueueWarm(i, path, -1);
                m_warmTotal++;
            }
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
        unusable.push_back(scene.presetMissing || scene.FirstPreset().empty());

    for (int old = 0; old < static_cast<int>(m_performance.scenes.size()); ++old)
    {
        const auto& before = m_performance.scenes[static_cast<size_t>(old)];
        auto it = newIndexById.find(before.id);
        if (it == newIndexById.end())
            continue;
        oldToNew[static_cast<size_t>(old)] = it->second;
        const auto& after = performance.scenes[static_cast<size_t>(it->second)];
        presetChanged[static_cast<size_t>(it->second)] = before.FirstPreset() != after.FirstPreset();

        // An edited playlist on the live scene: restart its cycle (the next entry is planned again).
        if (old == m_pool.LiveScene() &&
            (before.IsPlaylist() != after.IsPlaylist() || before.playlist.presets != after.playlist.presets ||
             before.playlist.intervalSeconds != after.playlist.intervalSeconds || before.playlist.shuffle != after.playlist.shuffle ||
             before.playlist.unit != after.playlist.unit || before.playlist.intervalCount != after.playlist.intervalCount))
        {
            Execute(m_pool.ClearItem());
            m_playlist = {};
        }

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

    // Pre-warm presets that are new to this session (including playlist entries).
    std::set<std::string> queued;
    for (int i = 0; i < static_cast<int>(performance.scenes.size()); ++i)
    {
        if (unusable[static_cast<size_t>(i)])
            continue;
        const auto presets = performance.scenes[static_cast<size_t>(i)].Presets();
        for (size_t item = 0; item < presets.size(); ++item)
        {
            const auto path = presets[item].string();
            if (item == 0 && m_pool.StatusOf(i).state != ScenePool::State::None)
                continue;
            if (m_frameMsByPreset.count(path) == 0 && m_errorByPreset.count(path) == 0 && queued.insert(path).second)
            {
                QueueWarm(i, path, -1);
                m_warmTotal++;
            }
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

    UpdatePlaylist();

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
        const auto path = CurrentPreset(live).string();
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
        const auto path = (index == m_pool.LiveScene() ? CurrentPreset(index) : m_performance.scenes[static_cast<size_t>(index)].FirstPreset()).string();
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
        if (index == m_pool.LiveScene() && m_playlist.scene == index)
        {
            const auto& playlist = m_performance.scenes[static_cast<size_t>(index)].playlist;
            info.playlistItem = m_playlist.item;
            if (playlist.unit == model::IntervalUnit::Seconds)
            {
                info.secondsToNextItem = std::max(0.0, std::chrono::duration<double>(m_playlist.switchAt - std::chrono::steady_clock::now()).count());
            }
            else if (!(m_clock.receiving && m_clock.running) || m_playlist.switchPulse < 0)
            {
                info.waitingForClock = true;
            }
            else
            {
                info.beatsToNextItem = std::max(0.0, static_cast<double>(m_playlist.switchPulse - m_playlist.lastPulse) / kPulsesPerBeat);
            }
        }
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
                const auto presets = scene.Presets();
                const auto& preset = presets[static_cast<size_t>(std::clamp(command.item, 0, static_cast<int>(presets.size()) - 1))];
                lane.Prepare(command.instance, command.scene, preset.string(), video, command.priority);
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

auto Engine::CurrentPreset(int scene) const -> std::filesystem::path
{
    if (scene < 0 || scene >= static_cast<int>(m_performance.scenes.size()))
        return {};
    const auto presets = m_performance.scenes[static_cast<size_t>(scene)].Presets();
    const int item = scene == m_pool.LiveScene() ? m_pool.LiveItem() : 0;
    return presets[static_cast<size_t>(std::clamp(item, 0, static_cast<int>(presets.size()) - 1))];
}

auto Engine::PickNextItem(const model::Scene& scene, int current) -> int
{
    const int count = static_cast<int>(scene.playlist.presets.size());
    auto usable = [&](int item) {
        return item != current && !(item < static_cast<int>(scene.playlistMissing.size()) && scene.playlistMissing[static_cast<size_t>(item)]);
    };
    if (scene.playlist.shuffle)
    {
        std::vector<int> candidates;
        for (int item = 0; item < count; ++item)
        {
            if (usable(item))
                candidates.push_back(item);
        }
        if (candidates.empty())
            return -1;
        return candidates[std::uniform_int_distribution<size_t>(0, candidates.size() - 1)(m_random)];
    }
    for (int step = 1; step < count; ++step)
    {
        const int item = (current + step) % count;
        if (usable(item))
            return item;
    }
    return -1;
}

void Engine::UpdatePlaylist()
{
    using Clock = std::chrono::steady_clock;
    const int live = m_pool.LiveScene();
    const model::Scene* scene = live >= 0 && live < static_cast<int>(m_performance.scenes.size()) ? &m_performance.scenes[static_cast<size_t>(live)] : nullptr;
    if (scene == nullptr || !scene->IsPlaylist() || scene->playlist.presets.size() < 2)
    {
        if (m_pool.NextItemInstance() != 0)
            Execute(m_pool.ClearItem());
        m_playlist = {};
        return;
    }

    const auto& playlist = scene->playlist;
    const auto now = Clock::now();
    const auto interval = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(playlist.intervalSeconds));
    const bool countsBeats = playlist.unit != model::IntervalUnit::Seconds;
    const long long grid = playlist.unit == model::IntervalUnit::Bars ? kPulsesPerBar : kPulsesPerBeat;
    const long long intervalPulses = grid * std::max(1, playlist.intervalCount);
    const bool clockRunning = m_clock.receiving && m_clock.running;
    // Ableton's song position when we have it (cycles land on its bar lines), else pulses since entry.
    const bool songGrid = m_clock.positionKnown;
    const long long pulses = songGrid ? m_clock.songPulses : m_clock.totalPulses;

    // Entered (or re-entered) the scene, or something else replaced its instance: start the cycle over.
    if (m_playlist.scene != live || m_playlist.liveInstance != m_pool.LiveInstance())
    {
        Execute(m_pool.ClearItem());
        m_playlist = {};
        m_playlist.scene = live;
        m_playlist.liveInstance = m_pool.LiveInstance();
        m_playlist.item = m_pool.LiveItem();
        m_playlist.switchAt = now + interval;
    }

    // Beats/bars: (re)anchor the cycle to the grid when the clock starts, jumps (Start, song position) or the
    // counter changes; holds while the clock is stopped.
    if (countsBeats && clockRunning &&
        (m_playlist.switchPulse < 0 || m_playlist.songGrid != songGrid || pulses < m_playlist.lastPulse ||
         pulses > m_playlist.switchPulse + intervalPulses))
    {
        m_playlist.songGrid = songGrid;
        m_playlist.switchPulse = (songGrid ? CycleStart(pulses, grid) : pulses) + intervalPulses;
    }
    m_playlist.lastPulse = pulses;

    // Prepare the next entry in the background on a non-live lane.
    if (m_pool.NextItemInstance() == 0)
    {
        m_playlist.nextItem = PickNextItem(*scene, m_playlist.item);
        if (m_playlist.nextItem >= 0)
            Execute(m_pool.PrepareItem(live, m_playlist.nextItem));
    }

    // Due and the next entry is ready: cut to it (if it isn't ready yet, the current one keeps playing).
    const bool due = countsBeats ? (clockRunning && m_playlist.switchPulse >= 0 && pulses >= m_playlist.switchPulse) : now >= m_playlist.switchAt;
    if (due && m_pool.IsReady(m_pool.NextItemInstance()))
    {
        const int item = m_playlist.nextItem;
        Execute(m_pool.ActivateItem(m_pool.NextItemInstance()));
        m_playlist.liveInstance = m_pool.LiveInstance();
        m_playlist.item = item;
        m_playlist.nextItem = -1;
        m_playlist.switchAt = now + interval;
        if (countsBeats)
            m_playlist.switchPulse += intervalPulses; // stay on the grid
        if (countsBeats)
            GV_LOG_INFO(kLogCategory, "playlist: scene %d (%s) now on preset %d of %zu at beat %.2f: %s", live, scene->DisplayName().c_str(), item + 1,
                        playlist.presets.size(), static_cast<double>(pulses) / kPulsesPerBeat, playlist.presets[static_cast<size_t>(item)].filename().c_str());
        else
            GV_LOG_INFO(kLogCategory, "playlist: scene %d (%s) now on preset %d of %zu: %s", live, scene->DisplayName().c_str(), item + 1,
                        playlist.presets.size(), playlist.presets[static_cast<size_t>(item)].filename().c_str());
    }
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
