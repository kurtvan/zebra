// Milestone 1 measurement tool: where does preset load time go, and how much memory does an instance use?
//
// Built only with -DGV_SPIKE=ON, which also applies the timing-scope patches in tools/spike_compile/patches/.
//
//   gv_spike_compile --library <dir> [--count 150] [--seed 1] [--width 1920] [--height 1080]
//                    [--memory-instances 8] [--csv out.csv]

#include "common/log.hpp"
#include "pipeline.hpp"

#include <projectM-4/projectM.h>

#include <SDL.h>

#include <OpenGL/gl3.h>
#include <mach/mach.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <atomic>
#include <vector>

extern "C" size_t projectm_spike_timings(char* buffer, size_t size);

namespace {

constexpr const char* kLogCategory = "spike";
constexpr double kFrameBudgetMs = 1000.0 / 60.0;

using Clock = std::chrono::steady_clock;

auto MillisecondsSince(Clock::time_point start) -> double
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

auto PhysicalFootprintBytes() -> uint64_t
{
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
    {
        return 0;
    }
    return info.phys_footprint;
}

struct Options
{
    std::filesystem::path library{"/Users/kurt/projects/visualizers/libprojectM-4.1.4/presets/milk"};
    int count{150};
    unsigned seed{1};
    int width{1920};
    int height{1080};
    int memoryInstances{8};
    std::filesystem::path csv;
    std::filesystem::path active; //!< lanes mode: preset rendered on the main thread (relative to the library).
    std::string workerMode{"both"}; //!< lanes mode: "load" (no warm-up draws), "frames" (draw only), or "both".
    int passes{1}; //!< timing mode: load the sample this many times in one process (pass 2+ shows in-process caching).
    std::filesystem::path scenesCsv; //!< pipeline mode: take scenes from a timing CSV...
    double maxSteadyMs{1e9};         //!< ...keeping presets whose steady frame time is at most this.
    int lanes{2};
    int lookahead{2};
    double secondsPerScene{2.0};
    std::filesystem::path textures{"/Users/kurt/projects/visualizers/milkdrop-textures/textures"};
    bool textureControl{false}; //!< textures mode: compare two renders without textures (randomness baseline).
    std::string mode{"timing"}; //!< "timing" (load phases + memory) or "lanes" (background prepare on a shared context).
};

auto ParseOptions(int argc, char* argv[]) -> Options
{
    Options options;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::string key = argv[i];
        const std::string value = argv[i + 1];
        if (key == "--library")
            options.library = value;
        else if (key == "--count")
            options.count = std::stoi(value);
        else if (key == "--seed")
            options.seed = static_cast<unsigned>(std::stoul(value));
        else if (key == "--width")
            options.width = std::stoi(value);
        else if (key == "--height")
            options.height = std::stoi(value);
        else if (key == "--memory-instances")
            options.memoryInstances = std::stoi(value);
        else if (key == "--csv")
            options.csv = value;
        else if (key == "--active")
            options.active = value;
        else if (key == "--worker-mode")
            options.workerMode = value;
        else if (key == "--passes")
            options.passes = std::stoi(value);
        else if (key == "--scenes-csv")
            options.scenesCsv = value;
        else if (key == "--max-steady")
            options.maxSteadyMs = std::stod(value);
        else if (key == "--lanes")
            options.lanes = std::stoi(value);
        else if (key == "--lookahead")
            options.lookahead = std::stoi(value);
        else if (key == "--seconds-per-scene")
            options.secondsPerScene = std::stod(value);
        else if (key == "--textures")
            options.textures = value;
        else if (key == "--texture-control")
            options.textureControl = value == "1";
        else if (key == "--mode")
            options.mode = value;
    }
    return options;
}

/// Picks presets round-robin across top-level categories so every category is represented.
auto SamplePresets(const std::filesystem::path& library, int count, unsigned seed) -> std::vector<std::filesystem::path>
{
    std::map<std::string, std::vector<std::filesystem::path>> byCategory;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(library))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".milk")
        {
            const auto relative = std::filesystem::relative(entry.path(), library);
            byCategory[relative.begin()->string()].push_back(entry.path());
        }
    }

    std::mt19937 rng(seed);
    for (auto& [category, presets] : byCategory)
    {
        std::sort(presets.begin(), presets.end());
        std::shuffle(presets.begin(), presets.end(), rng);
    }

    std::vector<std::filesystem::path> sample;
    for (size_t round = 0; static_cast<int>(sample.size()) < count; ++round)
    {
        bool added = false;
        for (auto& [category, presets] : byCategory)
        {
            if (round < presets.size() && static_cast<int>(sample.size()) < count)
            {
                sample.push_back(presets[round]);
                added = true;
            }
        }
        if (!added)
        {
            break;
        }
    }
    return sample;
}

struct Target
{
    GLuint fbo{0};
    GLuint texture{0};

    Target(int width, int height)
    {
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    ~Target()
    {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &texture);
    }
};

void FeedAudio(projectm_handle instance, int frame)
{
    // A beating two-tone signal so audio-reactive code paths run.
    static std::vector<float> samples(735 * 2);
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

auto RenderTimed(projectm_handle instance, const Target& target, int width, int height, int frame) -> double
{
    const auto start = Clock::now();
    FeedAudio(instance, frame);
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, width, height);
    projectm_opengl_render_frame_fbo(instance, target.fbo);
    glFinish();
    return MillisecondsSince(start);
}

struct Stats
{
    double p50{0}, p90{0}, p99{0}, max{0}, mean{0};
    int overBudget{0};
    size_t count{0};
};

auto ComputeStats(std::vector<double> values) -> Stats
{
    Stats stats;
    stats.count = values.size();
    if (values.empty())
    {
        return stats;
    }
    std::sort(values.begin(), values.end());
    auto at = [&](double q) { return values[std::min(values.size() - 1, static_cast<size_t>(q * (values.size() - 1) + 0.5))]; };
    stats.p50 = at(0.50);
    stats.p90 = at(0.90);
    stats.p99 = at(0.99);
    stats.max = values.back();
    double sum = 0.0;
    for (double v : values)
    {
        sum += v;
        if (v > kFrameBudgetMs)
        {
            stats.overBudget++;
        }
    }
    stats.mean = sum / static_cast<double>(values.size());
    return stats;
}

std::string g_lastFailure;

void OnPresetFailed(const char* filename, const char* message, void*)
{
    g_lastFailure = message != nullptr ? message : "unknown";
}

void ConfigureInstance(projectm_handle instance, const Options& options)
{
    projectm_set_window_size(instance, static_cast<size_t>(options.width), static_cast<size_t>(options.height));
    projectm_set_preset_locked(instance, true);
    projectm_set_hard_cut_enabled(instance, false);
    projectm_set_soft_cut_duration(instance, 0.0);
}

/// Main thread keeps rendering one instance while a worker thread with a shared GL context loads and
/// warms up presets. Reports how much the background work disturbs the main thread's frame times.
int RunLanes(const Options& options, SDL_Window* mainWindow, SDL_GLContext mainContext, const std::vector<std::filesystem::path>& presets)
{
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    SDL_Window* workerWindow = SDL_CreateWindow("spike-worker", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext workerContext = SDL_GL_CreateContext(workerWindow);
    if (workerContext == nullptr)
    {
        GV_LOG_ERROR(kLogCategory, "worker context creation failed: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(mainWindow, mainContext);

    Target target(options.width, options.height);
    projectm_handle active = projectm_create();
    ConfigureInstance(active, options);
    // A typical light preset as the "live" scene: the first sampled preset.
    const auto activePath = options.active.empty() ? presets.front() : options.library / options.active;
    projectm_load_preset_file(active, activePath.c_str(), false);

    int frame = 0;
    for (int i = 0; i < 30; ++i)
        RenderTimed(active, target, options.width, options.height, frame++);

    std::vector<double> baseline;
    for (int i = 0; i < 300; ++i)
        baseline.push_back(RenderTimed(active, target, options.width, options.height, frame++));

    std::atomic<bool> workerDone{false};
    std::vector<double> prepareMs;
    std::thread worker([&] {
        SDL_GL_MakeCurrent(workerWindow, workerContext);
        Target workerTarget(options.width, options.height);
        std::vector<projectm_handle> pool;
        int workerFrame = 0;
        for (size_t i = 1; i < presets.size(); ++i)
        {
            if (options.workerMode == "frames")
            {
                // Draw-only load on the worker: keep rendering one already-warm instance.
                if (pool.empty())
                {
                    projectm_handle instance = projectm_create();
                    ConfigureInstance(instance, options);
                    projectm_load_preset_file(instance, presets[i].c_str(), false);
                    for (int f = 0; f < 3; ++f)
                        RenderTimed(instance, workerTarget, options.width, options.height, workerFrame++);
                    pool.push_back(instance);
                }
                const auto start = Clock::now();
                RenderTimed(pool.front(), workerTarget, options.width, options.height, workerFrame++);
                RenderTimed(pool.front(), workerTarget, options.width, options.height, workerFrame++);
                prepareMs.push_back(MillisecondsSince(start));
                continue;
            }
            const auto start = Clock::now();
            projectm_handle instance = projectm_create();
            ConfigureInstance(instance, options);
            projectm_load_preset_file(instance, presets[i].c_str(), false);
            if (options.workerMode == "both")
            {
                // Warm-up: the first two draws absorb the driver's deferred compile.
                RenderTimed(instance, workerTarget, options.width, options.height, workerFrame++);
                RenderTimed(instance, workerTarget, options.width, options.height, workerFrame++);
            }
            else
            {
                glFinish();
            }
            prepareMs.push_back(MillisecondsSince(start));
            pool.push_back(instance);
            if (pool.size() > 4)
            {
                projectm_destroy(pool.front());
                pool.erase(pool.begin());
            }
        }
        for (auto* instance : pool)
            projectm_destroy(instance);
        glFinish();
        SDL_GL_MakeCurrent(workerWindow, nullptr);
        workerDone = true;
    });

    std::vector<double> during;
    while (!workerDone)
        during.push_back(RenderTimed(active, target, options.width, options.height, frame++));
    worker.join();
    SDL_GL_MakeCurrent(mainWindow, mainContext);

    auto print = [](const char* name, const std::vector<double>& values) {
        const auto s = ComputeStats(values);
        std::printf("%-32s n=%-5zu mean=%8.2f p50=%8.2f p90=%8.2f p99=%8.2f max=%8.2f  >16.7ms: %d\n",
                    name, s.count, s.mean, s.p50, s.p90, s.p99, s.max, s.overBudget);
    };
    std::printf("\n=== Lanes (worker: %s): main-thread frame times while a shared-context worker prepares %zu presets ===\n", options.workerMode.c_str(), prepareMs.size());
    print("main frame, baseline", baseline);
    print("main frame, during prepare", during);
    print("worker prepare (load+2 frames)", prepareMs);

    projectm_destroy(active);
    SDL_GL_DeleteContext(workerContext);
    SDL_DestroyWindow(workerWindow);
    return 0;
}

/// Renders each preset with and without texture search paths and compares the images.
int RunTextureCheck(const Options& options, const std::vector<std::filesystem::path>& presets)
{
    Target target(320, 180);
    auto render = [&](const std::filesystem::path& preset, bool withTextures) {
        projectm_handle instance = projectm_create();
        projectm_set_window_size(instance, 320, 180);
        projectm_set_preset_locked(instance, true);
        if (withTextures)
        {
            const char* paths[] = {options.textures.c_str()};
            projectm_set_texture_search_paths(instance, paths, 1);
        }
        projectm_load_preset_file(instance, preset.c_str(), false);
        for (int i = 0; i < 20; ++i)
            RenderTimed(instance, target, 320, 180, i);
        std::vector<unsigned char> pixels(320 * 180 * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.fbo);
        glReadPixels(0, 0, 320, 180, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        projectm_destroy(instance);
        return pixels;
    };
    int differ = 0;
    for (const auto& preset : presets)
    {
        const auto without = render(preset, false);
        const auto with = render(preset, !options.textureControl);
        double diff = 0.0;
        for (size_t i = 0; i < with.size(); ++i)
            diff += std::abs(static_cast<int>(with[i]) - static_cast<int>(without[i]));
        diff /= static_cast<double>(with.size());
        if (diff > 1.0)
            differ++;
        std::printf("mean abs pixel difference %6.2f  %s\n", diff, preset.filename().c_str());
    }
    std::printf("%d of %zu presets look different with textures\n", differ, presets.size());
    return 0;
}

/// Patch 0002 check: blend a half-red/half-blue texture and see where the colours end up in the output.
int RunFeedbackCheck(const Options& options, const std::vector<std::filesystem::path>& presets)
{
    const int size = 64;
    std::vector<unsigned char> image(size * size * 4);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            unsigned char* p = &image[static_cast<size_t>((y * size + x) * 4)];
            const bool top = y >= size / 2; // GL rows go bottom-up
            p[0] = top ? 255 : 0;
            p[1] = 0;
            p[2] = top ? 0 : 255;
            p[3] = 255;
        }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    Target target(320, 180);
    for (const auto& preset : presets)
    {
        for (float mix : {0.0f, 1.0f})
        {
            projectm_handle instance = projectm_create();
            projectm_set_window_size(instance, 320, 180);
            projectm_set_preset_locked(instance, true);
            projectm_load_preset_file(instance, preset.c_str(), false);
            projectm_set_feedback_texture(instance, mix > 0 ? texture : 0);
            projectm_set_feedback_mix(instance, mix);
            for (int i = 0; i < 10; ++i)
                RenderTimed(instance, target, 320, 180, i);
            std::vector<unsigned char> pixels(320 * 180 * 4);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, target.fbo);
            glReadPixels(0, 0, 320, 180, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            auto at = [&](int x, int y) { return &pixels[static_cast<size_t>((y * 320 + x) * 4)]; };
            const unsigned char* top = at(160, 160);   // GL row 160 = near the top of the image
            const unsigned char* bottom = at(160, 20); // near the bottom
            std::printf("mix %.0f  top rgb(%3d,%3d,%3d)  bottom rgb(%3d,%3d,%3d)  %s\n", mix, top[0], top[1], top[2],
                        bottom[0], bottom[1], bottom[2], preset.filename().c_str());
            projectm_destroy(instance);
        }
    }
    glDeleteTextures(1, &texture);
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    const auto options = ParseOptions(argc, argv);

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        GV_LOG_ERROR(kLogCategory, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_Window* window = SDL_CreateWindow("spike", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window != nullptr ? SDL_GL_CreateContext(window) : nullptr;
    if (context == nullptr)
    {
        GV_LOG_ERROR(kLogCategory, "GL context creation failed: %s", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(window, context);
    GV_LOG_INFO(kLogCategory, "OpenGL %s on %s, target %dx%d", glGetString(GL_VERSION), glGetString(GL_RENDERER), options.width, options.height);

    const auto presets = SamplePresets(options.library, options.count, options.seed);
    GV_LOG_INFO(kLogCategory, "sampled %zu presets from %s", presets.size(), options.library.c_str());

    if (options.mode == "feedback")
    {
        const int result = RunFeedbackCheck(options, presets);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return result;
    }

    if (options.mode == "textures")
    {
        std::vector<std::filesystem::path> withTextures;
        for (const auto& preset : presets)
        {
            std::ifstream in(preset);
            std::stringstream text;
            text << in.rdbuf();
            const auto lower = text.str();
            if (lower.find("sampler_worms") != std::string::npos || lower.find("sampler_clouds") != std::string::npos)
                withTextures.push_back(preset);
        }
        const int result = RunTextureCheck(options, withTextures);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return result;
    }

    if (options.mode == "pipeline")
    {
        std::vector<std::string> scenes;
        std::ifstream in(options.scenesCsv);
        std::string line;
        std::getline(in, line); // header: preset,wall.load,wall.firstframe,wall.earlymax,wall.steadyframe,...
        while (std::getline(in, line) && static_cast<int>(scenes.size()) < options.count)
        {
            const auto close = line.find("\",");
            if (line.empty() || line[0] != '"' || close == std::string::npos)
                continue;
            const std::string preset = line.substr(1, close - 1);
            std::vector<double> fields;
            std::stringstream rest(line.substr(close + 2));
            std::string field;
            while (std::getline(rest, field, ','))
                fields.push_back(std::stod(field));
            if (fields.size() >= 4 && fields[3] <= options.maxSteadyMs)
                scenes.push_back((options.library / preset).string());
        }
        GV_LOG_INFO(kLogCategory, "pipeline: %zu scenes from %s (steady <= %.1f ms)", scenes.size(), options.scenesCsv.c_str(), options.maxSteadyMs);
        gv::spike::PipelineOptions pipeline;
        pipeline.width = options.width;
        pipeline.height = options.height;
        pipeline.lanes = options.lanes;
        pipeline.lookahead = options.lookahead;
        pipeline.secondsPerScene = options.secondsPerScene;
        const int result = gv::spike::RunPipeline(pipeline, window, context, scenes);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return result;
    }

    if (options.mode == "lanes")
    {
        const int result = RunLanes(options, window, context, presets);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return result;
    }

    Target target(options.width, options.height);

    // --- Phase A: load timing on one instance -------------------------------------------------
    projectm_handle instance = projectm_create();
    projectm_set_window_size(instance, static_cast<size_t>(options.width), static_cast<size_t>(options.height));
    projectm_set_preset_locked(instance, true);
    projectm_set_hard_cut_enabled(instance, false);
    projectm_set_soft_cut_duration(instance, 0.0);
    projectm_set_preset_switch_failed_event_callback(instance, &OnPresetFailed, nullptr);

    int frame = 0;
    // Warm up projectM's process-wide state (static shaders, idle preset) so it isn't charged to the first sample.
    for (int i = 0; i < 10; ++i)
    {
        RenderTimed(instance, target, options.width, options.height, frame++);
    }
    projectm_spike_timings(nullptr, 0);

    std::vector<std::string> phaseNames;
    std::map<std::string, std::vector<double>> phases;
    std::vector<double> loadMs, firstFrameMs, steadyFrameMs, earlyMaxMs, lastSlow;
    std::ofstream csv;
    if (!options.csv.empty())
    {
        csv.open(options.csv);
    }
    std::vector<std::map<std::string, double>> rows;
    std::vector<std::string> rowPresets;
    int failures = 0;

    std::vector<std::filesystem::path> schedule;
    std::vector<int> schedulePass;
    for (int pass = 1; pass <= options.passes; ++pass)
    {
        for (const auto& preset : presets)
        {
            schedule.push_back(preset);
            schedulePass.push_back(pass);
        }
    }
    std::map<int, std::vector<double>> loadByPass, firstByPass;

    for (size_t item = 0; item < schedule.size(); ++item)
    {
        const auto& preset = schedule[item];
        const int pass = schedulePass[item];
        g_lastFailure.clear();
        glFinish();
        const auto start = Clock::now();
        projectm_load_preset_file(instance, preset.c_str(), false);
        glFinish();
        const double load = MillisecondsSince(start);

        std::vector<char> text(8192);
        projectm_spike_timings(text.data(), text.size());

        if (!g_lastFailure.empty())
        {
            failures++;
            GV_LOG_WARN(kLogCategory, "failed: %s: %s", preset.filename().c_str(), g_lastFailure.substr(0, 120).c_str());
            continue;
        }

        const double first = RenderTimed(instance, target, options.width, options.height, frame++);
        // Anything the driver deferred to the first draw shows up as first-frame cost above the steady state.
        // Render 60 frames: frames 2-10 catch late pipeline compiles, frames 40-60 give the steady state.
        std::vector<double> early;
        std::vector<double> steady;
        int lastSlowFrame = 0;
        for (int i = 2; i <= 60; ++i)
        {
            const double ms = RenderTimed(instance, target, options.width, options.height, frame++);
            if (i <= 10)
                early.push_back(ms);
            if (i >= 40)
                steady.push_back(ms);
            if (ms > kFrameBudgetMs)
                lastSlowFrame = i;
        }
        std::sort(steady.begin(), steady.end());
        const double earlyMax = *std::max_element(early.begin(), early.end());
        // Frame work after the load counts towards the next sample's timings otherwise.
        projectm_spike_timings(nullptr, 0);

        std::map<std::string, double> row;
        std::istringstream lines(text.data());
        std::string name;
        double ms = 0;
        int count = 0;
        while (lines >> name >> ms >> count)
        {
            row[name] = ms;
            phases[name].push_back(ms);
            if (std::find(phaseNames.begin(), phaseNames.end(), name) == phaseNames.end())
            {
                phaseNames.push_back(name);
            }
            if (name == "gl.compileprogram")
            {
                row["gl.compileprogram.count"] = count;
                phases["gl.compileprogram.count"].push_back(count);
            }
        }
        row["wall.load"] = load;
        row["wall.firstframe"] = first;
        row["wall.steadyframe"] = steady[steady.size() / 2];
        row["wall.earlymax"] = earlyMax;
        row["lastslowframe"] = lastSlowFrame;
        earlyMaxMs.push_back(earlyMax);
        lastSlow.push_back(lastSlowFrame);
        loadByPass[pass].push_back(load);
        firstByPass[pass].push_back(first);
        loadMs.push_back(load);
        firstFrameMs.push_back(first);
        steadyFrameMs.push_back(steady[steady.size() / 2]);
        rows.push_back(row);
        rowPresets.push_back(std::filesystem::relative(preset, options.library).string());
    }

    std::sort(phaseNames.begin(), phaseNames.end());
    phaseNames.push_back("gl.compileprogram.count");
    if (csv.is_open())
    {
        csv << "preset,wall.load,wall.firstframe,wall.earlymax,wall.steadyframe,lastslowframe";
        for (const auto& name : phaseNames)
            csv << ',' << name;
        csv << '\n';
        for (size_t i = 0; i < rows.size(); ++i)
        {
            csv << '"' << rowPresets[i] << '"' << ',' << rows[i]["wall.load"] << ',' << rows[i]["wall.firstframe"] << ',' << rows[i]["wall.earlymax"] << ',' << rows[i]["wall.steadyframe"] << ',' << rows[i]["lastslowframe"];
            for (const auto& name : phaseNames)
                csv << ',' << rows[i][name];
            csv << '\n';
        }
    }

    auto print = [](const char* name, const std::vector<double>& values) {
        const auto s = ComputeStats(values);
        std::printf("%-26s n=%-4zu mean=%8.2f p50=%8.2f p90=%8.2f p99=%8.2f max=%8.2f  >16.7ms: %d\n",
                    name, s.count, s.mean, s.p50, s.p90, s.p99, s.max, s.overBudget);
    };
    std::printf("\n=== Preset load timing (ms), %zu presets, %d failed ===\n", rows.size(), failures);
    print("wall.load", loadMs);
    print("wall.firstframe", firstFrameMs);
    print("wall.earlymax (f2-10)", earlyMaxMs);
    print("wall.steadyframe (f40+)", steadyFrameMs);
    print("last frame over budget", lastSlow);
    for (const auto& name : phaseNames)
    {
        print(name.c_str(), phases[name]);
    }
    if (options.passes > 1)
    {
        for (const auto& [pass, values] : loadByPass)
        {
            const std::string loadName = "pass " + std::to_string(pass) + " wall.load";
            const std::string firstName = "pass " + std::to_string(pass) + " wall.firstframe";
            print(loadName.c_str(), values);
            print(firstName.c_str(), firstByPass[pass]);
        }
    }

    projectm_destroy(instance);

    // --- Phase B: memory per loaded instance --------------------------------------------------
    glFinish();
    const uint64_t before = PhysicalFootprintBytes();
    std::vector<projectm_handle> instances;
    for (int i = 0; i < options.memoryInstances && i < static_cast<int>(presets.size()); ++i)
    {
        projectm_handle extra = projectm_create();
        projectm_set_window_size(extra, static_cast<size_t>(options.width), static_cast<size_t>(options.height));
        projectm_set_preset_locked(extra, true);
        projectm_load_preset_file(extra, presets[static_cast<size_t>(i)].c_str(), false);
        for (int f = 0; f < 3; ++f)
        {
            RenderTimed(extra, target, options.width, options.height, frame++);
        }
        instances.push_back(extra);
    }
    glFinish();
    const uint64_t after = PhysicalFootprintBytes();
    const double perInstanceMb = instances.empty() ? 0.0 : (static_cast<double>(after) - static_cast<double>(before)) / instances.size() / (1024.0 * 1024.0);
    std::printf("\n=== Memory at %dx%d ===\nfootprint before %.1f MB, after %zu instances %.1f MB, per instance %.1f MB\n",
                options.width, options.height, before / 1048576.0, instances.size(), after / 1048576.0, perInstanceMb);
    for (auto* extra : instances)
    {
        projectm_destroy(extra);
    }

    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
