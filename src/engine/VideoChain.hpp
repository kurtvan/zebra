#pragma once

#include "engine/RenderPlan.hpp"

#include <OpenGL/gl3.h>

#include <vector>

namespace gv::engine {

/// Runs a video branch's effects (contrast, saturation, luma gate, mix) in one draw, and composites finished
/// branches over a frame.
///
/// Run writes a straight-alpha image, which projectM then only blends in (patch 0002) or Composite draws on
/// top. The colour passes leave values unclamped; the gate and the end of the chain clamp, so a chain of
/// contrast, saturation, gate and mix is exactly the math of the old feedback blend and overlay shaders.
/// Created, used and destroyed on one lane's thread with its context current.
class VideoChain
{
public:
    static constexpr int kMaxPasses = 16; //!< Passes beyond this are ignored.

    VideoChain();
    ~VideoChain();

    VideoChain(const VideoChain&) = delete;
    auto operator=(const VideoChain&) -> VideoChain& = delete;

    /// Draws `video` (bottom-up GL_TEXTURE_2D) through `passes` into all of `fbo` (width x height), replacing
    /// its contents. The video's own alpha is ignored. Leaves blending off and framebuffer 0 bound.
    void Run(GLuint video, const std::vector<EffectPass>& passes, GLuint fbo, int width, int height);

    /// Alpha-blends `texture` (straight alpha) over the bound framebuffer's viewport, keeping its alpha.
    void Composite(GLuint texture);

private:
    GLuint m_vao{0};
    GLuint m_chainProgram{0};
    GLint m_videoLocation{-1};
    GLint m_countLocation{-1};
    GLint m_typeLocation{-1};
    GLint m_paramLocation{-1};
    GLint m_flagLocation{-1};
    GLuint m_compositeProgram{0};
    GLint m_compositeTextureLocation{-1};
};

} // namespace gv::engine
