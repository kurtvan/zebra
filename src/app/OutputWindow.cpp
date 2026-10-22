#include "app/OutputWindow.hpp"

#include "common/log.hpp"

#include <algorithm>

namespace gv::app {

namespace {
constexpr const char* kLogCategory = "output";
}

OutputWindow::~OutputWindow()
{
    Close();
}

auto OutputWindow::DisplayNames() -> std::vector<std::string>
{
    std::vector<std::string> names;
    const int count = SDL_GetNumVideoDisplays();
    for (int i = 0; i < count; ++i)
    {
        const char* name = SDL_GetDisplayName(i);
        names.emplace_back(name != nullptr ? name : "Display " + std::to_string(i + 1));
    }
    return names;
}

auto OutputWindow::Open(const std::string& displayName) -> bool
{
    Close();

    const auto names = DisplayNames();
    int display = -1;
    for (int i = 0; i < static_cast<int>(names.size()); ++i)
    {
        if (names[static_cast<size_t>(i)] == displayName)
        {
            display = i;
            break;
        }
    }
    if (display < 0)
    {
        GV_LOG_WARN(kLogCategory, "display \"%s\" isn't connected", displayName.c_str());
        return false;
    }

    SDL_Window* shareWindow = SDL_GL_GetCurrentWindow();
    SDL_GLContext shareContext = SDL_GL_GetCurrentContext();

    m_window = SDL_CreateWindow("Govtech Visualizer Output",
                                SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display), 1280, 720,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS);
    if (m_window == nullptr)
    {
        GV_LOG_ERROR(kLogCategory, "could not create the output window: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    m_context = SDL_GL_CreateContext(m_window);
    if (m_context == nullptr)
    {
        GV_LOG_ERROR(kLogCategory, "could not create the output GL context: %s", SDL_GetError());
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        SDL_GL_MakeCurrent(shareWindow, shareContext);
        return false;
    }
    SDL_GL_SetSwapInterval(1); // the output's vsync paces the whole app
    glGenFramebuffers(1, &m_readFbo);
    SDL_GL_GetDrawableSize(m_window, &m_drawableWidth, &m_drawableHeight);
    m_displayName = displayName;
    SDL_GL_MakeCurrent(shareWindow, shareContext);

    GV_LOG_INFO(kLogCategory, "output on \"%s\" at %dx%d pixels", displayName.c_str(), m_drawableWidth, m_drawableHeight);
    return true;
}

void OutputWindow::Close()
{
    if (m_window == nullptr)
    {
        return;
    }
    SDL_Window* previousWindow = SDL_GL_GetCurrentWindow();
    SDL_GLContext previousContext = SDL_GL_GetCurrentContext();
    SDL_GL_MakeCurrent(m_window, m_context);
    glDeleteFramebuffers(1, &m_readFbo);
    m_readFbo = 0;
    SDL_GL_MakeCurrent(previousWindow, previousContext);
    SDL_GL_DeleteContext(m_context);
    SDL_DestroyWindow(m_window);
    m_context = nullptr;
    m_window = nullptr;
    GV_LOG_INFO(kLogCategory, "output closed");
    m_displayName.clear();
}

auto OutputWindow::WindowId() const -> Uint32
{
    return m_window != nullptr ? SDL_GetWindowID(m_window) : 0;
}

void OutputWindow::Present(GLuint texture, GLsync fence, int textureWidth, int textureHeight)
{
    if (m_window == nullptr)
    {
        return;
    }
    SDL_GL_MakeCurrent(m_window, m_context);
    SDL_GL_GetDrawableSize(m_window, &m_drawableWidth, &m_drawableHeight);

    if (fence != nullptr)
    {
        glWaitSync(fence, 0, GL_TIMEOUT_IGNORED);
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, m_drawableWidth, m_drawableHeight);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (texture != 0 && textureWidth > 0 && textureHeight > 0)
    {
        // Letterbox to keep the render's aspect ratio.
        const float scale = std::min(static_cast<float>(m_drawableWidth) / textureWidth, static_cast<float>(m_drawableHeight) / textureHeight);
        const int width = static_cast<int>(textureWidth * scale);
        const int height = static_cast<int>(textureHeight * scale);
        const int x = (m_drawableWidth - width) / 2;
        const int y = (m_drawableHeight - height) / 2;

        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_readFbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        glBlitFramebuffer(0, 0, textureWidth, textureHeight, x, y, x + width, y + height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }
    SDL_GL_SwapWindow(m_window);
}

} // namespace gv::app
