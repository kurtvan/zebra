#include "isf/IsfProgram.hpp"

#include <algorithm>
#include <cmath>

namespace gv::isf {

namespace {

/// Compiles one stage; on failure returns 0 and puts the compiler's message in `error`.
auto Compile(GLenum type, const std::string& source, std::string& error) -> GLuint
{
    const GLuint shader = glCreateShader(type);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok)
        return shader;
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<size_t>(std::max(length, 1)), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    error = log.c_str(); // drops the terminator
    if (error.empty())
        error = "the shader didn't compile, and the driver gave no reason";
    glDeleteShader(shader);
    return 0;
}

} // namespace

IsfProgram::IsfProgram(const IsfShader& shader)
    : m_inputs(shader.inputs)
    , m_passes(shader.passes)
{
    const GLuint vertex = Compile(GL_VERTEX_SHADER, IsfVertexShader(), m_error);
    const GLuint fragment = vertex == 0 ? 0 : Compile(GL_FRAGMENT_SHADER, TranslateIsf(shader), m_error);
    if (fragment != 0)
    {
        const GLuint program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        glDetachShader(program, vertex);
        glDetachShader(program, fragment);
        GLint ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (ok)
            m_program = program;
        else
        {
            char log[1024] = {};
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            m_error = log[0] != '\0' ? log : "the shader didn't link, and the driver gave no reason";
            glDeleteProgram(program);
        }
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    if (m_program == 0)
        return;

    for (const auto& input : m_inputs)
        m_inputLocations.push_back(glGetUniformLocation(m_program, input.name.c_str()));
    for (const auto& target : Targets(shader))
        if (FindInput(shader, target) == nullptr)
            m_targets.emplace_back(target, glGetUniformLocation(m_program, target.c_str()));
    m_timeLocation = glGetUniformLocation(m_program, "TIME");
    m_timeDeltaLocation = glGetUniformLocation(m_program, "TIMEDELTA");
    m_frameIndexLocation = glGetUniformLocation(m_program, "FRAMEINDEX");
    m_renderSizeLocation = glGetUniformLocation(m_program, "RENDERSIZE");
    m_passIndexLocation = glGetUniformLocation(m_program, "PASSINDEX");
    m_dateLocation = glGetUniformLocation(m_program, "DATE");
    glGenVertexArrays(1, &m_vao);
}

IsfProgram::~IsfProgram()
{
    if (m_vao != 0)
        glDeleteVertexArrays(1, &m_vao);
    if (m_program != 0)
        glDeleteProgram(m_program);
}

void IsfProgram::Render(const std::map<std::string, GLuint>& images, const std::map<std::string, std::vector<float>>& values,
                        const IsfClock& clock, GLuint fbo, int width, int height, int passIndex)
{
    if (m_program == 0)
        return;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(m_program);
    glUniform1f(m_timeLocation, clock.time);
    glUniform1f(m_timeDeltaLocation, clock.timeDelta);
    glUniform1i(m_frameIndexLocation, clock.frameIndex);
    glUniform2f(m_renderSizeLocation, static_cast<float>(width), static_cast<float>(height));
    glUniform1i(m_passIndexLocation, passIndex);
    glUniform4fv(m_dateLocation, 1, clock.date.data());

    GLint unit = 0;
    for (size_t i = 0; i < m_inputs.size(); ++i)
    {
        const auto& input = m_inputs[i];
        const GLint location = m_inputLocations[i];
        if (input.type == InputType::Image || input.type == InputType::Audio || input.type == InputType::AudioFft)
        {
            const auto image = images.find(input.name);
            glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
            glBindTexture(GL_TEXTURE_2D, image == images.end() ? 0 : image->second);
            glUniform1i(location, unit++);
            continue;
        }
        const auto given = values.find(input.name);
        std::array<float, 4> v{};
        const auto& source = given != values.end() ? given->second : input.defaultValue;
        std::copy_n(source.begin(), std::min<size_t>(source.size(), 4), v.begin());
        switch (input.type)
        {
            case InputType::Float: glUniform1f(location, v[0]); break;
            case InputType::Bool:
            case InputType::Event:
            case InputType::Long: glUniform1i(location, static_cast<GLint>(std::lround(v[0]))); break;
            case InputType::Point2D: glUniform2f(location, v[0], v[1]); break;
            case InputType::Color: glUniform4f(location, v[0], v[1], v[2], v[3]); break;
            default: break;
        }
    }

    for (const auto& [name, location] : m_targets)
    {
        const auto image = images.find(name);
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
        glBindTexture(GL_TEXTURE_2D, image == images.end() ? 0 : image->second);
        glUniform1i(location, unit++);
    }

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    for (GLint i = unit - 1; i >= 0; --i)
    {
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + i));
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace gv::isf
