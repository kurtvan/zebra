#include "isf/IsfBuffers.hpp"

#include "isf/IsfSize.hpp"

#include <algorithm>
#include <atomic>

namespace gv::isf {

namespace {

std::atomic<int> g_liveTextures{0};

} // namespace

IsfBuffers::~IsfBuffers()
{
    for (auto& [name, buffer] : m_buffers)
        Free(buffer);
    Free(m_picture);
}

auto IsfBuffers::LiveTextures() -> int
{
    return g_liveTextures.load();
}

void IsfBuffers::Free(Buffer& buffer)
{
    for (int i = 0; i < 2; ++i)
    {
        if (buffer.texture[i] == 0)
            continue;
        glDeleteFramebuffers(1, &buffer.fbo[i]);
        glDeleteTextures(1, &buffer.texture[i]);
        g_liveTextures--;
    }
    buffer = {};
}

void IsfBuffers::ClearTexture(GLuint fbo)
{
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void IsfBuffers::Fit(Buffer& buffer, int width, int height, bool floating, int textures)
{
    const bool fits = buffer.width == width && buffer.height == height && buffer.floating == floating &&
                      (buffer.texture[1] != 0) == (textures == 2) && buffer.texture[0] != 0;
    if (fits)
        return;
    Free(buffer);
    buffer.width = width;
    buffer.height = height;
    buffer.floating = floating;
    for (int i = 0; i < textures; ++i)
    {
        glGenTextures(1, &buffer.texture[i]);
        glBindTexture(GL_TEXTURE_2D, buffer.texture[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, floating ? GL_RGBA32F : GL_RGBA8, width, height, 0, GL_RGBA, floating ? GL_FLOAT : GL_UNSIGNED_BYTE,
                     nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        glGenFramebuffers(1, &buffer.fbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, buffer.fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, buffer.texture[i], 0);
        ClearTexture(buffer.fbo[i]);
        g_liveTextures++;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void IsfBuffers::Clear()
{
    for (auto& [name, buffer] : m_buffers)
    {
        for (const GLuint fbo : buffer.fbo)
        {
            if (fbo != 0)
                ClearTexture(fbo);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_frames = 0;
}

void IsfBuffers::Render(IsfProgram& program, std::map<std::string, GLuint> images, const std::map<std::string, std::vector<float>>& values,
                        IsfClock clock, GLuint fbo, int width, int height, int divisor)
{
    if (!program.Ok())
        return;
    // Buffers a reloaded file no longer draws into go.
    const auto& passes = program.Passes();
    std::erase_if(m_buffers, [&](auto& entry) {
        const bool used = std::ranges::any_of(passes, [&](const IsfPass& pass) { return pass.target == entry.first; });
        if (!used)
            Free(entry.second);
        return !used;
    });

    divisor = std::max(divisor, 1);
    const auto variables = SizeVariables(program.Inputs(), values, std::max(width / divisor, 1), std::max(height / divisor, 1));
    clock.frameIndex = m_frames;
    const std::vector<IsfPass> one(1);
    const auto& drawn = passes.empty() ? one : passes;
    // Every buffer first, so a pass that reads one a later pass draws reads black on the first frame.
    for (const auto& pass : drawn)
    {
        if (!pass.target.empty())
        {
            const auto [passWidth, passHeight] = PassSize(pass, variables);
            Fit(m_buffers[pass.target], passWidth, passHeight, pass.floatTarget);
        }
    }
    GLuint pictureFbo = 0; // where the last pass's picture is, if not in `fbo`
    std::pair<int, int> pictureSize;
    for (size_t i = 0; i < drawn.size(); ++i)
    {
        const IsfPass& pass = drawn[i];
        const auto [passWidth, passHeight] = PassSize(pass, variables);
        for (const auto& [name, buffer] : m_buffers)
            images[name] = buffer.texture[buffer.front];
        const bool last = i + 1 == drawn.size();
        if (!pass.target.empty())
        {
            Buffer& buffer = m_buffers[pass.target];
            Fit(buffer, passWidth, passHeight, pass.floatTarget); // two passes may size one buffer differently
            images[pass.target] = buffer.texture[buffer.front];
            const int back = 1 - buffer.front;
            program.Render(images, values, clock, buffer.fbo[back], passWidth, passHeight, static_cast<int>(i));
            buffer.front = back;
            pictureFbo = buffer.fbo[back];
        }
        else if (last && passWidth == width && passHeight == height)
        {
            program.Render(images, values, clock, fbo, width, height, static_cast<int>(i));
            pictureFbo = 0;
        }
        else
        {
            Fit(m_picture, passWidth, passHeight, pass.floatTarget, 1);
            program.Render(images, values, clock, m_picture.fbo[0], passWidth, passHeight, static_cast<int>(i));
            pictureFbo = m_picture.fbo[0];
        }
        pictureSize = {passWidth, passHeight};
    }
    if (pictureFbo != 0)
    {
        // Scaled up (or copied) into the output, smoothly.
        glBindFramebuffer(GL_READ_FRAMEBUFFER, pictureFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
        glBlitFramebuffer(0, 0, pictureSize.first, pictureSize.second, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    m_frames++;
}

} // namespace gv::isf
