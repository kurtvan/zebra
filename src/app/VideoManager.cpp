#include "app/VideoManager.hpp"

#include "common/log.hpp"

#include <set>

namespace gv::app {

namespace {

constexpr const char* kLogCategory = "video";
constexpr long long kFenceLifetimeFrames = 8; //!< Lanes wait on a fence within a frame or two; keep a margin.
constexpr int kWarmBehind = 1; //!< Matches the scene pool window, so upcoming videos have a frame ready.
constexpr int kWarmAhead = 3;

auto FileKey(const std::string& videoId) -> std::string
{
    return "file:" + videoId;
}

auto ToIo(model::VideoFit fit) -> io::VideoFit
{
    switch (fit)
    {
        case model::VideoFit::Fill:
            return io::VideoFit::Fill;
        case model::VideoFit::Fit:
            return io::VideoFit::Fit;
        case model::VideoFit::Stretch:
            return io::VideoFit::Stretch;
    }
    return io::VideoFit::Fill;
}

/// The sources a scene shows: its pool videos that exist (several for a video playlist), or its Syphon server.
auto KeysFor(const model::Performance& performance, const model::Scene& scene) -> std::vector<std::string>
{
    if (!scene.video.enabled)
        return {};
    const auto& source = scene.video.source;
    if (source.type == model::VideoSourceType::Syphon)
        return {"syphon:" + source.appName + "\n" + source.serverName};
    std::vector<std::string> keys;
    for (const auto& id : scene.VideoIds())
    {
        if (performance.FindVideo(id) != nullptr)
            keys.push_back(FileKey(id));
    }
    return keys;
}

} // namespace

VideoManager::~VideoManager()
{
    Clear();
}

void VideoManager::Clear()
{
    for (auto& [frame, fence] : m_fences)
        glDeleteSync(fence);
    m_fences.clear();
    m_sources.clear();
    m_scenes.clear();
}

void VideoManager::Load(const model::Performance& performance)
{
    // Keep sources that are still used (so an edit doesn't restart a playing video); create new ones; drop the rest.
    // Every pool video gets a player, so the Videos panel can show its length and scenes switching to it
    // carry on from where it was.
    // A scene keeps its place in its video playlist through edits, as long as the video on screen is still in it.
    std::map<std::string, SceneVideos> previous;
    for (size_t i = 0; i < m_performance.scenes.size() && i < m_scenes.size(); ++i)
        previous[m_performance.scenes[i].id] = m_scenes[i];
    m_performance = performance;
    m_scenes.clear();
    std::map<std::string, SourceEntry> sources;
    auto keep = [&](const std::string& key) {
        if (auto existing = m_sources.find(key); existing != m_sources.end())
        {
            sources.emplace(key, std::move(existing->second));
            return true;
        }
        return false;
    };
    for (const auto& video : m_performance.videos)
    {
        const std::string key = FileKey(video.id);
        if (sources.count(key) > 0)
            continue;
        if (!keep(key))
        {
            SourceEntry entry;
            entry.source = io::CreateFileVideoSource(video.path);
            sources.emplace(key, std::move(entry));
        }
        sources[key].source->SetPlayback(video.startSeconds, video.speed);
        if (m_canvasWidth > 0)
            sources[key].source->SetLayout(ToIo(video.fit), m_canvasWidth, m_canvasHeight);
    }
    for (const auto& scene : m_performance.scenes)
    {
        SceneVideos videos;
        videos.keys = KeysFor(m_performance, scene);
        if (auto old = previous.find(scene.id); old != previous.end() && old->second.Key() != nullptr)
        {
            const auto now = std::find(videos.keys.begin(), videos.keys.end(), *old->second.Key());
            if (old->second.keys == videos.keys)
                videos = old->second;
            else if (now != videos.keys.end())
                videos.item = static_cast<size_t>(now - videos.keys.begin()); // the next one is planned again
        }
        m_scenes.push_back(videos);
        if (scene.video.source.type != model::VideoSourceType::Syphon || videos.keys.empty())
            continue;
        const std::string& key = videos.keys.front();
        if (sources.count(key) > 0 || keep(key))
            continue;
        SourceEntry entry;
        entry.source = io::CreateSyphonVideoSource(scene.video.source.appName, scene.video.source.serverName);
        sources.emplace(key, std::move(entry));
    }
    m_sources = std::move(sources);
    GV_LOG_INFO(kLogCategory, "%zu video source(s) for %zu scenes", m_sources.size(), m_performance.scenes.size());
}

void VideoManager::BeginFrame(int liveScene, int pendingScene, int handoverScene, const engine::ClockInput& clock)
{
    m_frame++;
    while (!m_fences.empty() && m_fences.front().first < m_frame - kFenceLifetimeFrames)
    {
        glDeleteSync(m_fences.front().second);
        m_fences.pop_front();
    }

    // Only videos on screen play (the live scene, a pending out-of-order trigger and the outgoing scene of
    // a handover), so a video that isn't showing keeps its place for the next scene that uses it. Videos
    // of the scenes around the live one stay paused but keep their current frame converted, so a cut to
    // them shows video straight away.
    auto keyOf = [&](int scene) -> const std::string* {
        if (scene < 0 || scene >= static_cast<int>(m_scenes.size()))
            return nullptr;
        return m_scenes[static_cast<size_t>(scene)].Key();
    };
    auto onScreenKeys = [&] {
        std::set<std::string> keys;
        for (int scene : {liveScene, pendingScene, handoverScene})
        {
            if (const auto* key = keyOf(scene))
                keys.insert(*key);
        }
        return keys;
    };

    // Scenes that left the screen go back to their first video, for the next time they're triggered.
    for (int scene = 0; scene < static_cast<int>(m_scenes.size()); ++scene)
    {
        auto& videos = m_scenes[static_cast<size_t>(scene)];
        if (scene != liveScene && scene != pendingScene && scene != handoverScene && (videos.item != 0 || videos.next >= 0))
            videos = SceneVideos{videos.keys};
    }
    if (liveScene >= 0 && liveScene < static_cast<int>(m_scenes.size()))
        CycleVideos(liveScene, onScreenKeys(), clock);

    const std::set<std::string> playing = onScreenKeys();
    for (auto& [key, entry] : m_sources)
    {
        entry.source->SetActive(playing.count(key) > 0);
    }
    const int anchor = liveScene >= 0 ? liveScene : 0;
    for (int scene = anchor - kWarmBehind; scene <= anchor + kWarmAhead; ++scene)
    {
        if (const auto* key = keyOf(scene); key != nullptr && playing.count(*key) == 0)
            UpdateEntry(m_sources[*key]);
    }
    // The live video playlist's next video: its first frame converted, ready for the cut.
    if (liveScene >= 0 && liveScene < static_cast<int>(m_scenes.size()))
    {
        const auto& videos = m_scenes[static_cast<size_t>(liveScene)];
        if (videos.next >= 0 && videos.nextReady)
            UpdateEntry(m_sources[videos.keys[static_cast<size_t>(videos.next)]]);
    }
}

void VideoManager::CycleVideos(int liveScene, const std::set<std::string>& onScreen, const engine::ClockInput& clock)
{
    const auto& scene = m_performance.scenes[static_cast<size_t>(liveScene)];
    auto& videos = m_scenes[static_cast<size_t>(liveScene)];
    const auto now = engine::CycleTimer::Clock::now();
    if (scene.id != m_cycleScene)
    {
        m_cycleScene = scene.id;
        m_timer.Restart(now);
    }
    if (!scene.IsVideoPlaylist() || videos.keys.size() < 2)
        return;
    if (videos.next < 0)
        PlanNext(videos, scene, onScreen);

    const auto& playlist = scene.video.playlist;
    if (videos.next < 0 || !m_timer.Tick({playlist.unit, playlist.intervalSeconds, playlist.intervalCount}, now, clock))
        return;
    // Each video plays from its start point. One that couldn't wait there (it was on screen) jumps now.
    auto& next = m_sources[videos.keys[static_cast<size_t>(videos.next)]];
    if (!videos.nextReady)
        next.source->Restart();
    videos.item = static_cast<size_t>(videos.next);
    videos.next = -1;
    videos.nextReady = false;
    GV_LOG_INFO(kLogCategory, "video playlist: scene %d (%s) now on video %zu of %zu: %s", liveScene, scene.DisplayName().c_str(), videos.item + 1,
                videos.keys.size(), next.source->Describe().c_str());
    std::set<std::string> nowOnScreen = onScreen;
    nowOnScreen.insert(*videos.Key());
    PlanNext(videos, scene, nowOnScreen);
}

void VideoManager::PlanNext(SceneVideos& videos, const model::Scene& scene, const std::set<std::string>& onScreen)
{
    videos.next = engine::NextEntry(static_cast<int>(videos.item), static_cast<int>(videos.keys.size()), scene.video.playlist.shuffle, m_random);
    videos.nextReady = false;
    if (videos.next < 0)
        return;
    // Wait at the start point, paused, unless it's on screen right now (then it jumps at the change).
    const std::string& key = videos.keys[static_cast<size_t>(videos.next)];
    if (onScreen.count(key) == 0)
    {
        m_sources[key].source->Restart();
        videos.nextReady = true;
    }
}

void VideoManager::SetCanvasSize(int width, int height)
{
    if (width == m_canvasWidth && height == m_canvasHeight)
        return;
    m_canvasWidth = width;
    m_canvasHeight = height;
    for (const auto& video : m_performance.videos)
    {
        if (auto source = m_sources.find(FileKey(video.id)); source != m_sources.end())
            source->second.source->SetLayout(ToIo(video.fit), width, height);
    }
}

void VideoManager::OnTrigger(int scene, int liveScene)
{
    if (scene == liveScene || scene < 0 || scene >= static_cast<int>(m_performance.scenes.size()))
        return;
    // A scene starts on its first video.
    auto& videos = m_scenes[static_cast<size_t>(scene)];
    videos = SceneVideos{videos.keys};
    if (!m_performance.scenes[static_cast<size_t>(scene)].video.restartOnEnter)
        return;
    if (auto* entry = EntryFor(scene))
    {
        entry->source->Restart();
    }
}

auto VideoManager::FrameFor(int scene) -> engine::VideoFrame
{
    SourceEntry* entry = EntryFor(scene);
    if (entry == nullptr)
    {
        return {};
    }
    return UpdateEntry(*entry);
}

auto VideoManager::UpdateEntry(SourceEntry& entry) -> engine::VideoFrame
{
    // Several scenes (live + handover) may share a source: convert once per frame.
    if (entry.updatedFrame != m_frame)
    {
        entry.updatedFrame = m_frame;
        const GLuint texture = entry.source->Update();
        GLsync fence = nullptr;
        if (texture != 0)
        {
            fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            glFlush();
            m_fences.emplace_back(m_frame, fence);
        }
        entry.frame = {texture, fence};
    }
    return entry.frame;
}

auto VideoManager::StateFor(int scene) const -> std::optional<io::VideoState>
{
    if (const auto* entry = EntryFor(scene))
        return entry->source->State();
    return std::nullopt;
}

auto VideoManager::VideoItemFor(int scene) const -> int
{
    if (scene < 0 || scene >= static_cast<int>(m_scenes.size()))
        return 0;
    return static_cast<int>(m_scenes[static_cast<size_t>(scene)].item);
}

auto VideoManager::DescribeFor(int scene) const -> std::string
{
    if (const auto* entry = EntryFor(scene))
        return entry->source->Describe();
    return {};
}

auto VideoManager::DurationOf(const std::string& videoId) const -> double
{
    auto it = m_sources.find(FileKey(videoId));
    return it == m_sources.end() ? 0.0 : it->second.source->DurationSeconds();
}

auto VideoManager::PositionOf(const std::string& videoId) const -> double
{
    auto it = m_sources.find(FileKey(videoId));
    return it == m_sources.end() ? 0.0 : it->second.source->PositionSeconds();
}

auto VideoManager::StateOf(const std::string& videoId) const -> std::optional<io::VideoState>
{
    auto it = m_sources.find(FileKey(videoId));
    if (it == m_sources.end())
        return std::nullopt;
    return it->second.source->State();
}

auto VideoManager::EntryFor(int scene) -> SourceEntry*
{
    if (scene < 0 || scene >= static_cast<int>(m_scenes.size()) || m_scenes[static_cast<size_t>(scene)].Key() == nullptr)
        return nullptr;
    auto it = m_sources.find(*m_scenes[static_cast<size_t>(scene)].Key());
    return it == m_sources.end() ? nullptr : &it->second;
}

auto VideoManager::EntryFor(int scene) const -> const SourceEntry*
{
    return const_cast<VideoManager*>(this)->EntryFor(scene);
}

} // namespace gv::app
