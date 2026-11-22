// Proves the effect chain (engine/VideoChain) plus the layer blend draws what the old shaders drew (N2.6).
//
// Each case renders a fixed test image two ways over the same background and compares the pixels:
// - reference: the shaders the app used before N2, copied verbatim below, blending the video straight in;
// - new: VideoChain::Run into an RGBA8 branch texture, then that texture alpha-blended in, as projectM's layer
//   blend (patch 0002) and VideoChain::Composite do.
// The only difference allowed is the RGBA8 branch texture's rounding: at most 1/255 per channel.

#include "engine/VideoChain.hpp"
#include "gl/GlTestContext.hpp"

#include <SDL.h>

#include <OpenGL/gl3.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

using gv::engine::EffectPass;
using gv::engine::VideoChain;
using gv::model::NodeType;
using gv::test::Context;
using gv::test::Texture;

namespace {

constexpr int kWidth = 256;
constexpr int kHeight = 192;
constexpr int kVideoWidth = 320; // a different size, so the video is resampled as in the app
constexpr int kVideoHeight = 180;

constexpr const char* kVertexShader = R"GLSL(#version 330 core
out vec2 vUv;
void main()
{
    vec2 position = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = position;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

// Patch 0002's FeedbackBlend fragment shader with 0004 (contrast, saturation) and 0005 (invert) applied,
// as they were before N2.
constexpr const char* kOldFeedbackShader = R"GLSL(
#version 330 core
uniform sampler2D uTexture;
uniform float uMix;
uniform float uLumaMin;
uniform float uContrast;
uniform float uSaturation;
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
    float gate = smoothstep(uLumaMin - 0.05, uLumaMin + 0.05, luma);
    if (uLumaInvert)
        gate = 1.0 - gate;
    fragColor = vec4(color, uMix * gate);
}
)GLSL";

// engine/VideoOverlay.cpp's fragment shader before N2.
constexpr const char* kOldOverlayShader = R"GLSL(#version 330 core
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

// Patch 0002's LayerBlend fragment shader (drawn with the same blend state as the old shaders).
constexpr const char* kLayerBlendShader = R"GLSL(#version 330 core
uniform sampler2D uTexture;
in vec2 vUv;
out vec4 fragColor;
void main()
{
    fragColor = texture(uTexture, vUv);
}
)GLSL";

auto Program(const char* fragmentSource) -> GLuint
{
    auto compile = [](GLenum type, const char* source) {
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        REQUIRE(ok);
        return shader;
    };
    const GLuint program = glCreateProgram();
    const GLuint vertex = compile(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragmentSource);
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    REQUIRE(ok);
    return program;
}

/// Gradients, a colour wheel and noise, with partial alpha (which the chain ignores, as the old shaders did).
auto TestImage() -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> pixels(static_cast<size_t>(kVideoWidth * kVideoHeight * 4));
    std::mt19937 random(1234);
    std::uniform_int_distribution<int> byte(0, 255);
    for (int y = 0; y < kVideoHeight; ++y)
    {
        for (int x = 0; x < kVideoWidth; ++x)
        {
            float r = 0, g = 0, b = 0;
            const float u = static_cast<float>(x) / (kVideoWidth - 1);
            const float v = static_cast<float>(y) / (kVideoHeight - 1);
            if (x < kVideoWidth / 3)
            {
                r = g = b = v; // grey ramp
                if (x > kVideoWidth / 6)
                    r = u * 3.0f, g = v, b = 1.0f - v; // colour ramp
            }
            else if (x < 2 * kVideoWidth / 3)
            {
                const float dx = u - 0.5f, dy = v - 0.5f;
                const float hue = std::atan2(dy, dx) / 6.2831853f + 0.5f;
                const float radius = std::min(1.0f, std::sqrt(dx * dx + dy * dy) * 4.0f);
                r = std::clamp(std::fabs(hue * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f) * radius + (1 - radius) * 0.5f;
                g = std::clamp(2.0f - std::fabs(hue * 6.0f - 2.0f), 0.0f, 1.0f) * radius + (1 - radius) * 0.5f;
                b = std::clamp(2.0f - std::fabs(hue * 6.0f - 4.0f), 0.0f, 1.0f) * radius + (1 - radius) * 0.5f;
            }
            else
            {
                r = static_cast<float>(byte(random)) / 255.0f;
                g = static_cast<float>(byte(random)) / 255.0f;
                b = static_cast<float>(byte(random)) / 255.0f;
            }
            auto* pixel = &pixels[static_cast<size_t>((y * kVideoWidth + x) * 4)];
            pixel[0] = static_cast<std::uint8_t>(std::lround(std::clamp(r, 0.0f, 1.0f) * 255));
            pixel[1] = static_cast<std::uint8_t>(std::lround(std::clamp(g, 0.0f, 1.0f) * 255));
            pixel[2] = static_cast<std::uint8_t>(std::lround(std::clamp(b, 0.0f, 1.0f) * 255));
            pixel[3] = static_cast<std::uint8_t>((x * 7 + y * 3) % 256);
        }
    }
    return pixels;
}

/// The frame the video is blended into: an opaque diagonal gradient.
auto Background() -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> pixels(static_cast<size_t>(kWidth * kHeight * 4));
    for (int y = 0; y < kHeight; ++y)
    {
        for (int x = 0; x < kWidth; ++x)
        {
            auto* pixel = &pixels[static_cast<size_t>((y * kWidth + x) * 4)];
            pixel[0] = static_cast<std::uint8_t>(x * 255 / (kWidth - 1));
            pixel[1] = static_cast<std::uint8_t>(y * 255 / (kHeight - 1));
            pixel[2] = static_cast<std::uint8_t>(255 - (x + y) * 255 / (kWidth + kHeight - 2));
            pixel[3] = 255;
        }
    }
    return pixels;
}

struct Look
{
    float contrast{1.0f};
    float saturation{1.0f};
    float lumaMin{0.0f};
    float softness{0.0f};
    bool invert{false};
    float mix{1.0f};
};

/// Draws everything into RGBA8 targets and reads them back.
class Bench
{
public:
    Bench()
        : m_video(Texture(kVideoWidth, kVideoHeight, TestImage()))
        , m_background(Background())
        , m_oldFeedback(Program(kOldFeedbackShader))
        , m_oldOverlay(Program(kOldOverlayShader))
        , m_layerBlend(Program(kLayerBlendShader))
    {
        m_target = Texture(kWidth, kHeight, {});
        glGenFramebuffers(1, &m_targetFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_targetFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_target, 0);
        m_branch = Texture(kWidth, kHeight, {});
        glGenFramebuffers(1, &m_branchFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_branchFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_branch, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glGenVertexArrays(1, &m_vao);
        m_chain = std::make_unique<VideoChain>();
    }
    ~Bench()
    {
        m_chain.reset();
        glDeleteVertexArrays(1, &m_vao);
        glDeleteProgram(m_oldFeedback);
        glDeleteProgram(m_oldOverlay);
        glDeleteProgram(m_layerBlend);
        glDeleteFramebuffers(1, &m_targetFbo);
        glDeleteFramebuffers(1, &m_branchFbo);
        GLuint textures[] = {m_video, m_target, m_branch};
        glDeleteTextures(3, textures);
    }

    /// The old feedback blend over the background.
    auto OldFeedback(const Look& look) -> std::vector<std::uint8_t>
    {
        ResetTarget();
        glUseProgram(m_oldFeedback);
        Uniform(m_oldFeedback, "uMix", look.mix);
        Uniform(m_oldFeedback, "uLumaMin", look.lumaMin);
        Uniform(m_oldFeedback, "uContrast", look.contrast);
        Uniform(m_oldFeedback, "uSaturation", look.saturation);
        glUniform1i(glGetUniformLocation(m_oldFeedback, "uLumaInvert"), look.invert ? 1 : 0);
        DrawBlended(m_oldFeedback, m_video);
        return Read();
    }

    /// The old overlay over the background.
    auto OldOverlay(const Look& look) -> std::vector<std::uint8_t>
    {
        ResetTarget();
        glUseProgram(m_oldOverlay);
        Uniform(m_oldOverlay, "uMix", look.mix);
        Uniform(m_oldOverlay, "uLumaMin", look.lumaMin);
        Uniform(m_oldOverlay, "uContrast", look.contrast);
        Uniform(m_oldOverlay, "uSaturation", look.saturation);
        Uniform(m_oldOverlay, "uSoftness", look.softness);
        glUniform1i(glGetUniformLocation(m_oldOverlay, "uLumaInvert"), look.invert ? 1 : 0);
        DrawBlended(m_oldOverlay, m_video);
        return Read();
    }

    /// The chain into the branch texture, then projectM's layer blend (feedback) or Composite (on top).
    auto New(const std::vector<EffectPass>& passes, bool composite) -> std::vector<std::uint8_t>
    {
        m_chain->Run(m_video, passes, m_branchFbo, kWidth, kHeight);
        ResetTarget();
        if (composite)
        {
            m_chain->Composite(m_branch);
        }
        else
        {
            glUseProgram(m_layerBlend);
            DrawBlended(m_layerBlend, m_branch);
        }
        return Read();
    }

private:
    void ResetTarget()
    {
        glBindTexture(GL_TEXTURE_2D, m_target);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, m_background.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, m_targetFbo);
        glViewport(0, 0, kWidth, kHeight);
    }

    static void Uniform(GLuint program, const char* name, float value)
    {
        glUniform1f(glGetUniformLocation(program, name), value);
    }

    /// The old shaders' and the layer blend's state: colour alpha-blended, destination alpha kept.
    void DrawBlended(GLuint program, GLuint texture)
    {
        glUniform1i(glGetUniformLocation(program, "uTexture"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glEnable(GL_BLEND);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glDisable(GL_BLEND);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
    }

    auto Read() -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> pixels(static_cast<size_t>(kWidth * kHeight * 4));
        glBindFramebuffer(GL_FRAMEBUFFER, m_targetFbo);
        glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return pixels;
    }

    GLuint m_video;
    std::vector<std::uint8_t> m_background;
    GLuint m_oldFeedback;
    GLuint m_oldOverlay;
    GLuint m_layerBlend;
    GLuint m_target{0};
    GLuint m_targetFbo{0};
    GLuint m_branch{0};
    GLuint m_branchFbo{0};
    GLuint m_vao{0};
    std::unique_ptr<VideoChain> m_chain;
};

/// The chain a converted scene compiles to (model/Classic.cpp): contrast, saturation, gate, mix.
auto Chain(const Look& look) -> std::vector<EffectPass>
{
    return {{NodeType::Contrast, look.contrast, 0.0f, false},
            {NodeType::Saturation, look.saturation, 0.0f, false},
            {NodeType::LumaGate, look.lumaMin, look.softness, look.invert},
            {NodeType::Opacity, look.mix, 0.0f, false}};
}

/// About 200 looks: the defaults, the extremes, and random ones (seeded, so every run is the same).
auto Looks() -> std::vector<Look>
{
    std::vector<Look> looks = {
        {},
        {0.0f, 1.0f, 0.0f, 0.0f, false, 1.0f},   // flat grey
        {100.0f, 0.0f, 0.5f, 0.0f, false, 1.0f}, // hard black and white
        {100.0f, 2.0f, 0.0f, 0.0f, true, 1.0f},
        {1.0f, 2.0f, 1.0f, 0.0f, false, 1.0f}, // gate at the top
        {1.0f, 1.0f, 0.0f, 1.0f, true, 0.5f},  // softest gate
        {1.0f, 1.0f, 0.3f, 0.0f, false, 0.0f}, // mix 0
    };
    std::mt19937 random(42);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    while (looks.size() < 200)
    {
        Look look;
        const float pick = unit(random);
        look.contrast = pick < 0.1f ? 0.0f : pick < 0.2f ? 100.0f : pick < 0.35f ? unit(random) * 100.0f : unit(random) * 3.0f;
        look.saturation = unit(random) < 0.15f ? 0.0f : unit(random) * 2.0f;
        look.lumaMin = unit(random);
        look.softness = unit(random) < 0.4f ? 0.0f : unit(random);
        look.invert = unit(random) < 0.5f;
        look.mix = unit(random) < 0.2f ? 1.0f : unit(random);
        looks.push_back(look);
    }
    return looks;
}

/// The largest difference of any channel, and how many pixels differ at all.
struct Difference
{
    int largest{0};
    int pixels{0};
    int x{-1};
    int y{-1};
};

auto Compare(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) -> Difference
{
    Difference difference;
    for (size_t i = 0; i < a.size(); i += 4)
    {
        int pixelLargest = 0;
        for (size_t c = 0; c < 4; ++c)
            pixelLargest = std::max(pixelLargest, std::abs(static_cast<int>(a[i + c]) - static_cast<int>(b[i + c])));
        if (pixelLargest > 0)
            difference.pixels++;
        if (pixelLargest > difference.largest)
        {
            difference.largest = pixelLargest;
            difference.x = static_cast<int>(i / 4) % kWidth;
            difference.y = static_cast<int>(i / 4) / kWidth;
        }
    }
    return difference;
}

auto Describe(const Look& look) -> std::string
{
    return "contrast " + std::to_string(look.contrast) + ", saturation " + std::to_string(look.saturation) + ", gate " +
           std::to_string(look.lumaMin) + " softness " + std::to_string(look.softness) + (look.invert ? " inverted" : "") +
           ", mix " + std::to_string(look.mix);
}

} // namespace

TEST_CASE("the effect chain and layer blend draw the old feedback blend", "[gl]")
{
    REQUIRE_GL();
    Bench bench;
    int largest = 0;
    for (Look look : Looks())
    {
        look.softness = 0.1f; // the old blend's fixed edge, smoothstep(lumaMin - 0.05, lumaMin + 0.05)
        const auto difference = Compare(bench.OldFeedback(look), bench.New(Chain(look), false));
        INFO(Describe(look) << ": " << difference.pixels << " pixels differ, worst at " << difference.x << "," << difference.y);
        CHECK(difference.largest <= 1);
        largest = std::max(largest, difference.largest);
    }
    CHECK(largest <= 1);
}

TEST_CASE("the effect chain and composite draw the old overlay", "[gl]")
{
    REQUIRE_GL();
    Bench bench;
    for (const Look& look : Looks())
    {
        const auto difference = Compare(bench.OldOverlay(look), bench.New(Chain(look), true));
        INFO(Describe(look) << ": " << difference.pixels << " pixels differ, worst at " << difference.x << "," << difference.y);
        CHECK(difference.largest <= 1);
    }
}

TEST_CASE("a chain without passes draws the video as it is", "[gl]")
{
    REQUIRE_GL();
    Bench bench;
    const Look identity; // contrast 1, saturation 1, gate at 0 with no softness, mix 1: every pixel passes
    CHECK(Compare(bench.OldOverlay(identity), bench.New({}, true)).largest <= 1);
}
