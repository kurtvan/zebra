// ISF shaders with passes and buffers (src/isf/IsfBuffers.hpp): named TARGETs between passes, PERSISTENT
// buffers kept from frame to frame, FLOAT buffers, WIDTH and HEIGHT expressions, and the node's Resolution.
// 16 x 16 throughout, so each output pixel samples exactly one texel.

#include "engine/VideoChain.hpp"
#include "gl/GlTestContext.hpp"
#include "isf/IsfBuffers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

using namespace gv;

namespace {

constexpr int kSize = 16;
using Pixels = std::vector<std::uint8_t>;

auto Shader(const std::string& source) -> std::shared_ptr<const isf::IsfShader>
{
    auto parsed = isf::ParseIsf(source);
    REQUIRE(std::holds_alternative<isf::IsfShader>(parsed));
    auto shader = std::make_shared<const isf::IsfShader>(std::get<isf::IsfShader>(std::move(parsed)));
    REQUIRE(isf::UnsupportedFeature(*shader).empty());
    return shader;
}

/// A red channel that changes along both axes, so a blur in either direction shows.
auto Pattern() -> Pixels
{
    Pixels pixels;
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
            for (const int value : {(x * 37 + y * 101) % 256, 0, 0, 255})
                pixels.push_back(static_cast<std::uint8_t>(value));
    return pixels;
}

/// An output texture and its framebuffer.
struct Target
{
    Target()
    {
        texture = test::Texture(kSize, kSize, {});
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    ~Target()
    {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &texture);
    }
    auto Read() const -> Pixels
    {
        Pixels bytes(static_cast<size_t>(kSize * kSize * 4));
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return bytes;
    }
    GLuint texture{0};
    GLuint fbo{0};
};

// Blurs across 3 pixels into a float buffer, then down 3 pixels from it into the output.
constexpr const char* kTwoPassBlur = R"(/*{
    "INPUTS": [ { "NAME": "inputImage", "TYPE": "image" } ],
    "PASSES": [ { "TARGET": "across", "FLOAT": true }, {} ]
}*/
void main()
{
    vec2 at = gl_FragCoord.xy;
    if (PASSINDEX == 0)
        gl_FragColor = (IMG_PIXEL(inputImage, at - vec2(1.0, 0.0)) + IMG_PIXEL(inputImage, at) + IMG_PIXEL(inputImage, at + vec2(1.0, 0.0))) / 3.0;
    else
        gl_FragColor = (IMG_PIXEL(across, at - vec2(0.0, 1.0)) + IMG_PIXEL(across, at) + IMG_PIXEL(across, at + vec2(0.0, 1.0))) / 3.0;
}
)";

// Adds a quarter to a persistent buffer every frame and shows it. FRAMEINDEX 0 seeds it, as simulations do.
constexpr const char* kAccumulate = R"(/*{
    "PASSES": [ { "TARGET": "total", "PERSISTENT": true, "FLOAT": true }, {} ]
}*/
void main()
{
    if (PASSINDEX == 0)
        gl_FragColor = FRAMEINDEX == 0 ? vec4(0.25) : IMG_THIS_PIXEL(total) + vec4(0.25);
    else
        gl_FragColor = vec4(IMG_THIS_PIXEL(total).rgb, 1.0);
}
)";

auto Red(const Pixels& pixels, int x, int y) -> int
{
    return pixels[static_cast<size_t>((y * kSize + x) * 4)];
}

} // namespace

TEST_CASE("a two-pass blur, across into a named buffer and then down, matches the same blur worked out by hand", "[gl]")
{
    REQUIRE_GL();
    const auto shader = Shader(kTwoPassBlur);
    isf::IsfProgram program(*shader);
    REQUIRE(program.Ok());
    const Pixels pattern = Pattern();
    const GLuint video = test::Texture(kSize, kSize, pattern);
    Target out;
    {
        isf::IsfBuffers buffers;
        buffers.Render(program, {{"inputImage", video}}, {}, {}, out.fbo, kSize, kSize);
        CHECK(buffers.BufferSize("across") == std::pair{kSize, kSize});
    }
    const auto clamped = [](int v) { return std::clamp(v, 0, kSize - 1); };
    const auto source = [&](int x, int y) { return pattern[static_cast<size_t>((clamped(y) * kSize + clamped(x)) * 4)] / 255.0; };
    const auto across = [&](int x, int y) { return (source(x - 1, y) + source(x, y) + source(x + 1, y)) / 3.0; };
    const Pixels got = out.Read();
    for (int y = 0; y < kSize; ++y)
    {
        for (int x = 0; x < kSize; ++x)
        {
            INFO("pixel " << x << ", " << y);
            const double expected = (across(x, clamped(y - 1)) + across(x, y) + across(x, clamped(y + 1))) / 3.0 * 255.0;
            CHECK(std::abs(Red(got, x, y) - expected) <= 1.0);
        }
    }
    glDeleteTextures(1, &video);
}

TEST_CASE("a persistent buffer builds up frame by frame, carries on after a cut away and back, and starts fresh when cleared or unloaded", "[gl]")
{
    REQUIRE_GL();
    const auto shader = Shader(kAccumulate);
    engine::IsfPrograms programs;
    REQUIRE(engine::CompileIsf(programs, shader).empty());
    engine::VideoChain chain;
    Target picture;
    Target scratch;
    const std::vector<engine::VideoStep> steps = {{{}, engine::IsfStep{"isf-1", "accumulate.fs", shader, {}}}};
    const int before = isf::IsfBuffers::LiveTextures();
    auto buffers = std::make_unique<engine::IsfNodeBuffers>(); // what a lane keeps for a loaded scene
    auto frame = [&] {
        chain.RunSteps(0, steps, programs, *buffers, {}, {picture.texture, picture.fbo}, {scratch.texture, scratch.fbo}, kSize, kSize);
        return Red(picture.Read(), 5, 7);
    };
    CHECK(frame() == 64);  // 0.25
    CHECK(frame() == 128); // 0.5
    CHECK(frame() == 191); // 0.75
    CHECK(buffers->at("isf-1").Frames() == 3);
    CHECK(isf::IsfBuffers::LiveTextures() == before + 2); // the buffer's two halves

    // Cut away: another scene draws (here, the same targets), and this one, still loaded, doesn't. Back: it
    // carries on where it was.
    chain.Run(0, {}, picture.fbo, kSize, kSize);
    CHECK(frame() == 255); // 1.0

    // Reset: back to the first frame.
    buffers->at("isf-1").Clear();
    CHECK(buffers->at("isf-1").Frames() == 0);
    CHECK(frame() == 64);

    // Unloaded: the textures go, and the next load starts fresh.
    buffers = std::make_unique<engine::IsfNodeBuffers>();
    CHECK(isf::IsfBuffers::LiveTextures() == before);
    CHECK(frame() == 64);
}

TEST_CASE("Resolution renders the buffers and picture smaller, and WIDTH and HEIGHT expressions size them, scaled up to fill", "[gl]")
{
    REQUIRE_GL();
    const auto shader = Shader(R"(/*{
        "PASSES": [ { "TARGET": "small", "PERSISTENT": true, "WIDTH": "$WIDTH / 2", "HEIGHT": "($HEIGHT - 4) / 3" },
                    { "TARGET": "whole" }, {} ]
    }*/
    void main()
    {
        if (PASSINDEX < 2)
            gl_FragColor = vec4(1.0);
        else
            gl_FragColor = vec4(RENDERSIZE / 255.0, IMG_THIS_PIXEL(small).r, 1.0);
    })");
    isf::IsfProgram program(*shader);
    REQUIRE(program.Ok());
    Target out;
    for (const auto& [divisor, size] : {std::pair{1, 16}, std::pair{2, 8}, std::pair{4, 4}})
    {
        INFO("divisor " << divisor);
        isf::IsfBuffers buffers;
        buffers.Render(program, {}, {}, {}, out.fbo, kSize, kSize, divisor);
        CHECK(buffers.BufferSize("whole") == std::pair{size, size});
        CHECK(buffers.BufferSize("small") == std::pair{size / 2, std::max((size - 4) / 3, 1)});
        const Pixels got = out.Read();
        // The last pass rendered at the smaller size (RENDERSIZE), and fills the whole output.
        for (const auto& [x, y] : {std::pair{0, 0}, std::pair{15, 15}, std::pair{8, 3}})
        {
            CHECK(got[static_cast<size_t>((y * kSize + x) * 4)] == size);
            CHECK(got[static_cast<size_t>((y * kSize + x) * 4 + 2)] == 255);
        }
    }
}
