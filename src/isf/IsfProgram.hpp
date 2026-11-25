#pragma once

#include "isf/IsfHeader.hpp"

#include <OpenGL/gl3.h>

#include <array>
#include <map>
#include <string>
#include <vector>

namespace gv::isf {

/// The host values an ISF pass reads besides its inputs.
struct IsfClock
{
    float time{0.f};       //!< TIME: seconds since start
    float timeDelta{0.f};  //!< TIMEDELTA: seconds since the last frame
    int frameIndex{0};     //!< FRAMEINDEX
    std::array<float, 4> date{}; //!< DATE: year, month, day, seconds since midnight
};

/// A compiled ISF shader that draws its first pass into a framebuffer (see IsfHeader.hpp for orientation).
///
/// Compiling never throws: a shader that fails leaves Ok() false and the compiler's message in Error(), with
/// line numbers counted in the .fs file. Created, used and destroyed on one thread with its context current.
class IsfProgram
{
public:
    explicit IsfProgram(const IsfShader& shader);
    ~IsfProgram();

    IsfProgram(const IsfProgram&) = delete;
    auto operator=(const IsfProgram&) -> IsfProgram& = delete;

    /// True if the shader compiled and linked.
    [[nodiscard]] auto Ok() const -> bool { return m_program != 0; }

    /// The compiler or linker message when !Ok(), else empty.
    [[nodiscard]] auto Error() const -> const std::string& { return m_error; }

    /// Draws the shader over all of `fbo` (width x height), replacing its contents.
    ///
    /// `images` gives image, audio and audioFFT inputs a bottom-up GL_TEXTURE_2D by name; a missing one reads
    /// as black. `values` gives the other inputs by name (1, 2 or 4 numbers, as IsfInput::defaultValue); a
    /// missing one uses its DEFAULT, or zero. Does nothing if !Ok(). Leaves blending off and framebuffer 0 bound.
    void Render(const std::map<std::string, GLuint>& images, const std::map<std::string, std::vector<float>>& values,
                const IsfClock& clock, GLuint fbo, int width, int height);

private:
    std::vector<IsfInput> m_inputs;
    std::vector<GLint> m_inputLocations; //!< one per m_inputs
    GLuint m_program{0};
    GLuint m_vao{0};
    GLint m_timeLocation{-1};
    GLint m_timeDeltaLocation{-1};
    GLint m_frameIndexLocation{-1};
    GLint m_renderSizeLocation{-1};
    GLint m_passIndexLocation{-1};
    GLint m_dateLocation{-1};
    std::string m_error;
};

} // namespace gv::isf
