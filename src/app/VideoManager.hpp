#pragma once

#include "engine/Engine.hpp"
#include "io/VideoSource.hpp"
#include "model/Performance.hpp"

#include <deque>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

namespace gv::app {

/// Owns the performance's video sources and gives the engine their frames.
///
/// There is one source per pool video and per Syphon server, shared by every scene and node using it: a
/// video that's already playing carries on from the same place unless something restarts it. The engine
/// decides what plays (engine::VideoNeeds): the videos on screen play, the rest pause where they are; the
/// live scene's next video (a video switcher's) waits paused at its start point, so the change is a clean
/// cut. Main thread only, with the editor's GL context current (the sources convert frames on it; lanes see
/// the textures through the share group).
class VideoManager final : public engine::VideoProvider
{
public:
    VideoManager() = default;
    ~VideoManager() override;

    VideoManager(const VideoManager&) = delete;
    auto operator=(const VideoManager&) -> VideoManager& = delete;

    /// Creates sources for every pool video and every Syphon server a video source node uses, keeping
    /// sources that are still in use and releasing the rest. Also used after edits.
    void Load(const model::Performance& performance);

    /// The output canvas size, which file videos are fitted to. Cheap when unchanged; call every frame.
    void SetCanvasSize(int width, int height);

    /// Called just before scene `scene` is triggered at preset entry `item`: restarts the video it starts
    /// with, if its video source node asks for that.
    void OnTrigger(int scene, int item, int liveScene);

    // engine::VideoProvider
    void BeginFrame(const engine::VideoNeeds& needs) override;
    auto FrameFor(const model::VideoSource& source) -> engine::VideoFrame override;

    /// A video source's playback and description, for the scene list and the canvas. Nullopt / empty if
    /// there's no such source (no video chosen, or not in the pool).
    auto StateOf(const model::VideoSource& source) const -> std::optional<io::VideoState>;
    auto Describe(const model::VideoSource& source) const -> std::string;

    /// Pool video playback, for the Videos panel. 0 / nullopt if not known.
    auto DurationOf(const std::string& videoId) const -> double;
    auto PositionOf(const std::string& videoId) const -> double;
    auto StateOf(const std::string& videoId) const -> std::optional<io::VideoState>;

private:
    struct SourceEntry
    {
        std::unique_ptr<io::VideoSource> source;
        long long updatedFrame{-1};
        engine::VideoFrame frame;
    };

    /// "file:<id>" for a pool video, "syphon:<app>\n<server>"; empty for a file source with no video.
    static auto KeyOf(const model::VideoSource& source) -> std::string;
    auto Find(const model::VideoSource& source) -> SourceEntry*;
    auto Find(const model::VideoSource& source) const -> const SourceEntry*;
    auto UpdateEntry(SourceEntry& entry) -> engine::VideoFrame;
    void Clear();

    model::Performance m_performance;
    std::map<std::string, SourceEntry> m_sources;
    std::string m_upcoming;      //!< Key of the live scene's next video.
    bool m_upcomingReady{false}; //!< It's waiting at its start point (it wasn't on screen when planned).
    long long m_frame{0};
    int m_canvasWidth{0};
    int m_canvasHeight{0};
    std::deque<std::pair<long long, GLsync>> m_fences;
};

} // namespace gv::app
