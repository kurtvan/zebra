#pragma once

#include <SDL.h>

#include <OpenGL/gl3.h>

#include <string>
#include <vector>

namespace gv::app {

/// Borderless fullscreen window on the performance display, with its own vsynced GL context in the
/// editor's share group. Shows the engine's presented texture, letterboxed.
class OutputWindow
{
public:
    OutputWindow() = default;
    ~OutputWindow();

    OutputWindow(const OutputWindow&) = delete;
    auto operator=(const OutputWindow&) -> OutputWindow& = delete;

    static auto DisplayNames() -> std::vector<std::string>;

    /// Opens on the display called `displayName`. The share context must be current. Returns false if the
    /// display isn't connected or the window can't be created.
    auto Open(const std::string& displayName) -> bool;
    void Close();

    auto IsOpen() const -> bool
    {
        return m_window != nullptr;
    }
    auto DisplayName() const -> const std::string&
    {
        return m_displayName;
    }
    auto WindowId() const -> Uint32;
    auto DrawableWidth() const -> int
    {
        return m_drawableWidth;
    }
    auto DrawableHeight() const -> int
    {
        return m_drawableHeight;
    }

    /// Makes the output context current, waits for `fence` on the GPU, draws `texture` and swaps (blocks on vsync).
    void Present(GLuint texture, GLsync fence, int textureWidth, int textureHeight);

private:
    SDL_Window* m_window{nullptr};
    SDL_GLContext m_context{nullptr};
    GLuint m_readFbo{0};
    std::string m_displayName;
    int m_drawableWidth{0};
    int m_drawableHeight{0};
};

} // namespace gv::app
