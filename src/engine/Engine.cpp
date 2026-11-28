#include "engine/Engine.hpp"

#include "common/log.hpp"
#include "engine/BeatGrid.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <span>

namespace gv::engine {

namespace {

constexpr const char* kLogCategory = "engine";
constexpr double kFrameBudgetMs = 1000.0 / 60.0;
constexpr int kVideoNearbyBehind = 1; //!< Scenes around the live one whose first video keeps a frame ready
constexpr int kVideoNearbyAhead = 3;  //!< (the scene pool's window).

/// The video `plan` shows in `scene`, with its source node's loop setting. Nullopt: no video.
auto VideoOf(const model::Scene& scene, const RenderPlan& plan) -> std::optional<VideoUse>
{
    const model::Node* node = plan.sourceNode.empty() ? nullptr : scene.graph.Find(plan.sourceNode);
    if (node == nullptr)
        return std::nullopt;
    return VideoUse{plan.source, model::ParamsOf<model::VideoSourceParams>(*node).loop};
}

/// The inputs of `switcher` with a wire in, ascending.
auto WiredInputs(const model::Graph& graph, const model::Node& switcher) -> std::vector<int>
{
    std::vector<int> inputs;
    for (int input = 0; input < model::ParamsOf<model::SwitcherParams>(switcher).inputCount; ++input)
    {
        if (graph.WireInto(switcher.id, input) != nullptr)
            inputs.push_back(input);
    }
    return inputs;
}

/// The entries of `scene` that can be prepared: a file chosen, and found.
auto UsableEntry(const model::Scene& scene, size_t entry) -> bool
{
    return entry < scene.presetEntries.size() && !scene.presetEntries[entry].path.empty() &&
           !(entry < scene.presetsMissing.size() && scene.presetsMissing[entry]);
}

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
        unusable.push_back(!Playable(scene));
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
            if (!UsableEntry(scene, item))
                continue;
            const auto path = presets[item].string();
            if (queued.insert(path).second && m_frameMsByPreset.count(path) == 0)
            {
                QueueWarm(i, path, -1);
                m_warmTotal++;
            }
        }
    }

    m_switchers = {};
    m_livePlan = {};
    m_liveVideo.reset();
    m_entryPlans.clear();
    for (const auto& scene : m_performance.scenes)
        m_entryPlans.push_back(CompilePlan(scene.graph, SelectionForItem(scene.graph, 0)));

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
    std::vector<bool> liveEntriesChanged; // by entry of the live scene: it now loads another preset
    for (const auto& scene : performance.scenes)
        unusable.push_back(!Playable(scene));

    for (int old = 0; old < static_cast<int>(m_performance.scenes.size()); ++old)
    {
        const auto& before = m_performance.scenes[static_cast<size_t>(old)];
        auto it = newIndexById.find(before.id);
        if (it == newIndexById.end())
            continue;
        oldToNew[static_cast<size_t>(old)] = it->second;
        const auto& after = performance.scenes[static_cast<size_t>(it->second)];
        presetChanged[static_cast<size_t>(it->second)] = NeedsNewInstances(before, after);

        // The live scene's preset switcher got other presets: its next one is planned again, from the
        // one on screen. (Timing edits just apply: the clock reads them every frame.)
        if (old == m_pool.LiveScene() && (before.presetEntries != after.presetEntries || before.presetSwitcher != after.presetSwitcher))
        {
            Execute(m_pool.ClearItem());
            m_switchers.presetInstance = 0;
        }
        if (old == m_pool.LiveScene())
        {
            const auto was = before.Presets();
            const auto now = after.Presets();
            for (size_t item = 0; item < now.size(); ++item)
                liveEntriesChanged.push_back((item >= was.size() || was[item] != now[item]) && UsableEntry(after, item));
        }

        // Video changes apply to the existing instances (including the live one) right away: each gets the
        // plan of its preset entry, compiled from the edited graph, when it differs from the one it has.
        if (!presetChanged[static_cast<size_t>(it->second)] && before.graph != after.graph)
        {
            for (const auto& hosted : m_pool.InstancesOf(old))
            {
                LanePlan plan = PlanFor(old, after, hosted.item);
                if (plan != PlanFor(old, before, hosted.item))
                    m_lanes[static_cast<size_t>(hosted.lane)]->SetPlan(hosted.instance, std::move(plan));
            }
            if (m_handover.instance != 0 && m_handover.scene == old && !after.SwitchesPresets())
            {
                LanePlan plan = PlanFor(old, after, 0);
                if (plan != PlanFor(old, before, 0))
                    m_lanes[static_cast<size_t>(m_handover.lane)]->SetPlan(m_handover.instance, std::move(plan));
            }
        }
    }

    if (m_handover.scene >= 0)
        m_handover.scene = oldToNew[static_cast<size_t>(m_handover.scene)];
    if (m_switchers.scene >= 0)
        m_switchers.scene = oldToNew[static_cast<size_t>(m_switchers.scene)];
    m_performance = performance;
    m_entryPlans.clear();
    for (const auto& scene : m_performance.scenes)
        m_entryPlans.push_back(CompilePlan(scene.graph, SelectionForItem(scene.graph, 0)));
    Execute(m_pool.Remap(static_cast<int>(performance.scenes.size()), oldToNew, unusable, presetChanged));

    // The live scene is on another entry whose preset changed: it's reloaded there (the old one holds until
    // the new one is ready). A stale prepared next entry is prepared again.
    if (const int live = m_pool.LiveScene(); live >= 0 && !liveEntriesChanged.empty())
    {
        const bool wasJumping = m_pool.JumpPending();
        Execute(m_pool.ReloadItems(liveEntriesChanged));
        if (!wasJumping && m_pool.JumpPending())
        {
            const auto& scene = m_performance.scenes[static_cast<size_t>(live)];
            const size_t item = static_cast<size_t>(m_pool.LiveItem()) < scene.presetEntries.size() ? static_cast<size_t>(m_pool.LiveItem()) : 0;
            GV_LOG_INFO(kLogCategory, "scene %d (%s): preset %zu of %zu changed while live; cutting to it when it's ready", live,
                        scene.DisplayName().c_str(), item + 1, scene.presetEntries.size());
        }
    }

    // Pre-warm presets that are new to this session (including every preset entry).
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

auto Engine::SelectionFor(int index, const model::Scene& scene, int item) const -> SwitcherSelection
{
    SwitcherSelection selection = SelectionForItem(scene.graph, item);
    if (index == m_switchers.scene)
    {
        for (const auto& [id, clock] : m_switchers.clocks)
        {
            if (id != scene.presetSwitcher && clock.Current() >= 0)
                selection[id] = clock.Current();
        }
    }
    return selection;
}

auto Engine::SignalInputsNow(int scene) const -> model::SignalInputs
{
    model::SignalInputs inputs;
    if (m_clock.receiving && m_clock.running)
        inputs.beats = static_cast<double>(m_clock.positionKnown ? m_clock.songPulses : m_clock.totalPulses) / static_cast<double>(kPulsesPerBeat);
    inputs.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_signalStart).count();
    if (scene >= 0 && scene < static_cast<int>(m_performance.scenes.size()))
    {
        if (const auto it = m_sceneAudio.find(m_performance.scenes[static_cast<size_t>(scene)].id); it != m_sceneAudio.end())
            inputs.levels = it->second.Levels();
    }
    return inputs;
}

void Engine::FollowAudio(const std::vector<float>& pcm)
{
    // What moves each scene's Band and Envelope settings: its numbers as of the last tick's levels.
    std::vector<model::SignalValues> signals;
    for (int i = 0; i < static_cast<int>(m_performance.scenes.size()); ++i)
        signals.push_back(model::EvaluateSignals(m_performance.scenes[static_cast<size_t>(i)].graph, SignalInputsNow(i)));
    std::map<std::string, SceneAudio> kept; // only the scenes still there
    for (size_t i = 0; i < m_performance.scenes.size(); ++i)
    {
        const auto& scene = m_performance.scenes[i];
        SceneAudio& audio = kept[scene.id] = std::move(m_sceneAudio[scene.id]);
        audio.Process(scene.graph, pcm, m_sampleRate, signals[i]);
    }
    m_sceneAudio = std::move(kept);
}

namespace {

/// LanePlan::audio of preset `preset` in scene `scene` (by id: scene indices change with edits).
auto AudioKey(const std::string& scene, const std::string& preset) -> std::string
{
    return scene + "/" + preset;
}

} // namespace

auto Engine::HeardAudioNow(const PcmChunk& pcm, bool input, const std::vector<float>& hit) const -> std::pair<HeardAudio, HeardAudio>
{
    HeardAudio heard;
    HeardAudio live; // the live scene's, with the hit
    const int liveScene = m_pool.LiveScene();
    PcmChunk silence;
    for (int i = 0; i < static_cast<int>(m_performance.scenes.size()); ++i)
    {
        const auto& scene = m_performance.scenes[static_cast<size_t>(i)];
        const auto audio = m_sceneAudio.find(scene.id);
        if (audio == m_sceneAudio.end())
            continue;
        for (const auto& [preset, samples] : audio->second.PresetAudio())
        {
            const std::string key = AudioKey(scene.id, preset);
            if (!samples)
            {
                // Nothing wired: silence, so it doesn't go on with the last audio it heard.
                if (input)
                    heard[key] = silence ? silence : (silence = std::make_shared<const std::vector<float>>(pcm->size(), 0.f));
                continue;
            }
            const PcmChunk chunk = samples->data() == pcm->data() ? pcm : std::make_shared<const std::vector<float>>(samples->begin(), samples->end());
            if (input)
                heard[key] = chunk;
            if (i == liveScene && !hit.empty())
            {
                auto mixed = std::make_shared<std::vector<float>>(*chunk);
                for (size_t s = 0; s < mixed->size() && s < hit.size(); ++s)
                    (*mixed)[s] += hit[s];
                live[key] = std::move(mixed);
            }
        }
    }
    return {std::move(heard), std::move(live)};
}

auto Engine::PlanFor(int index, const model::Scene& scene, int item) const -> LanePlan
{
    const RenderProgram program = CompileProgram(scene.graph, SelectionFor(index, scene, item));
    return LanePlanOf(program, program.Top() != nullptr ? AudioKey(scene.id, program.Top()->presetNode) : std::string());
}

void Engine::RefreshLivePlans()
{
    const int live = m_switchers.scene;
    if (live < 0 || live >= static_cast<int>(m_performance.scenes.size()))
        return;
    const auto& scene = m_performance.scenes[static_cast<size_t>(live)];
    for (const auto& hosted : m_pool.InstancesOf(live))
        m_lanes[static_cast<size_t>(hosted.lane)]->SetPlan(hosted.instance, PlanFor(live, scene, hosted.item));
}

void Engine::ReloadChangedShaders()
{
    const auto now = std::chrono::steady_clock::now();
    if (now - m_shadersPolled < std::chrono::milliseconds(250))
        return;
    m_shadersPolled = now;
    auto shaderPaths = [](const model::Scene& scene) {
        std::set<std::filesystem::path> paths;
        for (const auto& node : scene.graph.nodes)
        {
            if (node.type == model::NodeType::IsfShader && !model::ParamsOf<model::IsfShaderParams>(node).path.empty())
                paths.insert(model::ParamsOf<model::IsfShaderParams>(node).path);
        }
        return paths;
    };
    std::set<std::filesystem::path> watched;
    for (const auto& scene : m_performance.scenes)
        watched.merge(shaderPaths(scene));
    const auto changed = m_shaderFiles.Poll(watched);
    if (changed.empty())
        return;

    // Plans hold the parsed file, so a new one compiles on the lanes as a plan change does (between frames).
    for (int index = 0; index < static_cast<int>(m_performance.scenes.size()); ++index)
    {
        const auto& scene = m_performance.scenes[static_cast<size_t>(index)];
        const auto paths = shaderPaths(scene);
        if (std::none_of(changed.begin(), changed.end(), [&](const auto& path) { return paths.contains(path); }))
            continue;
        GV_LOG_INFO(kLogCategory, "scene %d (%s): an ISF shader file changed, reloading it", index, scene.DisplayName().c_str());
        for (const auto& hosted : m_pool.InstancesOf(index))
            m_lanes[static_cast<size_t>(hosted.lane)]->SetPlan(hosted.instance, PlanFor(index, scene, hosted.item));
        if (m_handover.instance != 0 && m_handover.scene == index && !scene.SwitchesPresets())
            m_lanes[static_cast<size_t>(m_handover.lane)]->SetPlan(m_handover.instance, PlanFor(index, scene, 0));
    }
}

void Engine::JumpVideo(int index, const std::string& switcherId, int input)
{
    if (index != m_pool.LiveScene() || index != m_switchers.scene)
        return;
    const auto& scene = m_performance.scenes[static_cast<size_t>(index)];
    auto clock = m_switchers.clocks.find(switcherId);
    if (clock == m_switchers.clocks.end() || switcherId == scene.presetSwitcher || scene.graph.WireInto(switcherId, input) == nullptr)
        return;
    clock->second.Start(input, SwitcherClock::Clock::now());
    m_videoJumped = true;
    GV_LOG_INFO(kLogCategory, "scene %d (%s): video switcher %s jumped to input %d", index, scene.DisplayName().c_str(), switcherId.c_str(), input + 1);
}

void Engine::ResetShader(int index, const std::string& nodeId)
{
    for (const auto& hosted : m_pool.InstancesOf(index))
        m_lanes[static_cast<size_t>(hosted.lane)]->ResetShader(hosted.instance, nodeId);
    if (m_handover.instance != 0 && m_handover.scene == index)
        m_lanes[static_cast<size_t>(m_handover.lane)]->ResetShader(m_handover.instance, nodeId);
}

auto Engine::Switcher(int index, const std::string& switcherId) const -> std::optional<SwitcherState>
{
    if (index < 0 || index != m_switchers.scene || index != m_pool.LiveScene())
        return std::nullopt;
    const auto clock = m_switchers.clocks.find(switcherId);
    const model::Node* node = m_performance.scenes[static_cast<size_t>(index)].graph.Find(switcherId);
    if (clock == m_switchers.clocks.end() || node == nullptr || node->type != model::NodeType::Switcher)
        return std::nullopt;
    const auto& params = model::ParamsOf<model::SwitcherParams>(*node);
    SwitcherState info;
    info.passing = clock->second.Current();
    info.next = clock->second.Next();
    if (params.unit == model::IntervalUnit::Seconds)
        info.secondsToNext = clock->second.SecondsToNext(params, SwitcherClock::Clock::now());
    else if (!(m_clock.receiving && m_clock.running) || clock->second.BeatsToNext() < 0)
        info.waitingForClock = true;
    else
        info.beatsToNext = clock->second.BeatsToNext();
    return info;
}

auto Engine::VideoSourceOf(int index) const -> std::optional<model::VideoSource>
{
    if (index < 0 || index >= static_cast<int>(m_performance.scenes.size()))
        return std::nullopt;
    const RenderPlan& plan = index == m_pool.LiveScene() && index == m_switchers.scene ? m_livePlan : m_entryPlans[static_cast<size_t>(index)];
    if (plan.sourceNode.empty())
        return std::nullopt;
    return plan.source;
}

void Engine::TriggerItem(int index, int item)
{
    if (index < 0 || index >= static_cast<int>(m_performance.scenes.size()))
        return;
    const auto& scene = m_performance.scenes[static_cast<size_t>(index)];
    if (!scene.SwitchesPresets() || item < 0 || !UsableEntry(scene, static_cast<size_t>(item)))
    {
        Trigger(index);
        return;
    }
    const auto status = m_pool.StatusOf(index).state;
    if (status == ScenePool::State::Unusable || status == ScenePool::State::Failed)
    {
        Trigger(index); // logs why
        return;
    }
    GV_LOG_INFO(kLogCategory, "scene %d (%s): going to preset %d of %zu", index, scene.DisplayName().c_str(), item + 1, scene.presetEntries.size());
    Execute(m_pool.TriggerItem(index, item));
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
        const auto& scene = m_performance.scenes[static_cast<size_t>(index)];
        GV_LOG_WARN(kLogCategory, "scene %d (%s) can't be played: %s", index, scene.DisplayName().c_str(),
                    status == ScenePool::State::Failed ? "preset failed to load"
                    : scene.presetMissing             ? "preset file missing"
                                                      : "nothing is wired to the output");
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

    // Without input audio this frame, the audio nodes follow a frame's worth of silence.
    const PcmChunk pcm = chunk ? chunk : std::make_shared<const std::vector<float>>(static_cast<size_t>(m_sampleRate / 60) * 2, 0.f);
    FollowAudio(*pcm);
    UpdateSwitchers();
    ReloadChangedShaders();
    if (m_videoProvider != nullptr)
        m_videoProvider->BeginFrame(Needs());

    // Beat hits: a transient on the clock's beats, mixed into what the live scene's presets hear through their wires.
    const int liveScene = m_pool.LiveScene();
    const model::BeatHit* beatHit =
        liveScene >= 0 && liveScene < static_cast<int>(m_performance.scenes.size()) ? &m_performance.scenes[static_cast<size_t>(liveScene)].beatHit : nullptr;
    if (beatHit != nullptr && beatHit->enabled)
    {
        if (m_beatHitClock.Advance(m_clock, BeatHitInterval(beatHit->every)))
            m_beatHitSynth.Trigger(beatHit->sound, beatHit->strength);
    }
    else
    {
        m_beatHitClock.Reset();
        m_beatHitSynth.Stop();
    }
    std::vector<float> hit;
    if (m_beatHitSynth.Active())
    {
        // Without input audio this frame, the hit goes into a frame's worth of silence.
        hit.assign(pcm->size(), 0.f);
        m_beatHitSynth.MixInto(hit.data(), hit.size() / 2, m_sampleRate);
    }
    // Without input audio and a hit, the presets hear nothing new (they aren't fed silence).
    const auto [heard, liveHeard] = HeardAudioNow(pcm, chunk != nullptr, hit);

    // Video for the live scene and, during a handover, the outgoing one; other lanes only take PCM.
    for (int i = 0; i < static_cast<int>(m_lanes.size()); ++i)
    {
        VideoFrame video;
        if (m_videoProvider != nullptr)
        {
            if (i == m_liveLane && liveScene >= 0 && !m_livePlan.sourceNode.empty())
                video = m_videoProvider->FrameFor(m_livePlan.source);
            else if (i == m_handover.lane && m_handover.video)
                video = m_videoProvider->FrameFor(m_handover.video->source);
        }
        model::SignalValues signals;
        if (i == m_liveLane && liveScene >= 0 && liveScene < static_cast<int>(m_performance.scenes.size()))
            signals = model::EvaluateSignals(m_performance.scenes[static_cast<size_t>(liveScene)].graph, SignalInputsNow(liveScene));
        HeardAudio audio = heard;
        if (i == m_liveLane)
        {
            for (const auto& [key, mixed] : liveHeard)
                audio[key] = mixed;
        }
        m_lanes[static_cast<size_t>(i)]->Tick(std::move(audio), m_tick, video, std::move(signals));
    }

    // Once a scene has been live for half a second, its lane's average render time replaces the
    // (noisy, single-frame) warm-up estimate.
    const int live = m_pool.LiveScene();
    if (live >= 0)
    {
        m_liveTicksByScene[live]++;
    }
    // (A picture with no preset has nothing to measure by preset.)
    if (m_liveLane >= 0 && live >= 0 && m_tick - m_lastActivateTick > 30 && !CurrentPreset(live).empty())
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
        const auto& scene = m_performance.scenes[static_cast<size_t>(index)];
        if (scene.SwitchesPresets())
        {
            if (const auto running = Switcher(index, scene.presetSwitcher))
            {
                info.playlistItem = m_pool.LiveItem();
                info.secondsToNextItem = running->secondsToNext;
                info.beatsToNextItem = running->beatsToNext;
                info.waitingForClock = running->waitingForClock;
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
                const auto presets = scene.Presets();
                const auto& preset = presets[static_cast<size_t>(std::clamp(command.item, 0, static_cast<int>(presets.size()) - 1))];
                lane.Prepare(command.instance, command.scene, preset.string(), PlanFor(command.scene, scene, command.item), command.priority);
            }
                break;
            case PoolCommand::Type::Release:
                if (command.instance == m_liveInstance)
                {
                    // The scene being left: it keeps rendering until the next one's first frame is presented.
                    // (The scene itself may be gone: deleted while live.)
                    m_handoverRelease = {command.lane, command.instance, command.scene, m_liveVideo};
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
    if (event.type == LaneEvent::Type::ShaderCompiled)
    {
        if (event.ok)
            m_errorByShader.erase(event.shaderPath);
        else
            m_errorByShader[event.shaderPath] = event.error;
        return;
    }
    if (event.presetPath.empty())
    {
        // A picture with no preset: nothing to measure or report by preset.
    }
    else if (event.ok)
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

void Engine::UpdateSwitchers()
{
    m_switchedTo.clear();
    m_upcoming.reset();
    const int live = m_pool.LiveScene();
    if (live < 0 || live >= static_cast<int>(m_performance.scenes.size()))
    {
        if (m_pool.NextItemInstance() != 0)
            Execute(m_pool.ClearItem());
        m_switchers = {};
        m_livePlan = {};
        m_liveVideo.reset();
        return;
    }
    const auto& scene = m_performance.scenes[static_cast<size_t>(live)];
    const auto now = SwitcherClock::Clock::now();
    if (m_switchers.scene != live)
    {
        // Entered: every switcher starts over.
        m_switchers = {};
        m_switchers.scene = live;
    }

    // A jump to a preset entry is about to go live; the preset switcher starts over from it then.
    if (!m_pool.JumpPending())
        UpdatePresetSwitcher(scene, live, now);
    const bool jumped = std::exchange(m_videoJumped, false);
    const bool moved = UpdateVideoSwitchers(scene, now);

    // A video switcher that moved on changes the live scene's video (and maybe its effects): every
    // instance of the scene gets the new plan, and the new video plays from its start point.
    const auto before = m_livePlan;
    m_livePlan = CompilePlan(scene.graph, SelectionFor(live, scene, m_pool.LiveItem()));
    m_liveVideo = VideoOf(scene, m_livePlan);
    if (jumped || moved)
    {
        RefreshLivePlans();
        if (!m_livePlan.sourceNode.empty() && (jumped || m_livePlan.source != before.source))
            m_switchedTo.push_back(m_livePlan.source);
    }

    // The next video of a video switcher on the live path: what the plan would show after it moves on.
    for (const auto& [id, clock] : m_switchers.clocks)
    {
        if (id == scene.presetSwitcher || clock.Next() < 0)
            continue;
        auto selection = SelectionFor(live, scene, m_pool.LiveItem());
        selection[id] = clock.Next();
        const RenderPlan next = CompilePlan(scene.graph, selection);
        if (!next.sourceNode.empty() && next.source != m_livePlan.source)
        {
            m_upcoming = next.source;
            break;
        }
    }
}

void Engine::UpdatePresetSwitcher(const model::Scene& scene, int index, SwitcherClock::Clock::time_point now)
{
    const model::Node* node = scene.SwitchesPresets() ? scene.graph.Find(scene.presetSwitcher) : nullptr;
    if (node == nullptr || scene.presetEntries.size() < 2)
    {
        if (m_pool.NextItemInstance() != 0)
            Execute(m_pool.ClearItem());
        if (node == nullptr)
            m_switchers.clocks.erase(scene.presetSwitcher);
        m_switchers.preparedInput = -1;
        return;
    }
    const auto& params = model::ParamsOf<model::SwitcherParams>(*node);
    auto& clock = m_switchers.clocks[node->id];

    // Entered (or re-entered) the scene, jumped, or something else replaced its instance: start over
    // from the entry on screen.
    if (m_switchers.presetInstance != m_pool.LiveInstance() || clock.Current() < 0)
    {
        Execute(m_pool.ClearItem());
        const int item = std::clamp(m_pool.LiveItem(), 0, static_cast<int>(scene.presetEntries.size()) - 1);
        clock.Start(scene.presetEntries[static_cast<size_t>(item)].input, now);
        m_switchers.presetInstance = m_pool.LiveInstance();
        m_switchers.preparedInput = -1;
    }

    // Prepare the next entry in the background on a non-live lane (again if the plan changed).
    std::vector<int> inputs;
    for (size_t entry = 0; entry < scene.presetEntries.size(); ++entry)
    {
        if (UsableEntry(scene, entry))
            inputs.push_back(scene.presetEntries[entry].input);
    }
    const int next = clock.PlanNext(params, inputs, m_random);
    if (next != m_switchers.preparedInput || (next >= 0 && m_pool.NextItemInstance() == 0))
    {
        if (m_pool.NextItemInstance() != 0)
            Execute(m_pool.ClearItem());
        m_switchers.preparedInput = next;
        if (next >= 0)
            Execute(m_pool.PrepareItem(index, scene.EntryOfInput(next)));
    }

    // Due and the next entry is ready: cut to it (if it isn't ready yet, the current one keeps playing).
    const auto step = clock.Tick(params, m_pool.IsReady(m_pool.NextItemInstance()), now, m_clock);
    const long long pulses = m_clock.positionKnown ? m_clock.songPulses : m_clock.totalPulses;
    if (step == SwitcherClock::Step::MissedBarLine)
    {
        GV_LOG_INFO(kLogCategory, "preset switcher: next preset of scene %d wasn't ready on the bar line, waiting for the next bar", index);
    }
    else if (step == SwitcherClock::Step::Switch)
    {
        const int item = scene.EntryOfInput(clock.Current());
        Execute(m_pool.ActivateItem(m_pool.NextItemInstance()));
        m_switchers.presetInstance = m_pool.LiveInstance();
        m_switchers.preparedInput = -1;
        const auto& path = scene.presetEntries[static_cast<size_t>(std::max(item, 0))].path;
        if (params.unit != model::IntervalUnit::Seconds)
            GV_LOG_INFO(kLogCategory, "preset switcher: scene %d (%s) now on preset %d of %zu at beat %.2f: %s", index, scene.DisplayName().c_str(), item + 1,
                        scene.presetEntries.size(), static_cast<double>(pulses) / kPulsesPerBeat, path.filename().c_str());
        else
            GV_LOG_INFO(kLogCategory, "preset switcher: scene %d (%s) now on preset %d of %zu: %s", index, scene.DisplayName().c_str(), item + 1,
                        scene.presetEntries.size(), path.filename().c_str());
    }
}

auto Engine::UpdateVideoSwitchers(const model::Scene& scene, SwitcherClock::Clock::time_point now) -> bool
{
    bool moved = false;
    std::set<std::string> present;
    for (const auto& node : scene.graph.nodes)
    {
        if (node.type != model::NodeType::Switcher || node.id == scene.presetSwitcher)
            continue;
        const auto& params = model::ParamsOf<model::SwitcherParams>(node);
        const auto inputs = WiredInputs(scene.graph, node);
        if (!model::SwitchesVideos(scene.graph, node) || inputs.empty())
            continue;
        present.insert(node.id);
        auto& clock = m_switchers.clocks[node.id];
        if (std::find(inputs.begin(), inputs.end(), clock.Current()) == inputs.end())
            clock.Start(inputs.front(), now); // new, or its input was unwired
        clock.PlanNext(params, inputs, m_random);
        // A video can always switch: the next one waits at its start point, or jumps there.
        if (clock.Tick(params, true, now, m_clock) == SwitcherClock::Step::Switch)
        {
            moved = true;
            GV_LOG_INFO(kLogCategory, "video switcher: scene %d (%s) %s now on input %d", m_switchers.scene, scene.DisplayName().c_str(), node.id.c_str(),
                        clock.Current() + 1);
        }
    }
    std::erase_if(m_switchers.clocks, [&](const auto& entry) { return entry.first != scene.presetSwitcher && present.count(entry.first) == 0; });
    return moved;
}

auto Engine::Needs() const -> VideoNeeds
{
    VideoNeeds needs;
    if (m_handover.instance != 0 && m_handover.video)
        needs.onScreen.push_back(*m_handover.video);
    const int pending = m_pool.PendingScene();
    if (pending >= 0 && pending < static_cast<int>(m_entryPlans.size()))
    {
        if (auto video = VideoOf(m_performance.scenes[static_cast<size_t>(pending)], m_entryPlans[static_cast<size_t>(pending)]))
            needs.onScreen.push_back(*video);
    }
    const int live = m_pool.LiveScene();
    if (live >= 0 && live == m_switchers.scene && m_liveVideo)
        needs.onScreen.push_back(*m_liveVideo);
    needs.upcoming = m_upcoming;
    needs.switchedTo = m_switchedTo;
    const int anchor = std::max(live, 0);
    for (int scene = anchor - kVideoNearbyBehind; scene <= anchor + kVideoNearbyAhead; ++scene)
    {
        if (scene >= 0 && scene < static_cast<int>(m_entryPlans.size()) && scene != live && !m_entryPlans[static_cast<size_t>(scene)].sourceNode.empty())
            needs.nearby.push_back(m_entryPlans[static_cast<size_t>(scene)].source);
    }
    return needs;
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
