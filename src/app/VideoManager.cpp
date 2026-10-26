#include "app/VideoManager.hpp"

#include "common/log.hpp"

#include <set>

namespace gv::app {

namespace {

constexpr const char* kLogCategory = "video";
constexpr long long kFenceLifetimeFrames = 8; //!< Lanes wait on a fence within a frame or two; keep a margin.
constexpr int kPlayBehind = 1;                //!< Matches the scene pool window, so upcoming videos are already decoding.
constexpr int kPlayAhead = 3;

auto KeyFor(const model::Scene& scene) -> std::string
{
    if (!scene.video.enabled)
        return {};
    const auto& source = scene.video.source;
    if (source.type == model::VideoSourceType::File)
        return "file:" + source.path.string();
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
    Clear();
    m_performance = performance;
    for (const auto& scene : m_performance.scenes)
    {
        const std::string key = KeyFor(scene);
        m_sceneKeys.push_back(key);
        if (key.empty() || m_sources.count(key) > 0)
            continue;

        const auto& source = scene.video.source;
        SourceEntry entry;
        entry.source = source.type == model::VideoSourceType::File
                           ? io::CreateFileVideoSource(source.path)
                           : io::CreateSyphonVideoSource(source.appName, source.serverName);
        m_sources.emplace(key, std::move(entry));
    }
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

    // Decode only what's live or about to be: the pool window around the live (or first) scene,
    // plus a pending out-of-order trigger and the outgoing scene of a handover.
    std::set<std::string> wanted;
    const int anchor = liveScene >= 0 ? liveScene : 0;
    for (int scene = anchor - kPlayBehind; scene <= anchor + kPlayAhead; ++scene)
    {
        if (scene >= 0 && scene < static_cast<int>(m_sceneKeys.size()) && !m_sceneKeys[static_cast<size_t>(scene)].empty())
            wanted.insert(m_sceneKeys[static_cast<size_t>(scene)]);
    }
    for (int scene : {pendingScene, handoverScene})
    {
        if (scene >= 0 && scene < static_cast<int>(m_sceneKeys.size()) && !m_sceneKeys[static_cast<size_t>(scene)].empty())
            wanted.insert(m_sceneKeys[static_cast<size_t>(scene)]);
    }
    for (auto& [key, entry] : m_sources)
    {
        entry.source->SetActive(wanted.count(key) > 0);
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
        entry->source->SetActive(true);
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
    // Several scenes (live + handover) may share a source: convert once per frame.
    if (entry->updatedFrame != m_frame)
    {
        entry->updatedFrame = m_frame;
        const GLuint texture = entry->source->Update();
        GLsync fence = nullptr;
        if (texture != 0)
        {
            fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            glFlush();
            m_fences.emplace_back(m_frame, fence);
        }
        entry->frame = {texture, fence};
    }
    return entry->frame;
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
