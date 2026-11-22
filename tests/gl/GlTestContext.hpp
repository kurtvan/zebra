#pragma once

// The GL tests' shared context: a hidden window with a GL 4.1 core context for the whole run.

#include <SDL.h>

#include <OpenGL/gl3.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace gv::test {

/// A hidden window and GL 4.1 core context for the whole test run, or nothing if none can be created.
struct GlContext
{
    SDL_Window* window{nullptr};
    SDL_GLContext context{nullptr};
    std::string error;

    GlContext()
    {
        if (SDL_Init(SDL_INIT_VIDEO) != 0)
        {
            error = SDL_GetError();
            return;
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
        window = SDL_CreateWindow("gv_gl_tests", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (window == nullptr)
        {
            error = SDL_GetError();
            return;
        }
        context = SDL_GL_CreateContext(window);
        if (context == nullptr)
            error = SDL_GetError();
    }
    ~GlContext()
    {
        if (context != nullptr)
            SDL_GL_DeleteContext(context);
        if (window != nullptr)
            SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

inline auto Context() -> GlContext&
{
    static GlContext context;
    return context;
}

#define REQUIRE_GL()                                                                \
    do                                                                              \
    {                                                                               \
        if (::gv::test::Context().context == nullptr)                               \
            SKIP("no GL 4.1 context: " << ::gv::test::Context().error);             \
    } while (false)

inline auto Texture(int width, int height, const std::vector<std::uint8_t>& pixels) -> GLuint
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.empty() ? nullptr : pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

} // namespace gv::test
