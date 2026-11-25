#pragma once

#include "engine/RenderPlan.hpp"
#include "engine/ScenePool.hpp"
#include "engine/VideoChain.hpp"

#include <projectM-4/projectM.h>

#include <SDL.h>

#include <OpenGL/gl3.h>

#include <chrono>
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

/// The current frame of the live scene's video, if it has one.
struct VideoFrame
{
    GLuint texture{0};     //!< GL_TEXTURE_2D, bottom-up; 0 means nothing to blend this frame.
    GLsync fence{nullptr}; //!< Signalled when `texture` is written; owned by the producer.
};

/// Something a lane finished, reported to the engine (from the lane thread).
struct LaneEvent
{
    enum class Type
    {
        Prepared, //!< An instance is ready (or failed: ok == false).
        Warmed,   //!< A throwaway pre-warm finished.
        ShaderCompiled, //!< An ISF shader compiled (ok) or didn't (error, the compiler's message).
    };

    Type type{Type::Prepared};
    int lane{-1};
    InstanceId instance{0};
    int scene{-1};
    std::string presetPath;
    std::string shaderPath; //!< ShaderCompiled: the shader's file.
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

    void Prepare(InstanceId instance, int scene, std::string presetPath, LanePlan plan, bool priority);

    /// Throwaway load + warm-up so the driver's shader cache is hot for `presetPath`.
    void Warm(int scene, std::string presetPath);

    void Release(InstanceId instance);

    /// Changes how a hosted instance draws its video (branches and their effects) without re-preparing it.
    void SetPlan(InstanceId instance, LanePlan plan);

    /// Drops queued (not started) Prepare and Warm jobs. Returns the dropped prepares and warm scenes.
    auto CancelQueued() -> std::pair<std::vector<InstanceId>, std::vector<std::pair<int, std::string>>>;

    /// Which hosted instance renders on ticks (0: none).
    void SetLive(InstanceId instance);

    /// Feeds `pcm` to every hosted instance and, if live, renders one frame, drawing `video` through its plan.
    /// The live instance gets `livePcm` instead when it's set (the same audio plus beat hits).
    /// `signals` move the live instance's modulated effects (Modulated). Coalesces with an unprocessed tick (the
    /// newest video frame and signals win).
    void Tick(PcmChunk pcm, long long tick, VideoFrame video, PcmChunk livePcm = nullptr, model::SignalValues signals = {});

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
            SetPlan,
            Shutdown,
        };
        Type type{Type::Prepare};
        InstanceId instance{0};
        int scene{-1};
        std::string presetPath;
        int width{0};
        int height{0};
        std::vector<std::string> texturePaths;
        LanePlan plan;
    };

    struct PendingTick
    {
        bool has{false};
        std::vector<PcmChunk> pcm;
        std::vector<PcmChunk> livePcm; //!< What the live instance hears: livePcm where given, else pcm.
        long long tick{-1};
        VideoFrame video;
        model::SignalValues signals;
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
    /// The preset's own texture that a Texture branch draws into, with a copy of the original image.
    struct TextureOverride
    {
        GLuint texture{0}; //!< Owned by projectM; 0: the preset has no such image (logged once).
        int width{0};
        int height{0};
        GLuint fbo{0};    //!< Draws into `texture`.
        GLuint backup{0}; //!< The original image, restored on detach.
        GLuint backupFbo{0};
    };
    using TextureOverrides = std::map<std::string, TextureOverride>;
    static void DeleteOverride(TextureOverride& override); //!< Deletes the lane's objects, not projectM's texture.

    /// Runs the plan's branches on `video` and sets the instance's layers. Texture branches are drawn only with
    /// `overrides` (hosted instances, not warm-ups). Returns the On top branch textures, to composite over the
    /// rendered frame.
    auto RunBranches(projectm_handle instance, const LanePlan& plan, GLuint video, TextureOverrides* overrides,
                     const model::SignalValues& signals = {}) -> std::vector<GLuint>;
    /// Targets[index] at render size, creating it (and those before it) if needed.
    auto BranchTarget(std::vector<RenderTarget>& targets, size_t index) -> RenderTarget;
    /// Compiles the plan's ISF shaders this lane doesn't have yet, reporting each one (ShaderCompiled).
    void CompileShaders(const LanePlan& plan);
    /// A version of the shader at `path` that a hosted instance draws with a working program, or null: what a
    /// version that doesn't compile is drawn with instead (CompileIsf).
    auto LastGoodShader(const std::filesystem::path& path) const -> std::shared_ptr<const isf::IsfShader>;
    /// Deletes the programs no hosted instance's plan uses.
    void DropUnusedPrograms();
    auto Override(InstanceId id, projectm_handle instance, const std::string& name, TextureOverrides& overrides) -> TextureOverride*;
    /// Restores the original images of the textures `plan` no longer draws into (all of them with an empty plan).
    void RestoreTextures(InstanceId id, const LanePlan& plan, TextureOverrides& overrides);

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
    struct Hosted
    {
        projectm_handle handle{nullptr};
        LanePlan plan; //!< How the scene draws its video.
        TextureOverrides textures;
    };
    std::map<InstanceId, Hosted> m_instances;
    GLuint m_placeholderVideo{0}; //!< 1x1 black texture used while warming up video scenes (compiles the blends).
    std::unique_ptr<VideoChain> m_chain; //!< Created when the thread starts.
    std::vector<RenderTarget> m_branchTargets; //!< Per branch index, RGBA8 straight alpha: the branch's picture.
    std::vector<RenderTarget> m_branchScratch; //!< Per branch index: the other half of its ping-pong (ISF branches).
    IsfPrograms m_programs;                    //!< The ISF shaders of the hosted instances' plans.
    isf::IsfClock m_isfClock;                  //!< TIME etc., advanced on live ticks.
    std::chrono::steady_clock::time_point m_isfStart{std::chrono::steady_clock::now()};
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
