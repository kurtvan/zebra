#pragma once

#include "engine/ScenePool.hpp"

#include <projectM-4/projectM.h>

#include <SDL.h>

#include <OpenGL/gl3.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace gv::engine {

using PcmChunk = std::shared_ptr<const std::vector<float>>; //!< Interleaved stereo float samples.

/// Something a lane finished, reported to the engine (from the lane thread).
struct LaneEvent
{
    enum class Type
    {
        Prepared, //!< An instance is ready (or failed: ok == false).
        Warmed,   //!< A throwaway pre-warm finished.
    };

    Type type{Type::Prepared};
    int lane{-1};
    InstanceId instance{0};
    int scene{-1};
    std::string presetPath;
    bool ok{false};
    std::string error;
    double prepareMs{0.0};
    double frameMs{0.0}; //!< Second warm-up frame: a steady-state estimate (first frames include compile work).
};

/// A frame the live lane rendered and published for the output thread.
struct PublishedFrame
{
    GLuint texture{0};
    GLsync fence{nullptr}; //!< Ownership passes to whoever takes the frame.
    long long tick{-1};
    InstanceId instance{0};
};

/// A worker thread with its own GL context in the output share group, hosting projectM instances.
///
/// Every instance is created, prepared, rendered and destroyed on its lane and never changes context,
/// so projectM's per-context objects (VAOs, FBOs) are never shared (technical doc section 4). The lane
/// marked live renders its live instance once per tick into one of three shared slot textures and
/// publishes a fence. Ticks take priority over queued preparation work.
class Lane
{
public:
    using EventSink = std::function<void(LaneEvent)>;

    /// `window` and `context` must be dedicated to this lane; the context must already share with the output context.
    Lane(int id, SDL_Window* window, SDL_GLContext context, EventSink sink);
    ~Lane();

    Lane(const Lane&) = delete;
    auto operator=(const Lane&) -> Lane& = delete;

    /// Render size and texture search paths, applied to slot textures and every hosted instance.
    void Configure(int width, int height, std::vector<std::string> texturePaths);

    void Prepare(InstanceId instance, int scene, std::string presetPath, bool priority);

    /// Throwaway load + warm-up so the driver's shader cache is hot for `presetPath`.
    void Warm(int scene, std::string presetPath);

    void Release(InstanceId instance);

    /// Drops queued (not started) Prepare and Warm jobs. Returns the dropped prepares and warm scenes.
    auto CancelQueued() -> std::pair<std::vector<InstanceId>, std::vector<std::pair<int, std::string>>>;

    /// Which hosted instance renders on ticks (0: none).
    void SetLive(InstanceId instance);

    /// Feeds `pcm` to every hosted instance and, if live, renders one frame. Coalesces with an unprocessed tick.
    void Tick(PcmChunk pcm, long long tick);

    /// The newest published frame not yet taken, or tick -1.
    auto TakeLatest() -> PublishedFrame;

    auto Id() const -> int
    {
        return m_id;
    }
    auto QueuedJobs() const -> size_t;

    /// Average CPU time of live renders over the last ticks (ms).
    auto LiveRenderMs() const -> double;

    /// Average GPU time of live renders over the last ticks (ms), from timer queries.
    auto LiveGpuMs() const -> double;

private:
    struct Job
    {
        enum class Type
        {
            Prepare,
            Warm,
            Release,
            Configure,
            Shutdown,
        };
        Type type{Type::Prepare};
        InstanceId instance{0};
        int scene{-1};
        std::string presetPath;
        int width{0};
        int height{0};
        std::vector<std::string> texturePaths;
    };

    struct PendingTick
    {
        bool has{false};
        std::vector<PcmChunk> pcm;
        long long tick{-1};
    };

    void Run();
    void Push(Job job, bool front);
    void DoPrepare(const Job& job, bool keep);
    void DoRelease(InstanceId instance);
    void DoConfigure(const Job& job);
    void DoTick(PendingTick tick);
    void CreateTargets();
    void DestroyTargets();
    auto CreateInstance() -> projectm_handle;

    const int m_id;
    SDL_Window* m_window;
    SDL_GLContext m_context;
    EventSink m_sink;
    std::thread m_thread;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Job> m_jobs;
    PendingTick m_pendingTick;
    InstanceId m_live{0};
    std::set<InstanceId> m_released; //!< Released while queued or in progress.

    // Lane thread only.
    int m_width{0};
    int m_height{0};
    std::vector<std::string> m_texturePaths;
    std::map<InstanceId, projectm_handle> m_instances;
    static constexpr int kSlots = 3;
    GLuint m_slotTexture[kSlots]{};
    GLuint m_slotFbo[kSlots]{};
    GLuint m_scratchTexture{0};
    GLuint m_scratchFbo{0};
    InstanceId m_renderedLive{0};
    GLuint m_timerQuery[kSlots]{};
    bool m_queryPending[kSlots]{};

    mutable std::mutex m_publishMutex;
    PublishedFrame m_published;
    bool m_publishedTaken{true};
    double m_liveRenderMs{0.0};
    double m_liveGpuMs{0.0};
};

} // namespace gv::engine
