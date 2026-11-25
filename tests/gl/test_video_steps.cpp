// A video branch's steps (VideoChain::RunSteps): fused built-in effects and ISF shaders, each step drawn into
// one of two textures in turn, the last into the branch's own. 16 x 16 throughout, so each pixel samples
// exactly one texel.

#include "engine/VideoChain.hpp"
#include "gl/GlTestContext.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace gv;
using engine::EffectPass;
using engine::VideoStep;
using model::NodeType;

namespace {

constexpr int kSize = 16;
using Pixels = std::vector<std::uint8_t>;

/// Colours that differ in every pixel and channel; opaque, as video is.
auto Colours() -> Pixels
{
    Pixels pixels;
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
            for (const int value : {x * 17, y * 17, (x * 7 + y * 13) % 256, 255})
                pixels.push_back(static_cast<std::uint8_t>(value));
    return pixels;
}

auto Shader(const std::string& source) -> std::shared_ptr<const isf::IsfShader>
{
    auto parsed = isf::ParseIsf(source);
    REQUIRE(std::holds_alternative<isf::IsfShader>(parsed));
    return std::make_shared<const isf::IsfShader>(std::get<isf::IsfShader>(std::move(parsed)));
}

auto Invert() -> std::shared_ptr<const isf::IsfShader>
{
    std::ifstream file(std::string(GV_TEST_FIXTURES_DIR) + "/isf/invert.fs");
    std::stringstream text;
    text << file.rdbuf();
    return Shader(text.str());
}

auto IsfStep(const std::shared_ptr<const isf::IsfShader>& shader, float amount) -> VideoStep
{
    return {{}, engine::IsfStep{"isf-1", "invert.fs", shader, {{"amount", {amount}}}}};
}

/// Two render targets, a video, and the chain: what a lane has for one branch.
struct Branch
{
    Branch()
    {
        video = test::Texture(kSize, kSize, Colours());
        for (auto* target : {&picture, &scratch})
        {
            target->texture = test::Texture(kSize, kSize, {});
            glGenFramebuffers(1, &target->fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, target->fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target->texture, 0);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    ~Branch()
    {
        programs.clear();
        for (auto* target : {&picture, &scratch})
        {
            glDeleteFramebuffers(1, &target->fbo);
            glDeleteTextures(1, &target->texture);
        }
        glDeleteTextures(1, &video);
    }

    /// Compiles `shader` as a lane does when its scene is prepared.
    void Compile(const std::shared_ptr<const isf::IsfShader>& shader)
    {
        programs.emplace(shader, std::make_unique<isf::IsfProgram>(*shader));
    }

    /// Runs `steps` and reads the branch's picture back.
    auto Run(const std::vector<VideoStep>& steps, bool withScratch = true) -> Pixels
    {
        chain.RunSteps(video, steps, programs, {}, picture, withScratch ? scratch : engine::RenderTarget{}, kSize, kSize);
        return Read(picture.fbo);
    }

    /// The old way: the chain alone, straight into the picture.
    auto RunChain(const std::vector<EffectPass>& passes) -> Pixels
    {
        chain.Run(video, passes, picture.fbo, kSize, kSize);
        return Read(picture.fbo);
    }

    static auto Read(GLuint fbo) -> Pixels
    {
        Pixels bytes(static_cast<size_t>(kSize * kSize * 4));
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return bytes;
    }

    engine::VideoChain chain;
    engine::IsfPrograms programs;
    GLuint video{0};
    engine::RenderTarget picture;
    engine::RenderTarget scratch;
};

const std::vector<EffectPass> kLook = {{NodeType::Contrast, 1.6f, 0.f, false},
                                       {NodeType::Saturation, 0.4f, 0.f, false},
                                       {NodeType::LumaGate, 0.3f, 0.1f, true},
                                       {NodeType::Opacity, 0.8f, 0.f, false}};

} // namespace

TEST_CASE("a branch of built-in effects only draws exactly what the chain drew before steps", "[gl]")
{
    REQUIRE_GL();
    Branch branch;
    const Pixels before = branch.RunChain(kLook);
    CHECK(branch.Run({{kLook}}, false) == before);
    CHECK(branch.Run({{kLook}}) == before);
    CHECK(branch.Run({}) == branch.RunChain({})); // no steps: the video as it is
}

TEST_CASE("a branch of video into ISF invert gives the inverted video", "[gl]")
{
    REQUIRE_GL();
    Branch branch;
    const auto invert = Invert();
    branch.Compile(invert);
    const Pixels in = Colours();
    const Pixels out = branch.Run({IsfStep(invert, 1.f)});
    for (size_t i = 0; i < in.size(); ++i)
    {
        INFO("byte " << i);
        CHECK(std::abs(out[i] - (i % 4 == 3 ? in[i] : 255 - in[i])) <= 1);
    }
}

TEST_CASE("effect, ISF, effect: each step reads the one before, and the last lands in the branch's picture", "[gl]")
{
    REQUIRE_GL();
    Branch branch;
    const auto invert = Invert();
    branch.Compile(invert);
    // Greyscale, inverted, then contrast 2 around mid-grey.
    const Pixels out = branch.Run({{{{NodeType::Saturation, 0.f, 0.f, false}}}, IsfStep(invert, 1.f), {{{NodeType::Contrast, 2.f, 0.f, false}}}});
    const Pixels in = Colours();
    for (size_t pixel = 0; pixel < in.size() / 4; ++pixel)
    {
        const float grey = (0.299f * in[pixel * 4] + 0.587f * in[pixel * 4 + 1] + 0.114f * in[pixel * 4 + 2]) / 255.f;
        const float inverted = std::round((1.f - grey) * 255.f) / 255.f; // stored as RGBA8 between steps
        const float want = std::clamp((inverted - 0.5f) * 2.f + 0.5f, 0.f, 1.f) * 255.f;
        INFO("pixel " << pixel);
        CHECK(std::abs(out[pixel * 4] - want) <= 2.5f);
        CHECK(out[pixel * 4] == out[pixel * 4 + 2]);
    }
}

TEST_CASE("an ISF step whose shader didn't compile, or wasn't compiled, passes the video through", "[gl]")
{
    REQUIRE_GL();
    Branch branch;
    const auto broken = Shader("/*{ \"INPUTS\": [ { \"NAME\": \"inputImage\", \"TYPE\": \"image\" } ] }*/\nvoid main() { gl_FragColor = nope; }\n");
    branch.Compile(broken);
    REQUIRE_FALSE(branch.programs.at(broken)->Ok());
    const Pixels plain = branch.RunChain(kLook);
    CHECK(branch.Run({IsfStep(broken, 1.f), {kLook}}) == plain);
    CHECK(branch.Run({{kLook}, IsfStep(Invert(), 1.f)}) == plain); // never compiled
}
