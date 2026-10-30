#include "engine/VideoOverlay.hpp"

#include "common/log.hpp"

namespace gv::engine {

namespace {

constexpr const char* kLogCategory = "overlay";

// A full-screen triangle from gl_VertexID; no vertex buffer needed.
constexpr const char* kVertexShader = R"GLSL(#version 330 core
out vec2 vUv;
void main()
{
    vec2 position = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = position;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

// Contrast and saturation as in the feedback blend, then a hard luma mask (inverted like the blend's
// gate, patch 0005), feathered by uSoftness.
constexpr const char* kFragmentShader = R"GLSL(#version 330 core
uniform sampler2D uTexture;
uniform float uMix;
uniform float uLumaMin;
uniform float uContrast;
uniform float uSaturation;
uniform float uSoftness;
uniform bool uLumaInvert;
in vec2 vUv;
out vec4 fragColor;
void main()
{
    const vec3 lumaWeights = vec3(0.299, 0.587, 0.114);
    vec3 color = texture(uTexture, vUv).rgb;
    color = (color - 0.5) * uContrast + 0.5;
    color = mix(vec3(dot(color, lumaWeights)), color, uSaturation);
    color = clamp(color, 0.0, 1.0);
    float luma = dot(color, lumaWeights);
    float edge = uSoftness * 0.5;
    float mask = edge > 0.0 ? smoothstep(uLumaMin - edge, uLumaMin + edge, luma) : step(uLumaMin, luma);
    if (uLumaInvert)
        mask = 1.0 - mask;
    fragColor = vec4(color, uMix * mask);
}
)GLSL";

auto Compile(GLenum type, const char* source) -> GLuint
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        GV_LOG_ERROR(kLogCategory, "shader failed to compile: %s", log);
    }
    return shader;
}

} // namespace

VideoOverlay::VideoOverlay()
{
    const GLuint vertex = Compile(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragment = Compile(GL_FRAGMENT_SHADER, kFragmentShader);
    m_program = glCreateProgram();
    glAttachShader(m_program, vertex);
    glAttachShader(m_program, fragment);
    glLinkProgram(m_program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024] = {};
        glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
        GV_LOG_ERROR(kLogCategory, "program failed to link: %s", log);
    }
    m_textureLocation = glGetUniformLocation(m_program, "uTexture");
    m_mixLocation = glGetUniformLocation(m_program, "uMix");
    m_lumaMinLocation = glGetUniformLocation(m_program, "uLumaMin");
    m_contrastLocation = glGetUniformLocation(m_program, "uContrast");
    m_saturationLocation = glGetUniformLocation(m_program, "uSaturation");
    m_softnessLocation = glGetUniformLocation(m_program, "uSoftness");
    m_lumaInvertLocation = glGetUniformLocation(m_program, "uLumaInvert");
    glGenVertexArrays(1, &m_vao);
}

VideoOverlay::~VideoOverlay()
{
    glDeleteVertexArrays(1, &m_vao);
    glDeleteProgram(m_program);
}

void VideoOverlay::Draw(GLuint texture, const VideoSettings& settings)
{
    glUseProgram(m_program);
    glUniform1i(m_textureLocation, 0);
    glUniform1f(m_mixLocation, settings.mix);
    glUniform1f(m_lumaMinLocation, settings.lumaMin);
    glUniform1f(m_contrastLocation, settings.contrast);
    glUniform1f(m_saturationLocation, settings.saturation);
    glUniform1f(m_softnessLocation, settings.softness);
    glUniform1i(m_lumaInvertLocation, settings.lumaInvert ? 1 : 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    // Colour over the frame; the frame's alpha stays opaque.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

} // namespace gv::engine
