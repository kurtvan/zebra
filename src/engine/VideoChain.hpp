#pragma once

#include "engine/RenderPlan.hpp"
#include "isf/IsfProgram.hpp"

#include <OpenGL/gl3.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gv::engine {

/// The compiled ISF shaders a lane draws with, by parsed file (IsfStep::shader). Versions of a file that don't
/// compile can share an earlier version's program (CompileIsf).
using IsfPrograms = std::map<std::shared_ptr<const isf::IsfShader>, std::shared_ptr<isf::IsfProgram>>;

/// Compiles `shader` into `programs`, unless it's there already. When it doesn't compile but `lastGood` (an
/// earlier version of the same file) has a working program, `shader` is drawn with that one, so a bad edit
/// keeps the old version on screen. Returns the compiler's message, or "" when it compiled.
auto CompileIsf(IsfPrograms& programs, const std::shared_ptr<const isf::IsfShader>& shader,
                const std::shared_ptr<const isf::IsfShader>& lastGood = nullptr) -> std::string;

/// A texture and the framebuffer that draws into it.
struct RenderTarget
{
    GLuint texture{0};
    GLuint fbo{0};
};

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

    /// Draws `video` through a branch's `steps` into all of `target` (width x height), ping-ponging through
    /// `scratch` between steps (it may be empty when no step is an ISF shader). An ISF step whose program is
    /// missing from `programs` or didn't compile passes its input through. Leaves framebuffer 0 bound.
    void RunSteps(GLuint video, const std::vector<VideoStep>& steps, const IsfPrograms& programs, const isf::IsfClock& clock,
                  RenderTarget target, RenderTarget scratch, int width, int height);

    /// A scene with no preset: draws `video` (0 for none, as with a generator) through `steps` into `picture`
    /// (RunSteps), then that over black into all of `fbo`, so what effects make see-through shows black. Leaves
    /// framebuffer 0 bound.
    void DrawPicture(GLuint video, const std::vector<VideoStep>& steps, const IsfPrograms& programs, const isf::IsfClock& clock,
                     RenderTarget picture, RenderTarget scratch, GLuint fbo, int width, int height);

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
