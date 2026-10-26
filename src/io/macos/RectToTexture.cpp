#include "io/macos/RectToTexture.hpp"

#include "common/log.hpp"

namespace gv::io {

namespace {

constexpr const char* kLogCategory = "video";

const char* kVertexShader = R"GLSL(
#version 330 core
const vec2 kPosition[4] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));
const vec2 kUv[4] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0));
uniform bool uFlip;
out vec2 vUv;
void main()
{
    vec2 uv = kUv[gl_VertexID];
    vUv = uFlip ? vec2(uv.x, 1.0 - uv.y) : uv;
    gl_Position = vec4(kPosition[gl_VertexID], 0.0, 1.0);
}
)GLSL";

const char* kFragmentShader = R"GLSL(
#version 330 core
uniform sampler2DRect uSource;
uniform vec2 uSize;
in vec2 vUv;
out vec4 fragColor;
void main()
{
    fragColor = vec4(texture(uSource, vUv * uSize).rgb, 1.0);
}
)GLSL";

auto Compile(GLenum type, const char* source) -> GLuint
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE)
    {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        GV_LOG_ERROR(kLogCategory, "video conversion shader failed to compile: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

RectToTexture::~RectToTexture()
{
    if (m_fbo != 0)
        glDeleteFramebuffers(1, &m_fbo);
    if (m_textures[0] != 0)
        glDeleteTextures(2, m_textures);
    if (m_vao != 0)
        glDeleteVertexArrays(1, &m_vao);
    if (m_program != 0)
        glDeleteProgram(m_program);
}

auto RectToTexture::EnsureProgram() -> bool
{
    if (m_program != 0)
    {
        return true;
    }
    GLuint vertex = Compile(GL_VERTEX_SHADER, kVertexShader);
    GLuint fragment = Compile(GL_FRAGMENT_SHADER, kFragmentShader);
    if (vertex == 0 || fragment == 0)
    {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return false;
    }
    m_program = glCreateProgram();
    glAttachShader(m_program, vertex);
    glAttachShader(m_program, fragment);
    glLinkProgram(m_program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE)
    {
        GV_LOG_ERROR(kLogCategory, "video conversion shader failed to link");
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }
    glUseProgram(m_program);
    glUniform1i(glGetUniformLocation(m_program, "uSource"), 0);
    glUseProgram(0);
    m_flipLocation = glGetUniformLocation(m_program, "uFlip");
    m_sizeLocation = glGetUniformLocation(m_program, "uSize");
    glGenVertexArrays(1, &m_vao);
    glGenFramebuffers(1, &m_fbo);
    return true;
}

void RectToTexture::EnsureTargets(int width, int height)
{
    if (width == m_width && height == m_height && m_textures[0] != 0)
    {
        return;
    }
    if (m_textures[0] != 0)
    {
        glDeleteTextures(2, m_textures);
    }
    glGenTextures(2, m_textures);
    for (GLuint texture : m_textures)
    {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    m_width = width;
    m_height = height;
    m_hasFrame = false;
}

auto RectToTexture::Convert(GLuint rectTexture, int width, int height, bool flipVertically) -> GLuint
{
    if (width <= 0 || height <= 0 || !EnsureProgram())
    {
        return 0;
    }
    EnsureTargets(width, height);
    const int target = m_hasFrame ? 1 - m_current : m_current;

    GLint previousFramebuffer = 0;
    GLint viewport[4];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_textures[target], 0);
    glViewport(0, 0, width, height);
    glDisable(GL_BLEND);
    glUseProgram(m_program);
    glUniform1i(m_flipLocation, flipVertically ? 1 : 0);
    glUniform2f(m_sizeLocation, static_cast<float>(width), static_cast<float>(height));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_RECTANGLE, rectTexture);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_RECTANGLE, 0);
    glUseProgram(0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

    m_current = target;
    m_hasFrame = true;
    return m_textures[m_current];
}

} // namespace gv::io
