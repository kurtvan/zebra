#pragma once

#include <OpenGL/gl3.h>

namespace gv::io {

/// Copies a GL_TEXTURE_RECTANGLE (what CoreVideo and Syphon hand out) into an owned GL_TEXTURE_2D,
/// optionally flipping it vertically so the result is bottom-up. Double-buffered: each Convert() writes the
/// texture the previous call didn't, so a lane still sampling last frame's texture isn't overwritten.
///
/// Must be created, used and destroyed on one GL context.
class RectToTexture
{
public:
    RectToTexture() = default;
    ~RectToTexture();

    RectToTexture(const RectToTexture&) = delete;
    auto operator=(const RectToTexture&) -> RectToTexture& = delete;

    /// Returns the 2D texture holding the converted frame, or 0 on failure.
    auto Convert(GLuint rectTexture, int width, int height, bool flipVertically) -> GLuint;

    /// The most recently converted texture (0 before the first frame).
    auto Current() const -> GLuint
    {
        return m_hasFrame ? m_textures[m_current] : 0;
    }

private:
    auto EnsureProgram() -> bool;
    void EnsureTargets(int width, int height);

    GLuint m_program{0};
    GLuint m_vao{0};
    GLint m_flipLocation{-1};
    GLint m_sizeLocation{-1};
    GLuint m_fbo{0};
    GLuint m_textures[2]{};
    int m_current{0};
    bool m_hasFrame{false};
    int m_width{0};
    int m_height{0};
};

} // namespace gv::io
