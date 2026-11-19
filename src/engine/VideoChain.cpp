#include "engine/VideoChain.hpp"

#include "common/log.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace gv::engine {

namespace {

constexpr const char* kLogCategory = "video-chain";

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

// The effect chain: every pass in order, each running its node type's shader (NodeDef::shader).
auto ChainShader() -> std::string
{
    std::string source = R"GLSL(#version 330 core
uniform sampler2D uVideo;
uniform int uCount;
uniform int uType[16];
uniform vec2 uParam[16];
uniform int uFlag[16];
in vec2 vUv;
out vec4 fragColor;
void main()
{
    const vec3 lumaWeights = vec3(0.299, 0.587, 0.114);
    vec3 color = texture(uVideo, vUv).rgb;
    float alpha = 1.0;
    for (int i = 0; i < uCount; ++i)
    {
        vec2 param = uParam[i];
        bool flag = uFlag[i] != 0;
)GLSL";
    const char* keyword = "if";
    for (const auto& def : model::NodeDefs())
    {
        if (def.shader == nullptr)
            continue;
        source += std::string("        ") + keyword + " (uType[i] == " + std::to_string(static_cast<int>(def.type)) + ")\n        {\n            " +
                  def.shader + "\n        }\n";
        keyword = "else if";
    }
    source += R"GLSL(    }
    fragColor = vec4(clamp(color, 0.0, 1.0), alpha);
}
)GLSL";
    return source;
}

constexpr const char* kCompositeShader = R"GLSL(#version 330 core
uniform sampler2D uTexture;
in vec2 vUv;
out vec4 fragColor;
void main()
{
    fragColor = texture(uTexture, vUv);
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

auto Link(const char* fragmentSource) -> GLuint
{
    const GLuint vertex = Compile(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragment = Compile(GL_FRAGMENT_SHADER, fragmentSource);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDetachShader(program, vertex);
    glDetachShader(program, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024] = {};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        GV_LOG_ERROR(kLogCategory, "program failed to link: %s", log);
    }
    return program;
}

} // namespace

VideoChain::VideoChain()
{
    m_chainProgram = Link(ChainShader().c_str());
    m_videoLocation = glGetUniformLocation(m_chainProgram, "uVideo");
    m_countLocation = glGetUniformLocation(m_chainProgram, "uCount");
    m_typeLocation = glGetUniformLocation(m_chainProgram, "uType[0]");
    m_paramLocation = glGetUniformLocation(m_chainProgram, "uParam[0]");
    m_flagLocation = glGetUniformLocation(m_chainProgram, "uFlag[0]");
    m_compositeProgram = Link(kCompositeShader);
    m_compositeTextureLocation = glGetUniformLocation(m_compositeProgram, "uTexture");
    glGenVertexArrays(1, &m_vao);
}

VideoChain::~VideoChain()
{
    glDeleteVertexArrays(1, &m_vao);
    glDeleteProgram(m_chainProgram);
    glDeleteProgram(m_compositeProgram);
}

void VideoChain::Run(GLuint video, const std::vector<EffectPass>& passes, GLuint fbo, int width, int height)
{
    const int count = std::min(static_cast<int>(passes.size()), kMaxPasses);
    std::array<GLint, kMaxPasses> types{};
    std::array<GLfloat, kMaxPasses * 2> params{};
    std::array<GLint, kMaxPasses> flags{};
    for (int i = 0; i < count; ++i)
    {
        const auto& pass = passes[static_cast<size_t>(i)];
        types[static_cast<size_t>(i)] = static_cast<GLint>(pass.type);
        params[static_cast<size_t>(i * 2)] = pass.a;
        params[static_cast<size_t>(i * 2 + 1)] = pass.b;
        flags[static_cast<size_t>(i)] = pass.flag ? 1 : 0;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(m_chainProgram);
    glUniform1i(m_videoLocation, 0);
    glUniform1i(m_countLocation, count);
    if (count > 0)
    {
        glUniform1iv(m_typeLocation, count, types.data());
        glUniform2fv(m_paramLocation, count, params.data());
        glUniform1iv(m_flagLocation, count, flags.data());
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, video);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void VideoChain::Composite(GLuint texture)
{
    glUseProgram(m_compositeProgram);
    glUniform1i(m_compositeTextureLocation, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    // Colour over the frame; the frame's alpha stays as it was.
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
