#pragma once

#include "engine/Engine.hpp"
#include "io/VideoSource.hpp"
#include "model/Performance.hpp"

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gv::app {

/// Owns the performance's video sources and hands the engine the live scenes' frames.
///
/// Scenes using the same file or Syphon server share one source. Main thread only, with the editor's GL
/// context current (the sources convert frames on it; lanes see the textures through the share group).
class VideoManager final : public engine::VideoProvider
{
public:
    VideoManager() = default;
    ~VideoManager() override;

    VideoManager(const VideoManager&) = delete;
    auto operator=(const VideoManager&) -> VideoManager& = delete;

    /// Creates sources for every scene with video enabled; releases the previous performance's.
    void Load(const model::Performance& performance);

    /// Once per frame, before the engine's Tick: plays the sources of the scenes around `liveScene`
    /// (and `pendingScene`), pauses the rest, and drops old fences.
    void BeginFrame(int liveScene, int pendingScene, int handoverScene);

    /// Called just before a scene is triggered: restarts its video if the scene asks for that.
    void OnTrigger(int scene, int liveScene);

    // engine::VideoProvider
    auto FrameFor(int scene) -> engine::VideoFrame override;

    auto StateFor(int scene) const -> std::optional<io::VideoState>;
    auto DescribeFor(int scene) const -> std::string;

private:
    struct SourceEntry
    {
        std::unique_ptr<io::VideoSource> source;
        long long updatedFrame{-1};
        engine::VideoFrame frame;
    };

    auto EntryFor(int scene) -> SourceEntry*;
    auto EntryFor(int scene) const -> const SourceEntry*;
    void Clear();

    model::Performance m_performance;
    std::map<std::string, SourceEntry> m_sources;
    std::vector<std::string> m_sceneKeys; //!< Per scene: key into m_sources, or empty.
    long long m_frame{0};
    std::deque<std::pair<long long, GLsync>> m_fences;
};

} // namespace gv::app
