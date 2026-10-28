#pragma once

#include "engine/Lane.hpp"

#include <OpenGL/gl3.h>

namespace gv::engine {

/// Draws a scene's video over a rendered frame, where it passes the luma gate (VideoSettings::overlay).
///
/// Applies the same contrast and saturation as the feedback blend (patch 0004), so the mask matches it.
/// Created, used and destroyed on one lane's thread with its context current.
class VideoOverlay
{
public:
    VideoOverlay();
    ~VideoOverlay();

    VideoOverlay(const VideoOverlay&) = delete;
    auto operator=(const VideoOverlay&) -> VideoOverlay& = delete;

    /// Blends `texture` (bottom-up GL_TEXTURE_2D) into the bound framebuffer's viewport.
    void Draw(GLuint texture, const VideoSettings& settings);

private:
    GLuint m_program{0};
    GLuint m_vao{0};
    GLint m_textureLocation{-1};
    GLint m_mixLocation{-1};
    GLint m_lumaMinLocation{-1};
    GLint m_contrastLocation{-1};
    GLint m_saturationLocation{-1};
    GLint m_softnessLocation{-1};
};

} // namespace gv::engine
