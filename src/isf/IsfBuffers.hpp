#pragma once

#include "isf/IsfProgram.hpp"

#include <OpenGL/gl3.h>

#include <map>
#include <string>
#include <vector>

namespace gv::isf {

/// One ISF node's buffers: the textures its passes draw into (TARGETs), kept from frame to frame, and its
/// own frame count (FRAMEINDEX, which shaders check to seed their buffers on frame 0).
///
/// A lane keeps one per ISF node of each loaded scene, so a simulation carries on while it stays loaded. Each
/// buffer has two textures: a pass reads one while it draws the other, then they swap. New buffers start
/// transparent black. Created, used and destroyed on one thread with its context current.
class IsfBuffers
{
public:
    IsfBuffers() = default;
    ~IsfBuffers();

    IsfBuffers(const IsfBuffers&) = delete;
    auto operator=(const IsfBuffers&) -> IsfBuffers& = delete;

    /// Draws every pass of `program` and its picture over all of `fbo` (width x height), replacing its contents.
    /// The shader renders at width x height divided by `divisor` (1, 2 or 4: the node's Resolution), its
    /// $WIDTH, $HEIGHT and RENDERSIZE; a picture of another size is scaled up. `images` and `values` are as for
    /// IsfProgram::Render, which also gets the buffers by name; `clock`'s frame index is replaced by this
    /// node's. Buffers are made, or remade at a new size or format (starting black), as the passes need them.
    /// Does nothing if !program.Ok(). Leaves framebuffer 0 bound.
    void Render(IsfProgram& program, std::map<std::string, GLuint> images, const std::map<std::string, std::vector<float>>& values,
                IsfClock clock, GLuint fbo, int width, int height, int divisor = 1);

    /// Clears every buffer to transparent black and starts the frame count again: the shader starts fresh.
    void Clear();

    /// Frames drawn since this was made or last cleared.
    [[nodiscard]] auto Frames() const -> int { return m_frames; }

    /// The size of buffer `name`, or 0 x 0 if there's none.
    [[nodiscard]] auto BufferSize(const std::string& name) const -> std::pair<int, int>
    {
        const auto it = m_buffers.find(name);
        return it == m_buffers.end() ? std::pair{0, 0} : std::pair{it->second.width, it->second.height};
    }

    /// Textures held by every IsfBuffers that exists, for tests that check they're freed.
    static auto LiveTextures() -> int;

private:
    struct Buffer
    {
        GLuint texture[2]{};
        GLuint fbo[2]{};
        int width{0};
        int height{0};
        bool floating{false};
        int front{0}; //!< The texture that holds the latest picture.
    };

    /// `buffer` with `textures` (1 or 2) at this size and format, remade (black) if it isn't.
    static void Fit(Buffer& buffer, int width, int height, bool floating, int textures = 2);
    static void Free(Buffer& buffer);
    static void ClearTexture(GLuint fbo);

    std::map<std::string, Buffer> m_buffers;
    Buffer m_picture; //!< The last pass, when it's drawn at a size other than the output's.
    int m_frames{0};
};

} // namespace gv::isf
