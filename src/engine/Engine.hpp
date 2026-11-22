#pragma once

#include "engine/BeatGrid.hpp"
#include "engine/BeatHit.hpp"
#include "engine/Lane.hpp"
#include "engine/RenderPlan.hpp"
#include "engine/ScenePool.hpp"
#include "engine/SwitcherClock.hpp"
#include "model/Performance.hpp"

#include <SDL.h>

#include <OpenGL/gl3.h>

#include <chrono>
#include <deque>
#include <filesystem>
#include <random>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace gv::engine {

/// A video the engine shows, and whether it loops there (its video source node's setting).
struct VideoUse
{
    model::VideoSource source;
    bool loop{true};

    auto operator==(const VideoUse&) const -> bool = default;
};

/// What the video sources should do this frame. The engine works it out from the scenes and their
/// switchers, and hands it to the VideoProvider at the start of every Tick.
struct VideoNeeds
{
    /// Playing: the outgoing scene of a handover, a pending trigger's scene, then the live scene (so the
    /// live scene's loop setting wins for a video they share).
    std::vector<VideoUse> onScreen;
    /// The live scene's next video (a video switcher's next input): waiting paused at its start point.
    std::optional<model::VideoSource> upcoming;
    /// A video switcher moved on to these this frame (or jumped to them): each plays from its start point.
    std::vector<model::VideoSource> switchedTo;
    /// The first video of the scenes around the live one: paused, with a frame kept ready for a cut.
    std::vector<model::VideoSource> nearby;
};

/// Supplies video frames (implemented by the app, which owns the video sources).
class VideoProvider
{
public:
    virtual ~VideoProvider() = default;
    /// Once per Tick, before any FrameFor.
    virtual void BeginFrame(const VideoNeeds& needs) = 0;
    virtual auto FrameFor(const model::VideoSource& source) -> VideoFrame = 0;
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
        int playlistItem{-1};          //!< Live scenes switching presets: the entry showing (Scene::presetEntries).
        double secondsToNextItem{-1.0}; //!< Live, switching presets every so many seconds: time until the next one.
        double beatsToNextItem{-1.0};   //!< Live, switching presets on beats/bars: beats until the next one.
        bool waitingForClock{false};    //!< Live, switching presets on beats/bars, but the MIDI clock isn't running.
    };

    /// A switcher of the live scene, as it's running.
    struct SwitcherInfo
    {
        int passing{-1};              //!< The input passing now.
        int next{-1};                 //!< The input coming next, or -1.
        double secondsToNext{-1.0};   //!< Counting seconds: time until it moves on.
        double beatsToNext{-1.0};     //!< Counting beats/bars: beats until it moves on.
        bool waitingForClock{false};  //!< Counting beats/bars, but the MIDI clock isn't running.
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

    /// Goes live on entry `item` (Scene::presetEntries) of scene `index`, which switches presets (cutting
    /// within the live scene, or entering another scene there); its switcher carries on from that entry.
    void TriggerItem(int index, int item);

    /// Jumps video switcher `switcherId` of the live scene `index` to `input`: that video from its start
    /// point, and a whole interval before the next. Preset switchers jump with TriggerItem.
    void JumpVideo(int index, const std::string& switcherId, int input);

    /// The MIDI clock, for switchers that count beats or bars. Call every frame before Tick().
    void SetClock(const ClockInput& clock)
    {
        m_clock = clock;
    }

    /// The audio input's sample rate (0: not known), for the beat hits mixed into the live scene's audio.
    void SetSampleRate(int sampleRate)
    {
        m_sampleRate = sampleRate > 0 ? sampleRate : 48000;
    }

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
    /// The preset entry on screen in the live scene (0 for a scene without a preset switcher).
    auto LiveItem() const -> int
    {
        return m_pool.LiveItem();
    }
    /// What the live scene draws now: its preset node, video source node and branches. Empty with no live scene.
    auto LivePlan() const -> const RenderPlan&
    {
        return m_livePlan;
    }
    /// Switcher `switcherId` of scene `index`, if that's the live scene.
    auto Switcher(int index, const std::string& switcherId) const -> std::optional<SwitcherInfo>;
    /// The video scene `index` shows: the live scene's now, any other's when it starts. Nullopt: no video.
    auto VideoSourceOf(int index) const -> std::optional<model::VideoSource>;
    auto PendingScene() const -> int
    {
        return m_pool.PendingScene();
    }
    auto Scene(int index) const -> SceneInfo;

    struct PresetMeasurement
    {
        std::string path;
        double frameMs{0.0}; //!< Live average if measured live, else the warm-up estimate.
        std::string error;   //!< Load error, if it failed.
    };

    /// Everything learned about presets this session (for the library index).
    auto PresetMeasurements() const -> std::vector<PresetMeasurement>;
    auto GetStats() const -> Stats;

private:
    void Execute(std::vector<PoolCommand> commands);
    void HandleEvent(const LaneEvent& event);
    void QueueWarm(int scene, const std::string& path, int excludeLane);
    void FinishHandover();
    void UpdateSwitchers();
    void UpdatePresetSwitcher(const model::Scene& scene, int index, SwitcherClock::Clock::time_point now);
    auto UpdateVideoSwitchers(const model::Scene& scene, SwitcherClock::Clock::time_point now) -> bool;
    /// Which input each switcher of scene `index` passes for preset entry `item`: the live scene's video
    /// switchers as they're running, every other's first wired input.
    auto SelectionFor(int index, const model::Scene& scene, int item) const -> SwitcherSelection;
    auto PlanFor(int index, const model::Scene& scene, int item) const -> LanePlan;
    void RefreshLivePlans();
    auto Needs() const -> VideoNeeds;
    auto CurrentPreset(int scene) const -> std::filesystem::path;
    void CreatePresentTargets();

    std::vector<std::unique_ptr<Lane>> m_lanes;
    VideoProvider* m_videoProvider{nullptr};

    /// The live scene's switchers: one clock each, by switcher node id. Every switcher starts over when
    /// the scene is entered.
    struct LiveSwitchers
    {
        int scene{-1};
        InstanceId presetInstance{0}; //!< The live instance the preset switcher's clock follows.
        int preparedInput{-1};        //!< The preset switcher's input prepared as the next entry.
        std::map<std::string, SwitcherClock> clocks;
    };
    LiveSwitchers m_switchers;
    bool m_videoJumped{false};               //!< JumpVideo since the last UpdateSwitchers.
    RenderPlan m_livePlan;                   //!< For the live scene's current selection.
    std::optional<model::VideoSource> m_upcoming;
    std::vector<model::VideoSource> m_switchedTo;
    std::vector<RenderPlan> m_entryPlans;    //!< Per scene: what it shows when it starts.
    std::mt19937 m_random{std::random_device{}()};
    ClockInput m_clock;
    BeatHitClock m_beatHitClock;
    BeatHitSynth m_beatHitSynth;
    int m_sampleRate{48000};
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
        std::optional<VideoUse> video; //!< The video it was showing.
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
