#pragma once

#include "engine/Lane.hpp"
#include "engine/ScenePool.hpp"
#include "model/Performance.hpp"

#include <SDL.h>

#include <OpenGL/gl3.h>

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gv::engine {

/// Supplies the current video frame of a scene (implemented by the app, which owns the video sources).
class VideoProvider
{
public:
    virtual ~VideoProvider() = default;
    virtual auto FrameFor(int scene) -> VideoFrame = 0;
};

/// Playback engine: lanes, the scene pool, PCM fan-out and the presented frame.
///
/// All methods are called on the main thread. Lanes report back through a queue drained in Tick().
class Engine
{
public:
    struct SceneInfo
    {
        ScenePool::State state{ScenePool::State::None};
        int lane{-1};
        double frameMs{0.0};       //!< Render time per frame: live average if measuredLive, else the warm-up estimate; 0 if unknown.
        bool measuredLive{false};
        double droppedFraction{-1.0}; //!< Share of live ticks without a new frame; -1 until live for a second.
        bool heavy{false};   //!< frameMs over the 60 fps budget.
        std::string error;   //!< Load error, if the preset failed.
    };

    struct Stats
    {
        long long ticks{0};
        long long presentedFrames{0};
        long long heldTicks{0};        //!< Ticks with no new live frame while a scene was live.
        long long heldWaitingTicks{0}; //!< Of those, ticks spent waiting for a triggered scene to be prepared.
        long long heldNearCutTicks{0}; //!< Of those, other ticks within 0.5 s after a cut (when lanes are re-preparing).
        std::map<int, long long> heldByScene; //!< Held ticks by the scene that was live.
        int warmDone{0};
        int warmTotal{0};
        float audioLevel{0.f}; //!< RMS of the last tick's samples after gain.
        std::vector<size_t> laneQueue;
        std::vector<double> laneRenderMs;
        std::vector<double> laneGpuMs;
    };

    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    auto operator=(const Engine&) -> Engine& = delete;

    /// Creates `laneCount` lanes whose contexts share with the current GL context. Call with that context current.
    auto Start(int laneCount, PoolConfig poolConfig) -> bool;
    void Shutdown();

    void SetRenderSize(int width, int height);
    void SetTexturePaths(std::vector<std::string> paths);

    /// Starts over with `performance`: resets the pool, prepares the first scenes and pre-warms the rest.
    void LoadPerformance(const model::Performance& performance);

    /// The loaded performance was edited (scenes matched by id). Keeps playing: unchanged scenes keep their
    /// prepared instances, changed presets are re-prepared, video settings change in place.
    void UpdatePerformance(const model::Performance& performance);

    /// Requests scene `index` (into the loaded performance) to go live.
    void Trigger(int index);

    /// Where live scenes get their video frames from (may be null: no video).
    void SetVideoProvider(VideoProvider* provider)
    {
        m_videoProvider = provider;
    }

    /// The outgoing scene during a cut's handover, or -1.
    auto HandoverScene() const -> int
    {
        return m_handover.scene;
    }

    /// Once per output frame: handles lane events, fans out `frames` interleaved stereo samples (times `gain`),
    /// and asks the live lane for the next frame.
    void Tick(const float* samples, size_t frames, float gain);

    /// Copies the newest live frame into the presented texture, waiting for it on the GPU. Call once per
    /// output frame on one GL context (the output context). Returns the presented texture (black until
    /// the first scene is live).
    auto Present() -> GLuint;

    /// Fence after the last Present() copy; other contexts wait on it before sampling the presented texture.
    auto PresentFence() const -> GLsync
    {
        return m_presentFence;
    }

    auto PresentedTexture() const -> GLuint
    {
        return m_presentedTexture;
    }
    auto RenderWidth() const -> int
    {
        return m_width;
    }
    auto RenderHeight() const -> int
    {
        return m_height;
    }
    auto LiveScene() const -> int
    {
        return m_pool.LiveScene();
    }
    auto PendingScene() const -> int
    {
        return m_pool.PendingScene();
    }
    auto Scene(int index) const -> SceneInfo;
    auto GetStats() const -> Stats;

private:
    void Execute(std::vector<PoolCommand> commands);
    void HandleEvent(const LaneEvent& event);
    void QueueWarm(int scene, const std::string& path, int excludeLane);
    void FinishHandover();
    void CreatePresentTargets();

    std::vector<std::unique_ptr<Lane>> m_lanes;
    VideoProvider* m_videoProvider{nullptr};
    ScenePool m_pool;
    model::Performance m_performance;
    int m_width{1920};
    int m_height{1080};
    std::vector<std::string> m_texturePaths;

    std::mutex m_eventMutex;
    std::deque<LaneEvent> m_events; //!< Filled by lane threads.

    int m_liveLane{-1};
    InstanceId m_liveInstance{0};

    struct LaneInstance
    {
        int lane{-1};
        InstanceId instance{0};
        int scene{-1};
    };
    LaneInstance m_handover;        //!< Outgoing live instance, still rendering until the new one has a frame.
    LaneInstance m_handoverRelease; //!< Set while executing an Activate's commands.
    long long m_tick{0};
    long long m_lastPresentedTick{-1};

    std::map<std::string, double> m_frameMsByPreset; //!< Warm-up estimate of the frame time per preset path.
    std::map<std::string, double> m_liveMsByPreset;  //!< Average live render time per preset path.
    std::map<std::string, std::string> m_errorByPreset;
    int m_warmDone{0};
    int m_warmTotal{0};
    int m_nextWarmLane{0};

    GLuint m_presentedTexture{0};
    GLuint m_presentFbo{0};
    GLuint m_readFbo{0};
    GLsync m_presentFence{nullptr};
    long long m_presentedFrames{0};
    long long m_heldTicks{0};
    long long m_heldWaitingTicks{0};
    long long m_heldNearCutTicks{0};
    long long m_lastActivateTick{-1000};
    std::map<int, long long> m_heldByScene;
    std::map<int, long long> m_liveTicksByScene;
    float m_audioLevel{0.f};
};

} // namespace gv::engine
