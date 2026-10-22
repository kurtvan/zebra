// Govtech Visualizer: M0 skeleton.
// An ImGui docking window showing projectM's idle preset, rendered into an offscreen FBO.

#include "app_info.hpp"
#include "common/log.hpp"
#include "common/paths.hpp"

#include <projectM-4/projectM.h>

#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <OpenGL/gl3.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr const char* kLogCategory = "app";

/// Colour texture + FBO that projectM renders into.
struct OffscreenTarget
{
    GLuint fbo{0};
    GLuint texture{0};
    int width{0};
    int height{0};

    void Resize(int newWidth, int newHeight)
    {
        if (newWidth == width && newHeight == height)
        {
            return;
        }
        Destroy();
        width = newWidth;
        height = newHeight;

        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            GV_LOG_ERROR(kLogCategory, "offscreen framebuffer %dx%d is incomplete", width, height);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void Destroy()
    {
        if (fbo != 0)
        {
            glDeleteFramebuffers(1, &fbo);
            fbo = 0;
        }
        if (texture != 0)
        {
            glDeleteTextures(1, &texture);
            texture = 0;
        }
        width = 0;
        height = 0;
    }

    /// Mean luma of the target in [0, 1]; used by --smoke-frames to show that something was drawn.
    auto MeanLuma() const -> double
    {
        std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        double sum = 0.0;
        for (size_t i = 0; i < pixels.size(); i += 4)
        {
            sum += 0.299 * pixels[i] + 0.587 * pixels[i + 1] + 0.114 * pixels[i + 2];
        }
        return sum / (255.0 * (pixels.size() / 4));
    }
};

struct Options
{
    int smokeFrames{0}; //!< > 0: render this many frames, report, then quit (for automated checks).
};

auto ParseOptions(int argc, char* argv[]) -> Options
{
    Options options;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--smoke-frames") == 0 && i + 1 < argc)
        {
            options.smokeFrames = std::atoi(argv[++i]);
        }
    }
    return options;
}

} // namespace

int main(int argc, char* argv[])
{
    const auto options = ParseOptions(argc, argv);

    gv::log::Init(gv::paths::LogsDir());
    GV_LOG_INFO(kLogCategory, "%s %s starting", gv::app_info::kName, gv::app_info::kVersion);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        GV_LOG_ERROR(kLogCategory, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    const Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                               (options.smokeFrames > 0 ? SDL_WINDOW_HIDDEN : 0);
    SDL_Window* window = SDL_CreateWindow(gv::app_info::kName, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1600, 1000, windowFlags);
    if (window == nullptr)
    {
        GV_LOG_ERROR(kLogCategory, "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (glContext == nullptr)
    {
        GV_LOG_ERROR(kLogCategory, "SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(1);
    GV_LOG_INFO(kLogCategory, "OpenGL %s, renderer %s", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    static const std::string iniPath = (gv::paths::ApplicationSupportDir() / "imgui.ini").string();
    io.IniFilename = iniPath.c_str();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 410 core");

    projectm_handle projectM = projectm_create();
    if (projectM == nullptr)
    {
        GV_LOG_ERROR(kLogCategory, "projectm_create failed");
        return 1;
    }
    projectm_set_preset_locked(projectM, true);
    projectm_set_hard_cut_enabled(projectM, false);

    // Placeholder size until M2 sizes the target from the output display.
    OffscreenTarget target;
    target.Resize(1920, 1080);
    projectm_set_window_size(projectM, static_cast<size_t>(target.width), static_cast<size_t>(target.height));

    bool running = true;
    int frame = 0;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT ||
                (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window)))
            {
                running = false;
            }
        }

        // projectM renders into the offscreen target.
        glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
        glViewport(0, 0, target.width, target.height);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        projectm_opengl_render_frame_fbo(projectM, target.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport();

        ImGui::Begin("Preview");
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float aspect = static_cast<float>(target.width) / static_cast<float>(target.height);
        ImVec2 size(available.x, available.x / aspect);
        if (size.y > available.y)
        {
            size = ImVec2(available.y * aspect, available.y);
        }
        // GL textures are bottom-up; flip V for ImGui.
        ImGui::Image(static_cast<ImTextureID>(target.texture), size, ImVec2(0, 1), ImVec2(1, 0));
        ImGui::End();

        ImGui::Begin("Status");
        ImGui::Text("%.1f fps (%.2f ms)", io.Framerate, 1000.f / io.Framerate);
        ImGui::Text("Render target %dx%d", target.width, target.height);
        ImGui::End();

        ImGui::Render();
        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_GL_GetDrawableSize(window, &drawableWidth, &drawableHeight);
        glViewport(0, 0, drawableWidth, drawableHeight);
        glClearColor(0.08f, 0.08f, 0.09f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);

        ++frame;
        if (options.smokeFrames > 0 && frame >= options.smokeFrames)
        {
            const GLenum error = glGetError();
            GV_LOG_INFO(kLogCategory, "smoke: rendered %d frames, mean luma %.4f, glGetError 0x%04x", frame, target.MeanLuma(), error);
            running = false;
        }
    }

    projectm_destroy(projectM);
    target.Destroy();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    GV_LOG_INFO(kLogCategory, "exited cleanly");
    return 0;
}
