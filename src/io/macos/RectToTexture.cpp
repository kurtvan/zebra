#include "io/macos/RectToTexture.hpp"

#include "common/log.hpp"

#include <algorithm>
#include <cmath>

namespace gv::io {

namespace {

constexpr const char* kLogCategory = "video";

const char* kVertexShader = R"GLSL(
#version 330 core
const vec2 kPosition[4] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));
const vec2 kUv[4] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0));
out vec2 vUv;
void main()
{
    vUv = kUv[gl_VertexID];
    gl_Position = vec4(kPosition[gl_VertexID], 0.0, 1.0);
}
)GLSL";

const char* kFragmentShader = R"GLSL(
#version 330 core
uniform sampler2DRect uSource;
uniform vec2 uSize;        // stored frame, pixels
uniform bool uTopDown;     // the stored frame's first row is the top
uniform vec2 uScale;       // output -> upright picture, around the centre (the fit)
uniform vec2 uPictureSize; // upright picture, pixels
uniform mat3 uToStored;    // upright picture -> stored frame, top-down pixels
in vec2 vUv;               // output, bottom-up
out vec4 fragColor;
void main()
{
    vec2 picture = (vec2(vUv.x, 1.0 - vUv.y) - 0.5) * uScale + 0.5;
    if (any(lessThan(picture, vec2(0.0))) || any(greaterThan(picture, vec2(1.0))))
    {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec2 stored = (uToStored * vec3(picture * uPictureSize, 1.0)).xy;
    if (!uTopDown)
        stored.y = uSize.y - stored.y;
    fragColor = vec4(texture(uSource, stored).rgb, 1.0);
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
    m_topDownLocation = glGetUniformLocation(m_program, "uTopDown");
    m_sizeLocation = glGetUniformLocation(m_program, "uSize");
    m_scaleLocation = glGetUniformLocation(m_program, "uScale");
    m_pictureSizeLocation = glGetUniformLocation(m_program, "uPictureSize");
    m_toStoredLocation = glGetUniformLocation(m_program, "uToStored");
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

auto RectToTexture::Convert(GLuint rectTexture, int width, int height, bool topDown, const Placement& placement) -> GLuint
{
    if (width <= 0 || height <= 0 || !EnsureProgram())
    {
        return 0;
    }

    // The upright picture: the stored frame's corners through the transform, moved to the origin.
    const double* t = placement.transform;
    double minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
    for (const auto [x, y] : {std::pair{0.0, 0.0}, std::pair{double(width), 0.0}, std::pair{0.0, double(height)}, std::pair{double(width), double(height)}})
    {
        const double px = t[0] * x + t[2] * y + t[4];
        const double py = t[1] * x + t[3] * y + t[5];
        minX = std::min(minX, px);
        maxX = std::max(maxX, px);
        minY = std::min(minY, py);
        maxY = std::max(maxY, py);
    }
    const double determinant = t[0] * t[3] - t[1] * t[2];
    const double pictureWidth = maxX - minX;
    const double pictureHeight = maxY - minY;
    if (std::abs(determinant) < 1e-9 || pictureWidth < 1.0 || pictureHeight < 1.0)
    {
        return 0;
    }
    // Inverse: picture (top-down pixels) + origin -> stored frame. Column-major for GLSL.
    const double i00 = t[3] / determinant, i01 = -t[2] / determinant;
    const double i10 = -t[1] / determinant, i11 = t[0] / determinant;
    const double ox = minX - t[4], oy = minY - t[5];
    const float toStored[9] = {float(i00), float(i10), 0.0f, float(i01), float(i11), 0.0f,
                               float(i00 * ox + i01 * oy), float(i10 * ox + i11 * oy), 1.0f};

    const int outWidth = placement.width > 0 ? placement.width : static_cast<int>(std::lround(pictureWidth));
    const int outHeight = placement.height > 0 ? placement.height : static_cast<int>(std::lround(pictureHeight));
    const double pictureAspect = pictureWidth / pictureHeight;
    const double outAspect = static_cast<double>(outWidth) / outHeight;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    if (placement.fit == VideoFit::Fill)
    {
        if (pictureAspect > outAspect)
            scaleX = static_cast<float>(outAspect / pictureAspect);
        else
            scaleY = static_cast<float>(pictureAspect / outAspect);
    }
    else if (placement.fit == VideoFit::Fit)
    {
        if (pictureAspect > outAspect)
            scaleY = static_cast<float>(pictureAspect / outAspect);
        else
            scaleX = static_cast<float>(outAspect / pictureAspect);
    }

    EnsureTargets(outWidth, outHeight);
    const int target = m_hasFrame ? 1 - m_current : m_current;

    GLint previousFramebuffer = 0;
    GLint viewport[4];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_textures[target], 0);
    glViewport(0, 0, outWidth, outHeight);
    glDisable(GL_BLEND);
    glUseProgram(m_program);
    glUniform1i(m_topDownLocation, topDown ? 1 : 0);
    glUniform2f(m_sizeLocation, static_cast<float>(width), static_cast<float>(height));
    glUniform2f(m_scaleLocation, scaleX, scaleY);
    glUniform2f(m_pictureSizeLocation, static_cast<float>(pictureWidth), static_cast<float>(pictureHeight));
    glUniformMatrix3fv(m_toStoredLocation, 1, GL_FALSE, toStored);
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
