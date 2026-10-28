#pragma once

#include "engine/CycleTimer.hpp"
#include "engine/Engine.hpp"
#include "io/VideoSource.hpp"
#include "model/Performance.hpp"

#include <deque>
#include <map>
#include <set>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace gv::app {

/// Owns the performance's video sources and hands the engine the live scenes' frames.
///
/// There is one source per pool video and per Syphon server, shared by every scene using it: a scene
/// whose video is already playing carries on from the same place unless the scene restarts it. A scene
/// with a video playlist cycles through its videos while it's live, each from its start point; the next
/// one waits paused at its start point, so the change is a clean cut. Main thread only, with the editor's GL
/// context current (the sources convert frames on it; lanes see the textures through the share group).
class VideoManager final : public engine::VideoProvider
{
public:
    VideoManager() = default;
    ~VideoManager() override;

    VideoManager(const VideoManager&) = delete;
    auto operator=(const VideoManager&) -> VideoManager& = delete;

    /// Creates sources for every scene with video enabled, keeping sources that are still in use and
    /// releasing the rest. Also used after edits.
    void Load(const model::Performance& performance);

    /// Once per frame, before the engine's Tick: moves the live scene's video playlist on when it's due
    /// (by `clock` for beats/bars), plays the sources on screen (`liveScene`, `pendingScene`,
    /// `handoverScene`), pauses the rest, keeps a frame ready for the scenes around the live one and the
    /// live playlist's next video, and drops old fences.
    void BeginFrame(int liveScene, int pendingScene, int handoverScene, const engine::ClockInput& clock);

    /// The output canvas size, which file videos are fitted to. Cheap when unchanged; call every frame.
    void SetCanvasSize(int width, int height);

    /// Called just before a scene is triggered: restarts its video if the scene asks for that.
    void OnTrigger(int scene, int liveScene);

    // engine::VideoProvider
    auto FrameFor(int scene) -> engine::VideoFrame override;

    auto StateFor(int scene) const -> std::optional<io::VideoState>;
    /// The video playlist entry a scene shows (0 when it has no video playlist).
    auto VideoItemFor(int scene) const -> int;
    auto DescribeFor(int scene) const -> std::string;

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

    /// The videos a scene shows, and which of them is on screen.
    struct SceneVideos
    {
        std::vector<std::string> keys; //!< Into m_sources: one per video (several for a video playlist); empty: no video.
        size_t item{0};                //!< The one on screen.
        int next{-1};                  //!< Video playlists: the one coming up.
        bool nextReady{false};         //!< The next one is waiting at its start point.

        auto Key() const -> const std::string*
        {
            return keys.empty() ? nullptr : &keys[std::min(item, keys.size() - 1)];
        }
    };

    void CycleVideos(int liveScene, const std::set<std::string>& onScreen, const engine::ClockInput& clock);
    void PlanNext(SceneVideos& videos, const model::Scene& scene, const std::set<std::string>& onScreen);
    auto UpdateEntry(SourceEntry& entry) -> engine::VideoFrame;
    auto EntryFor(int scene) -> SourceEntry*;
    auto EntryFor(int scene) const -> const SourceEntry*;
    void Clear();

    model::Performance m_performance;
    std::map<std::string, SourceEntry> m_sources;
    std::vector<SceneVideos> m_scenes; //!< Per scene.
    std::string m_cycleScene;          //!< Id of the scene whose video playlist m_timer times.
    engine::CycleTimer m_timer;
    std::mt19937 m_random{std::random_device{}()};
    long long m_frame{0};
    int m_canvasWidth{0};
    int m_canvasHeight{0};
    std::deque<std::pair<long long, GLsync>> m_fences;
};

} // namespace gv::app
