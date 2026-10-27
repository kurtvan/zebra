#pragma once

#include "io/VideoSource.hpp"

#include <OpenGL/gl3.h>

namespace gv::io {

/// How RectToTexture places a frame in its output.
struct FramePlacement
{
    /// Stored frame -> upright picture, in top-down pixels (a CGAffineTransform: a, b, c, d, tx, ty).
    double transform[6]{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    VideoFit fit{VideoFit::Stretch};
    int width{0}; //!< Output size; 0: the upright picture's size.
    int height{0};
};

/// Copies a GL_TEXTURE_RECTANGLE (what CoreVideo and Syphon hand out) into an owned GL_TEXTURE_2D, bottom-up,
/// optionally turning the picture upright and fitting it to another size. Double-buffered: each Convert() writes the
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

    using Placement = FramePlacement;

    /// Returns the 2D texture holding the converted frame, or 0 on failure. `topDown`: the rectangle
    /// texture's first row is the top of the picture (CoreVideo's "flipped").
    auto Convert(GLuint rectTexture, int width, int height, bool topDown, const Placement& placement = {}) -> GLuint;

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
    GLint m_topDownLocation{-1};
    GLint m_sizeLocation{-1};
    GLint m_scaleLocation{-1};
    GLint m_pictureSizeLocation{-1};
    GLint m_toStoredLocation{-1};
    GLuint m_fbo{0};
    GLuint m_textures[2]{};
    int m_current{0};
    bool m_hasFrame{false};
    int m_width{0};
    int m_height{0};
};

} // namespace gv::io
