// Milestone 1 "lanes" pipeline prototype.
//
// Each projectM instance lives on a lane: a thread with its own GL context in the output context's share
// group. The lane hosting the live scene renders it into one of three shared textures per output tick and
// publishes a fence; the output thread waits on the fence (GPU-side) and blits the latest frame. Scenes are
// prepared (load + warm-up draws) only on lanes that aren't live, so the live lane never stalls on a load.
//
// Measures, at a simulated 60 Hz: output ticks that had no new frame (drops), cut latency, prepare times.

#include "pipeline.hpp"

#include "common/log.hpp"

#include <projectM-4/projectM.h>

#include <OpenGL/gl3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace gv::spike {

namespace {

constexpr const char* kLogCategory = "pipeline";
constexpr double kTickMs = 1000.0 / 60.0;
constexpr int kSlots = 3;

using Clock = std::chrono::steady_clock;

auto MillisecondsSince(Clock::time_point start) -> double
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void FeedAudio(projectm_handle instance, int frame)
{
    static thread_local std::vector<float> samples(735 * 2);
    for (size_t i = 0; i < samples.size() / 2; ++i)
    {
        const double t = (frame * 735.0 + static_cast<double>(i)) / 44100.0;
        const double envelope = 0.5 + 0.5 * std::sin(2.0 * M_PI * 2.0 * t);
        const auto value = static_cast<float>(envelope * (0.6 * std::sin(2.0 * M_PI * 110.0 * t) + 0.3 * std::sin(2.0 * M_PI * 1760.0 * t)));
        samples[i * 2] = value;
        samples[i * 2 + 1] = value;
    }
    projectm_pcm_add_float(instance, samples.data(), static_cast<unsigned int>(samples.size() / 2), PROJECTM_STEREO);
}

struct PublishedFrame
{
    GLuint texture{0};
    GLsync fence{nullptr};
    long long index{-1};
};

class Lane
{
public:
    Lane(int id, SDL_Window* window, SDL_GLContext context, int width, int height)
        : m_id(id)
        , m_window(window)
        , m_context(context)
        , m_width(width)
        , m_height(height)
    {
        m_thread = std::thread([this] { Run(); });
    }

    ~Lane()
    {
        Post([this] {
            for (auto& [scene, instance] : m_instances)
                projectm_destroy(instance);
            m_instances.clear();
            glDeleteFramebuffers(kSlots, m_slotFbo);
            glDeleteTextures(kSlots, m_slotTexture);
            glDeleteFramebuffers(1, &m_scratchFbo);
            glDeleteTextures(1, &m_scratchTexture);
            glFinish();
        });
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_quit = true;
        }
        m_cv.notify_one();
        m_thread.join();
    }

    void Post(std::function<void()> job)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_jobs.push_back(std::move(job));
        }
        m_cv.notify_one();
    }

    /// Prepares a scene on this lane: load + warm-up draws into a scratch target.
    void Prepare(int scene, std::string presetPath, std::function<void(int scene, double ms)> done)
    {
        Post([this, scene, presetPath = std::move(presetPath), done = std::move(done)] {
            const auto start = Clock::now();
            projectm_handle instance = projectm_create();
            projectm_set_window_size(instance, static_cast<size_t>(m_width), static_cast<size_t>(m_height));
            projectm_set_preset_locked(instance, true);
            projectm_set_hard_cut_enabled(instance, false);
            projectm_set_soft_cut_duration(instance, 0.0);
            projectm_load_preset_file(instance, presetPath.c_str(), false);
            for (int i = 0; i < 2; ++i)
            {
                FeedAudio(instance, i);
                glBindFramebuffer(GL_FRAMEBUFFER, m_scratchFbo);
                projectm_opengl_render_frame_fbo(instance, m_scratchFbo);
            }
            glFinish();
            m_instances[scene] = instance;
            done(scene, MillisecondsSince(start));
        });
    }

    void Release(int scene)
    {
        Post([this, scene] {
            auto it = m_instances.find(scene);
            if (it != m_instances.end())
            {
                projectm_destroy(it->second);
                m_instances.erase(it);
            }
        });
    }

    /// Renders one frame of `scene` into the next slot and publishes it.
    void RenderLive(int scene, long long tick)
    {
        Post([this, scene, tick] {
            auto it = m_instances.find(scene);
            if (it == m_instances.end())
                return;
            const int slot = static_cast<int>(tick % kSlots);
            FeedAudio(it->second, static_cast<int>(tick));
            glBindFramebuffer(GL_FRAMEBUFFER, m_slotFbo[slot]);
            projectm_opengl_render_frame_fbo(it->second, m_slotFbo[slot]);
            GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            glFlush();
            std::lock_guard<std::mutex> lock(m_publishMutex);
            if (m_published.fence != nullptr && !m_publishedTaken)
                glDeleteSync(m_published.fence);
            m_published = {m_slotTexture[slot], fence, tick};
            m_publishedTaken = false;
        });
    }

    /// Latest published frame (ownership of the fence passes to the caller) or index -1.
    auto TakeLatest() -> PublishedFrame
    {
        std::lock_guard<std::mutex> lock(m_publishMutex);
        if (m_publishedTaken)
            return {};
        m_publishedTaken = true;
        return m_published;
    }

    auto Hosts(int scene) const -> bool
    {
        return m_hosted.count(scene) > 0;
    }
    void MarkHosted(int scene, bool hosted)
    {
        if (hosted)
            m_hosted.insert({scene, true});
        else
            m_hosted.erase(scene);
    }
    auto HostedCount() const -> size_t
    {
        return m_hosted.size();
    }
    auto Id() const -> int
    {
        return m_id;
    }

private:
    void Run()
    {
        SDL_GL_MakeCurrent(m_window, m_context);
        glGenTextures(kSlots, m_slotTexture);
        glGenFramebuffers(kSlots, m_slotFbo);
        for (int i = 0; i < kSlots; ++i)
            CreateTarget(m_slotTexture[i], m_slotFbo[i]);
        glGenTextures(1, &m_scratchTexture);
        glGenFramebuffers(1, &m_scratchFbo);
        CreateTarget(m_scratchTexture, m_scratchFbo);
        glFinish();

        while (true)
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] { return m_quit || !m_jobs.empty(); });
                if (m_jobs.empty() && m_quit)
                    break;
                job = std::move(m_jobs.front());
                m_jobs.pop_front();
            }
            job();
        }
        SDL_GL_MakeCurrent(m_window, nullptr);
    }

    void CreateTarget(GLuint texture, GLuint fbo)
    {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    int m_id;
    SDL_Window* m_window;
    SDL_GLContext m_context;
    int m_width;
    int m_height;

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::function<void()>> m_jobs;
    bool m_quit{false};

    GLuint m_slotTexture[kSlots]{};
    GLuint m_slotFbo[kSlots]{};
    GLuint m_scratchTexture{0};
    GLuint m_scratchFbo{0};
    std::map<int, projectm_handle> m_instances; //!< Lane thread only.
    std::map<int, bool> m_hosted;               //!< Output thread only.

    std::mutex m_publishMutex;
    PublishedFrame m_published;
    bool m_publishedTaken{true};
};

struct Stats
{
    double p50{0}, p99{0}, max{0};
};

auto ComputeStats(std::vector<double> values) -> Stats
{
    if (values.empty())
        return {};
    std::sort(values.begin(), values.end());
    auto at = [&](double q) { return values[std::min(values.size() - 1, static_cast<size_t>(q * (values.size() - 1) + 0.5))]; };
    return {at(0.5), at(0.99), values.back()};
}

} // namespace

int RunPipeline(const PipelineOptions& options, SDL_Window* outputWindow, SDL_GLContext outputContext, const std::vector<std::string>& scenes)
{
    // Lane contexts share with the output context.
    std::vector<std::unique_ptr<Lane>> lanes;
    SDL_GL_MakeCurrent(outputWindow, outputContext);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    for (int i = 0; i < options.lanes; ++i)
    {
        SDL_Window* window = SDL_CreateWindow("lane", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        SDL_GLContext context = SDL_GL_CreateContext(window);
        SDL_GL_MakeCurrent(outputWindow, outputContext);
        if (context == nullptr)
        {
            GV_LOG_ERROR(kLogCategory, "lane context creation failed: %s", SDL_GetError());
            return 1;
        }
        lanes.push_back(std::make_unique<Lane>(i, window, context, options.width, options.height));
    }

    // Output target + a read FBO for blitting lane textures.
    GLuint outputTexture = 0, outputFbo = 0, readFbo = 0;
    glGenTextures(1, &outputTexture);
    glBindTexture(GL_TEXTURE_2D, outputTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, options.width, options.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &outputFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, outputFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture, 0);
    glGenFramebuffers(1, &readFbo);

    std::mutex readyMutex;
    std::map<int, bool> ready;
    std::vector<double> prepareMs;
    auto onPrepared = [&](int scene, double ms) {
        std::lock_guard<std::mutex> lock(readyMutex);
        ready[scene] = true;
        prepareMs.push_back(ms);
    };
    auto isReady = [&](int scene) {
        std::lock_guard<std::mutex> lock(readyMutex);
        return ready.count(scene) > 0;
    };

    const int sceneCount = static_cast<int>(scenes.size());
    auto laneFor = [&](int scene) -> Lane* {
        for (auto& lane : lanes)
            if (lane->Hosts(scene))
                return lane.get();
        return nullptr;
    };

    int live = -1;
    Lane* liveLane = nullptr;

    // Prepare the window (current + next N) on non-live lanes, round-robin.
    int nextLaneHint = 0;
    auto ensurePrepared = [&](int scene) {
        if (scene < 0 || scene >= sceneCount || laneFor(scene) != nullptr)
            return;
        for (int attempt = 0; attempt < options.lanes; ++attempt)
        {
            Lane* lane = lanes[static_cast<size_t>((nextLaneHint + attempt) % options.lanes)].get();
            if (lane != liveLane)
            {
                lane->MarkHosted(scene, true);
                lane->Prepare(scene, scenes[static_cast<size_t>(scene)], onPrepared);
                nextLaneHint = (lane->Id() + 1) % options.lanes;
                return;
            }
        }
    };
    auto release = [&](int scene) {
        Lane* lane = laneFor(scene);
        if (lane == nullptr)
            return;
        lane->MarkHosted(scene, false);
        lane->Release(scene);
        std::lock_guard<std::mutex> lock(readyMutex);
        ready.erase(scene);
    };

    // Before the show: prepare the initial window and wait for it.
    for (int s = 0; s <= options.lookahead && s < sceneCount; ++s)
        ensurePrepared(s);
    while (!isReady(0))
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    const int ticksPerScene = static_cast<int>(options.secondsPerScene * 60.0);
    const long long totalTicks = static_cast<long long>(ticksPerScene) * sceneCount;

    long long lastShownIndex = -1;
    int drops = 0;
    int dropsNearCut = 0;
    int cutsNotReady = 0;
    std::vector<double> outputWorkMs;
    std::vector<int> cutLatencyTicks;
    long long pendingCutTick = -1;
    Lane* pendingCutLane = nullptr;
    std::map<int, int> dropsByScene;

    auto deadline = Clock::now();
    for (long long tick = 0; tick < totalTicks; ++tick)
    {
        deadline += std::chrono::microseconds(static_cast<long long>(kTickMs * 1000.0));
        std::this_thread::sleep_until(deadline);
        const auto workStart = Clock::now();

        // Cut on scene boundaries ("MIDI trigger").
        if (tick % ticksPerScene == 0)
        {
            const int target = static_cast<int>(tick / ticksPerScene);
            if (!isReady(target))
            {
                cutsNotReady++;
                while (!isReady(target)) // a real engine holds the last frame; the spike just waits
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const int previous = live;
            live = target;
            liveLane = laneFor(live);
            pendingCutTick = tick;
            pendingCutLane = liveLane;
            // Fresh on entry: the scene we left is discarded (a real engine re-prepares it if still in the window).
            if (previous >= 0)
                release(previous);
            for (int s = live + 1; s <= live + options.lookahead; ++s)
                ensurePrepared(s);
        }

        // Ask the live lane for the frame we'll show next tick, then show the latest finished one.
        liveLane->RenderLive(live, tick);

        PublishedFrame frame = liveLane->TakeLatest();
        if (frame.index >= 0 && frame.index > lastShownIndex)
        {
            glWaitSync(frame.fence, 0, GL_TIMEOUT_IGNORED);
            glDeleteSync(frame.fence);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, frame.texture, 0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, outputFbo);
            glBlitFramebuffer(0, 0, options.width, options.height, 0, 0, options.width, options.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glFlush();
            lastShownIndex = frame.index;
            if (pendingCutTick >= 0 && liveLane == pendingCutLane && frame.index >= pendingCutTick)
            {
                cutLatencyTicks.push_back(static_cast<int>(tick - pendingCutTick));
                pendingCutTick = -1;
            }
        }
        else if (tick > 2 && tick != pendingCutTick)
        {
            drops++;
            dropsByScene[live]++;
            if (tick % ticksPerScene < 30)
                dropsNearCut++;
        }
        outputWorkMs.push_back(MillisecondsSince(workStart));
    }

    const auto work = ComputeStats(outputWorkMs);
    const auto prep = ComputeStats(prepareMs);
    std::vector<double> latency(cutLatencyTicks.begin(), cutLatencyTicks.end());
    const auto lat = ComputeStats(latency);
    std::printf("\n=== Pipeline: %d scenes x %.1fs, %d lanes, lookahead %d, %dx%d ===\n",
                sceneCount, options.secondsPerScene, options.lanes, options.lookahead, options.width, options.height);
    std::printf("output ticks %lld, dropped (no new frame) %d (%.2f%%), of which within 0.5s after a cut %d\n",
                totalTicks, drops, 100.0 * drops / static_cast<double>(totalTicks), dropsNearCut);
    std::printf("cuts where the scene wasn't ready yet: %d\n", cutsNotReady);
    std::printf("cut latency (ticks from trigger to new scene on screen): p50 %.0f p99 %.0f max %.0f\n", lat.p50, lat.p99, lat.max);
    std::printf("output thread work per tick (ms): p50 %.2f p99 %.2f max %.2f\n", work.p50, work.p99, work.max);
    std::printf("prepare per scene (ms): p50 %.1f p99 %.1f max %.1f\n", prep.p50, prep.p99, prep.max);
    if (!dropsByScene.empty())
    {
        std::printf("drops by scene:");
        for (const auto& [scene, count] : dropsByScene)
            std::printf(" [%d]=%d", scene, count);
        std::printf("\n");
    }

    lanes.clear();
    glDeleteFramebuffers(1, &readFbo);
    glDeleteFramebuffers(1, &outputFbo);
    glDeleteTextures(1, &outputTexture);
    return 0;
}

} // namespace gv::spike
