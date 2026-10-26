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

auto KeyFor(const model::Performance& performance, const model::Scene& scene) -> std::string
{
    if (!scene.video.enabled)
        return {};
    const auto& source = scene.video.source;
    if (source.type == model::VideoSourceType::File)
        return performance.VideoFor(scene) != nullptr ? FileKey(source.videoId) : std::string{};
    return "syphon:" + source.appName + "\n" + source.serverName;
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
    m_sceneKeys.clear();
}

void VideoManager::Load(const model::Performance& performance)
{
    // Keep sources that are still used (so an edit doesn't restart a playing video); create new ones; drop the rest.
    // Every pool video gets a player, so the Videos panel can show its length and scenes switching to it
    // carry on from where it was.
    m_performance = performance;
    m_sceneKeys.clear();
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
    }
    for (const auto& scene : m_performance.scenes)
    {
        const std::string key = KeyFor(m_performance, scene);
        m_sceneKeys.push_back(key);
        if (key.empty() || sources.count(key) > 0 || keep(key))
            continue;
        SourceEntry entry;
        entry.source = io::CreateSyphonVideoSource(scene.video.source.appName, scene.video.source.serverName);
        sources.emplace(key, std::move(entry));
    }
    m_sources = std::move(sources);
    GV_LOG_INFO(kLogCategory, "%zu video source(s) for %zu scenes", m_sources.size(), m_performance.scenes.size());
}

void VideoManager::BeginFrame(int liveScene, int pendingScene, int handoverScene)
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
        if (scene < 0 || scene >= static_cast<int>(m_sceneKeys.size()) || m_sceneKeys[static_cast<size_t>(scene)].empty())
            return nullptr;
        return &m_sceneKeys[static_cast<size_t>(scene)];
    };
    std::set<std::string> playing;
    for (int scene : {liveScene, pendingScene, handoverScene})
    {
        if (const auto* key = keyOf(scene))
            playing.insert(*key);
    }
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
}

void VideoManager::OnTrigger(int scene, int liveScene)
{
    if (scene == liveScene || scene < 0 || scene >= static_cast<int>(m_performance.scenes.size()))
        return;
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
    if (scene < 0 || scene >= static_cast<int>(m_sceneKeys.size()) || m_sceneKeys[static_cast<size_t>(scene)].empty())
        return nullptr;
    auto it = m_sources.find(m_sceneKeys[static_cast<size_t>(scene)]);
    return it == m_sources.end() ? nullptr : &it->second;
}

auto VideoManager::EntryFor(int scene) const -> const SourceEntry*
{
    return const_cast<VideoManager*>(this)->EntryFor(scene);
}

} // namespace gv::app
