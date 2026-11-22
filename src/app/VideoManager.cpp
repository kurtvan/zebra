#include "app/VideoManager.hpp"

#include "common/log.hpp"
#include "engine/RenderPlan.hpp"

#include <set>

namespace gv::app {

namespace {

constexpr const char* kLogCategory = "video";
constexpr long long kFenceLifetimeFrames = 8; //!< Lanes wait on a fence within a frame or two; keep a margin.

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
}

auto VideoManager::KeyOf(const model::VideoSource& source) -> std::string
{
    if (source.type == model::VideoSourceType::Syphon)
        return "syphon:" + source.appName + "\n" + source.serverName;
    return source.videoId.empty() ? std::string() : FileKey(source.videoId);
}

void VideoManager::Load(const model::Performance& performance)
{
    // Keep sources that are still used (so an edit doesn't restart a playing video); create new ones; drop
    // the rest. Every pool video gets a player, so the Videos panel can show its length and scenes switching
    // to it carry on from where it was.
    m_performance = performance;
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
        for (const auto& node : scene.graph.nodes)
        {
            if (node.type != model::NodeType::VideoSource)
                continue;
            const auto& source = model::ParamsOf<model::VideoSourceParams>(node).source;
            if (source.type != model::VideoSourceType::Syphon)
                continue;
            const std::string key = KeyOf(source);
            if (sources.count(key) > 0 || keep(key))
                continue;
            SourceEntry entry;
            entry.source = io::CreateSyphonVideoSource(source.appName, source.serverName);
            sources.emplace(key, std::move(entry));
        }
    }
    m_sources = std::move(sources);
    GV_LOG_INFO(kLogCategory, "%zu video source(s) for %zu scenes", m_sources.size(), m_performance.scenes.size());
}

void VideoManager::BeginFrame(const engine::VideoNeeds& needs)
{
    m_frame++;
    while (!m_fences.empty() && m_fences.front().first < m_frame - kFenceLifetimeFrames)
    {
        glDeleteSync(m_fences.front().second);
        m_fences.pop_front();
    }

    std::set<std::string> playing;
    for (const auto& use : needs.onScreen)
        playing.insert(KeyOf(use.source));

    // Switched to: each plays from its start point. The one waiting there is ready; any other (it was on
    // screen when it was planned, so it couldn't wait) jumps there now.
    for (const auto& source : needs.switchedTo)
    {
        const std::string key = KeyOf(source);
        SourceEntry* entry = Find(source);
        if (entry != nullptr && !(key == m_upcoming && m_upcomingReady))
            entry->source->Restart();
        if (entry != nullptr)
            GV_LOG_INFO(kLogCategory, "video switcher: now on %s", entry->source->Describe().c_str());
        if (key == m_upcoming)
            m_upcoming.clear();
    }

    // The next video waits at its start point, paused, unless it's on screen right now.
    const std::string upcoming = needs.upcoming ? KeyOf(*needs.upcoming) : std::string();
    if (upcoming != m_upcoming)
    {
        m_upcoming = upcoming;
        m_upcomingReady = false;
        if (SourceEntry* entry = needs.upcoming ? Find(*needs.upcoming) : nullptr; entry != nullptr && playing.count(upcoming) == 0)
        {
            entry->source->Restart();
            m_upcomingReady = true;
        }
    }

    // Only videos on screen play, so a video that isn't showing keeps its place for the next scene that
    // uses it. A shared video loops or not as the scene showing it says; the live scene comes last and wins.
    for (auto& [key, entry] : m_sources)
        entry.source->SetActive(playing.count(key) > 0);
    for (const auto& use : needs.onScreen)
    {
        if (SourceEntry* entry = Find(use.source))
            entry->source->SetLoop(use.loop);
    }

    // The videos of the scenes around the live one, and the next video, stay paused but keep their current
    // frame converted, so a cut to them shows video straight away.
    for (const auto& source : needs.nearby)
    {
        if (SourceEntry* entry = Find(source); entry != nullptr && playing.count(KeyOf(source)) == 0)
            UpdateEntry(*entry);
    }
    if (needs.upcoming && m_upcomingReady)
    {
        if (SourceEntry* entry = Find(*needs.upcoming))
            UpdateEntry(*entry);
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

void VideoManager::OnTrigger(int scene, int item, int liveScene)
{
    if (scene == liveScene || scene < 0 || scene >= static_cast<int>(m_performance.scenes.size()))
        return;
    // The video the scene starts with, from its start point if its source node says so.
    const auto& graph = m_performance.scenes[static_cast<size_t>(scene)].graph;
    const auto plan = engine::CompilePlan(graph, engine::SelectionForItem(graph, item));
    const model::Node* node = plan.sourceNode.empty() ? nullptr : graph.Find(plan.sourceNode);
    if (node == nullptr || !model::ParamsOf<model::VideoSourceParams>(*node).restartOnEnter)
        return;
    if (SourceEntry* entry = Find(plan.source))
        entry->source->Restart();
}

auto VideoManager::FrameFor(const model::VideoSource& source) -> engine::VideoFrame
{
    SourceEntry* entry = Find(source);
    return entry == nullptr ? engine::VideoFrame{} : UpdateEntry(*entry);
}

auto VideoManager::UpdateEntry(SourceEntry& entry) -> engine::VideoFrame
{
    // Several lanes (live + handover) may share a source: convert once per frame.
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

auto VideoManager::StateOf(const model::VideoSource& source) const -> std::optional<io::VideoState>
{
    if (const auto* entry = Find(source))
        return entry->source->State();
    return std::nullopt;
}

auto VideoManager::Describe(const model::VideoSource& source) const -> std::string
{
    if (const auto* entry = Find(source))
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

auto VideoManager::Find(const model::VideoSource& source) -> SourceEntry*
{
    const std::string key = KeyOf(source);
    auto it = key.empty() ? m_sources.end() : m_sources.find(key);
    return it == m_sources.end() ? nullptr : &it->second;
}

auto VideoManager::Find(const model::VideoSource& source) const -> const SourceEntry*
{
    return const_cast<VideoManager*>(this)->Find(source);
}

} // namespace gv::app
