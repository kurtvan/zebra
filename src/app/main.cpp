// Govtech Visualizer: editor window, performance output and the main loop.

#include "app/Settings.hpp"
#include "app/Shell.hpp"
#include "app_info.hpp"
#include "common/SpscRingBuffer.hpp"
#include "common/log.hpp"
#include "common/paths.hpp"
#include "engine/Engine.hpp"
#include "io/AudioInput.hpp"
#include "io/MidiInput.hpp"
#include "model/PerformanceJson.hpp"

#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <OpenGL/gl3.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kLogCategory = "app";
constexpr int kLanes = 3;
constexpr size_t kFrameHistory = 240;
constexpr size_t kMaxMessages = 200;
constexpr int kMaxRenderWidth = 1920; //!< Keeps Retina screens at a 1080p-class render (the M1 budgets).

using Clock = std::chrono::steady_clock;

struct Options
{
    std::optional<std::filesystem::path> performance; //!< Opened instead of the last one.
    // Headless check: play `performance` cutting every `smokeCutSeconds`, report, and quit.
    bool smoke{false};
    double smokeSeconds{20.0};
    double smokeCutSeconds{2.0};
    bool smokeNoEditor{false}; //!< Skip drawing the editor (to measure its cost).
};

auto ParseOptions(int argc, char* argv[]) -> Options
{
    Options options;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--performance" && hasValue)
            options.performance = argv[++i];
        else if (arg == "--smoke")
            options.smoke = true;
        else if (arg == "--smoke-seconds" && hasValue)
            options.smokeSeconds = std::atof(argv[++i]);
        else if (arg == "--smoke-no-editor")
            options.smokeNoEditor = true;
        else if (arg == "--smoke-cut-every" && hasValue)
            options.smokeCutSeconds = std::atof(argv[++i]);
        else if (arg.size() > 5 && arg.substr(arg.size() - 5) == ".json")
            options.performance = arg; // also accept a bare performance path
    }
    return options;
}

class App final : public gv::app::AppActions
{
public:
    App(SDL_Window* editorWindow, SDL_GLContext editorContext, Options options)
        : m_editorWindow(editorWindow)
        , m_editorContext(editorContext)
        , m_options(std::move(options))
        , m_ring(48000 * 2)
        , m_audio(m_ring)
    {
    }

    auto Initialize() -> bool
    {
        m_settingsFile = gv::paths::ApplicationSupportDir() / "settings.json";
        m_settings = gv::app::Settings::Load(m_settingsFile);

        if (!m_engine.Start(kLanes, gv::engine::PoolConfig{kLanes, 1, 3}))
        {
            return false;
        }
        ApplyTexturePaths();

        m_midi = gv::io::CreateMidiInput();
        m_midi->SetChannel(m_settings.midiChannel);

        if (!m_audio.Open(m_settings.audioDevice) && !m_settings.audioDevice.empty())
        {
            AddMessage("Audio input \"" + m_settings.audioDevice + "\" isn't available; using the system default.");
            m_audio.Open("");
        }

        UpdateRenderSize();

        if (m_options.performance)
            OpenPerformance(*m_options.performance);
        else if (!m_settings.lastPerformance.empty())
            OpenPerformance(m_settings.lastPerformance);
        return true;
    }

    void Shutdown()
    {
        SaveSettings();
        m_audio.Close();
        m_midi.reset();
        m_engine.Shutdown();
    }

    // --- AppActions -----------------------------------------------------------------------

    void OpenPerformance(const std::filesystem::path& file) override
    {
        auto result = gv::model::LoadPerformance(file);
        m_issues = result.issues;
        if (!result.ok)
        {
            AddMessage("Could not open " + file.string() + " (see the errors above).");
            GV_LOG_ERROR(kLogCategory, "could not open performance %s", file.c_str());
            return;
        }
        m_performance = std::move(result.performance);
        m_performancePath = file;
        m_hasPerformance = true;
        m_engine.LoadPerformance(m_performance);
        m_settings.lastPerformance = file;
        SaveSettings();
        AddMessage("Opened " + file.filename().string() + ": " + std::to_string(m_performance.scenes.size()) + " scenes.");
    }

    void ReloadPerformance() override
    {
        if (!m_performancePath.empty())
            OpenPerformance(m_performancePath);
    }

    void ToggleFullscreen() override
    {
        m_fullscreen = !m_fullscreen;
        SDL_SetWindowFullscreen(m_editorWindow, m_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        SDL_ShowCursor(m_fullscreen ? SDL_DISABLE : SDL_ENABLE);
        GV_LOG_INFO(kLogCategory, "fullscreen %s", m_fullscreen ? "on" : "off");
    }

    void SetAudioDevice(const std::string& deviceName) override
    {
        m_settings.audioDevice = deviceName;
        if (!m_audio.Open(deviceName))
            AddMessage("Could not open audio input \"" + deviceName + "\".");
        SaveSettings();
    }

    void SetMidiChannel(int channel) override
    {
        m_settings.midiChannel = channel;
        m_midi->SetChannel(channel);
        SaveSettings();
    }

    void SetLibraryRoot(const std::filesystem::path& folder) override
    {
        m_settings.libraryRoot = folder;
        SaveSettings();
    }

    void SetTextureFolder(const std::filesystem::path& folder) override
    {
        m_settings.texturePaths = {folder};
        ApplyTexturePaths();
        SaveSettings();
        AddMessage("Texture folder changed; it applies to scenes prepared from now on.");
    }

    void SettingsChanged() override
    {
        m_settingsDirty = true;
    }

    void Trigger(int scene) override
    {
        m_engine.Trigger(scene);
    }

    /// Renders at the native resolution of the screen the window is on, capped at kMaxRenderWidth wide.
    void UpdateRenderSize()
    {
        const int display = SDL_GetWindowDisplayIndex(m_editorWindow);
        SDL_DisplayMode mode{};
        if (display < 0 || SDL_GetDesktopDisplayMode(display, &mode) != 0)
        {
            return;
        }
        // Desktop modes are in points; scale by the window's pixel density to get the screen's pixels.
        int windowWidth = 0;
        int windowHeight = 0;
        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_GetWindowSize(m_editorWindow, &windowWidth, &windowHeight);
        SDL_GL_GetDrawableSize(m_editorWindow, &drawableWidth, &drawableHeight);
        const double density = windowWidth > 0 ? static_cast<double>(drawableWidth) / windowWidth : 1.0;
        int width = static_cast<int>(mode.w * density);
        int height = static_cast<int>(mode.h * density);
        if (width > kMaxRenderWidth)
        {
            height = static_cast<int>(static_cast<double>(height) * kMaxRenderWidth / width);
            width = kMaxRenderWidth;
        }
        m_engine.SetRenderSize(width, height);
    }

    // --- Main loop ------------------------------------------------------------------------

    void HandleEvent(const SDL_Event& event, bool& running)
    {
        if (event.type == SDL_QUIT)
        {
            running = false;
        }
        else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE)
        {
            running = false;
        }
        else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED)
        {
            UpdateRenderSize(); // moved to another screen
        }
        else if (m_audio.HandleEvent(event))
        {
            AddMessage("The audio input was removed.");
        }
        else if (event.type == SDL_KEYDOWN && (event.key.keysym.mod & KMOD_GUI) != 0)
        {
            if (event.key.keysym.sym == SDLK_r)
                ReloadPerformance();
            else if (event.key.keysym.sym == SDLK_f && event.key.repeat == 0)
                ToggleFullscreen();
        }
        else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE && m_fullscreen)
        {
            ToggleFullscreen();
        }
    }

    /// One output frame. Returns false when the smoke run is over.
    auto Frame(gv::app::Shell& shell, int frame) -> bool
    {
        // MIDI scene triggers.
        if (auto value = m_midi->TakeSceneTrigger(); value && m_hasPerformance)
        {
            if (auto scene = m_performance.SceneForCc20(*value))
                m_engine.Trigger(static_cast<int>(*scene));
            else
                GV_LOG_INFO(kLogCategory, "CC 20 = %d doesn't match any scene", *value);
        }

        if (m_options.smoke)
        {
            SmokeStep();
        }

        // Audio: everything captured since the last frame.
        const size_t samples = m_ring.Read(m_pcm.data(), m_pcm.size());
        m_engine.Tick(m_pcm.data(), samples / 2, m_settings.gain);

        SDL_GL_MakeCurrent(m_editorWindow, m_editorContext);
        const GLuint presented = m_engine.Present();

        if (m_options.smokeNoEditor)
        {
            glFlush();
            PaceSmoke();
        }
        else if (m_fullscreen)
        {
            DrawFullscreen(presented);
        }
        else
        {
            DrawEditor(shell, presented);
        }

        RecordFrameInterval();
        if (m_settingsDirty && frame % 60 == 0)
        {
            SaveSettings();
        }
        return !m_smokeDone;
    }

    auto SmokeSucceeded() const -> bool
    {
        return m_smokeOk;
    }

private:
    void DrawEditor(gv::app::Shell& shell, GLuint presented)
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        gv::app::ShellState state;
        state.performance = m_hasPerformance ? &m_performance : nullptr;
        state.performancePath = m_performancePath;
        state.issues = &m_issues;
        state.messages = &m_messages;
        state.settings = &m_settings;
        state.engine = &m_engine;
        state.midi = m_midi.get();
        state.audio = &m_audio;
        state.preview = presented;
        state.frameIntervalsMs = &m_frameIntervals;
        state.slowFrames = m_slowFrames;
        shell.Draw(state, *this);

        ImGui::Render();
        int width = 0;
        int height = 0;
        SDL_GL_GetDrawableSize(m_editorWindow, &width, &height);
        if (m_engine.PresentFence() != nullptr)
        {
            glWaitSync(m_engine.PresentFence(), 0, GL_TIMEOUT_IGNORED);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glClearColor(0.08f, 0.08f, 0.09f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(m_editorWindow);
        if (m_options.smoke)
        {
            PaceSmoke(); // hidden windows don't wait for vsync
        }
    }

    /// Fullscreen: only the visuals, letterboxed, no UI.
    void DrawFullscreen(GLuint presented)
    {
        int width = 0;
        int height = 0;
        SDL_GL_GetDrawableSize(m_editorWindow, &width, &height);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        const int textureWidth = m_engine.RenderWidth();
        const int textureHeight = m_engine.RenderHeight();
        if (presented != 0 && textureWidth > 0 && textureHeight > 0)
        {
            if (m_readFbo == 0)
                glGenFramebuffers(1, &m_readFbo);
            const float scale = std::min(static_cast<float>(width) / textureWidth, static_cast<float>(height) / textureHeight);
            const int w = static_cast<int>(textureWidth * scale);
            const int h = static_cast<int>(textureHeight * scale);
            const int x = (width - w) / 2;
            const int y = (height - h) / 2;
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_readFbo);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, presented, 0);
            glBlitFramebuffer(0, 0, textureWidth, textureHeight, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        }
        SDL_GL_SwapWindow(m_editorWindow);
    }

    void RecordFrameInterval()
    {
        const auto now = Clock::now();
        if (m_lastFrame.time_since_epoch().count() != 0)
        {
            const float ms = std::chrono::duration<float, std::milli>(now - m_lastFrame).count();
            m_frameIntervals.push_back(ms);
            if (m_frameIntervals.size() > kFrameHistory)
                m_frameIntervals.pop_front();
            if (ms > 25.f)
                m_slowFrames++;
        }
        m_lastFrame = now;
    }

    void PaceSmoke()
    {
        m_smokeDeadline += std::chrono::microseconds(16667);
        const auto now = Clock::now();
        if (m_smokeDeadline < now)
            m_smokeDeadline = now;
        std::this_thread::sleep_until(m_smokeDeadline);
    }

    void SmokeStep()
    {
        if (m_smokeStart.time_since_epoch().count() == 0)
        {
            m_smokeStart = Clock::now();
            m_smokeDeadline = m_smokeStart;
        }
        const double elapsed = std::chrono::duration<double>(Clock::now() - m_smokeStart).count();
        if (!m_hasPerformance || m_performance.scenes.empty())
        {
            GV_LOG_ERROR(kLogCategory, "smoke: no performance loaded");
            m_smokeDone = true;
            return;
        }
        // Wait for the first scenes to be ready before cutting, like a show would.
        const int cut = static_cast<int>(elapsed / m_options.smokeCutSeconds);
        if (cut != m_smokeLastCut)
        {
            m_smokeLastCut = cut;
            const int scene = cut % static_cast<int>(m_performance.scenes.size());
            m_engine.Trigger(scene);
            m_smokeCuts++;
        }
        if (elapsed >= m_options.smokeSeconds)
        {
            const auto stats = m_engine.GetStats();
            const long long liveTicks = stats.ticks;
            GV_LOG_INFO(kLogCategory, "smoke: %.1fs, %d cuts, %lld ticks, %lld presented, %lld held (%.2f%%: %lld waiting for a scene to prepare, %lld late within 0.5 s of a cut, %lld late otherwise), frames over 25 ms: %lld, pre-warm %d/%d",
                        elapsed, m_smokeCuts, liveTicks, stats.presentedFrames, stats.heldTicks,
                        liveTicks > 0 ? 100.0 * stats.heldTicks / liveTicks : 0.0, stats.heldWaitingTicks, stats.heldNearCutTicks, stats.heldTicks - stats.heldWaitingTicks - stats.heldNearCutTicks,
                        m_slowFrames, stats.warmDone, stats.warmTotal);
            for (int i = 0; i < static_cast<int>(m_performance.scenes.size()); ++i)
            {
                const auto info = m_engine.Scene(i);
                const auto held = stats.heldByScene.count(i) ? stats.heldByScene.at(i) : 0;
                GV_LOG_INFO(kLogCategory, "smoke: scene %d %-10s %6.1f ms/frame%s, held %lld (%.1f%% of its live frames)", i, m_performance.scenes[static_cast<size_t>(i)].id.c_str(), info.frameMs, info.heavy ? " (heavy)" : "", held, info.droppedFraction * 100.0);
            }
            m_smokeOk = stats.presentedFrames > liveTicks / 2;
            m_smokeDone = true;
        }
    }

    void ApplyTexturePaths()
    {
        std::vector<std::string> paths;
        for (const auto& path : m_settings.texturePaths)
            paths.push_back(path.string());
        m_engine.SetTexturePaths(paths);
    }

    void SaveSettings()
    {
        if (m_options.smoke)
            return;
        if (auto error = m_settings.Save(m_settingsFile); !error.empty())
            GV_LOG_ERROR(kLogCategory, "%s", error.c_str());
        m_settingsDirty = false;
    }

    void AddMessage(std::string message)
    {
        GV_LOG_INFO(kLogCategory, "%s", message.c_str());
        m_messages.push_back(std::move(message));
        if (m_messages.size() > kMaxMessages)
            m_messages.pop_front();
    }

    SDL_Window* m_editorWindow;
    SDL_GLContext m_editorContext;
    Options m_options;

    std::filesystem::path m_settingsFile;
    gv::app::Settings m_settings;
    bool m_settingsDirty{false};

    gv::engine::Engine m_engine;
    bool m_fullscreen{false};
    GLuint m_readFbo{0};
    gv::SpscRingBuffer<float> m_ring;
    std::vector<float> m_pcm = std::vector<float>(48000 * 2);
    gv::io::AudioInput m_audio;
    std::unique_ptr<gv::io::MidiInput> m_midi;

    gv::model::Performance m_performance;
    std::filesystem::path m_performancePath;
    bool m_hasPerformance{false};
    std::vector<gv::model::Issue> m_issues;
    std::deque<std::string> m_messages;

    std::deque<float> m_frameIntervals;
    long long m_slowFrames{0};
    Clock::time_point m_lastFrame{};

    Clock::time_point m_smokeStart{};
    Clock::time_point m_smokeDeadline{};
    int m_smokeLastCut{-1};
    int m_smokeCuts{0};
    bool m_smokeDone{false};
    bool m_smokeOk{false};
};

} // namespace

int main(int argc, char* argv[])
{
    const auto options = ParseOptions(argc, argv);

    gv::log::Init(gv::paths::LogsDir());
    GV_LOG_INFO(kLogCategory, "%s %s starting", gv::app_info::kName, gv::app_info::kVersion);

    // Real fullscreen on the output display, without a separate Space and its animation.
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0)
    {
        GV_LOG_ERROR(kLogCategory, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    const Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (options.smoke ? SDL_WINDOW_HIDDEN : 0);
    SDL_Window* window = SDL_CreateWindow(gv::app_info::kName, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1600, 1000, windowFlags);
    SDL_GLContext context = window != nullptr ? SDL_GL_CreateContext(window) : nullptr;
    if (context == nullptr)
    {
        GV_LOG_ERROR(kLogCategory, "could not create the editor window: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);
    GV_LOG_INFO(kLogCategory, "OpenGL %s, renderer %s", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    static const std::string iniPath = (gv::paths::ApplicationSupportDir() / "layout.ini").string();
    io.IniFilename = options.smoke ? nullptr : iniPath.c_str();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init("#version 410 core");

    int status = 0;
    {
        App app(window, context, options);
        if (!app.Initialize())
        {
            status = 1;
        }
        else
        {
            gv::app::Shell shell;
            bool running = true;
            for (int frame = 0; running; ++frame)
            {
                SDL_Event event;
                while (SDL_PollEvent(&event))
                {
                    ImGui_ImplSDL2_ProcessEvent(&event);
                    app.HandleEvent(event, running);
                }
                if (!app.Frame(shell, frame))
                {
                    running = false;
                }
            }
            if (options.smoke)
            {
                status = app.SmokeSucceeded() ? 0 : 2;
            }
        }
        app.Shutdown();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    GV_LOG_INFO(kLogCategory, "exited (status %d)", status);
    return status;
}
