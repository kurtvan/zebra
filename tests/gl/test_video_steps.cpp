// A video branch's steps (VideoChain::RunSteps): fused built-in effects and ISF shaders, each step drawn into
// one of two textures in turn, the last into the branch's own. 16 x 16 throughout, so each pixel samples
// exactly one texel.

#include "engine/RenderPlan.hpp"
#include "engine/VideoChain.hpp"
#include "gl/GlTestContext.hpp"
#include "model/nodes/IsfShaderNode.hpp"

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
        chain.RunSteps(video, steps, programs, buffers, {}, picture, withScratch ? scratch : engine::RenderTarget{}, kSize, kSize);
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
    engine::IsfNodeBuffers buffers;
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

TEST_CASE("a bad edit of a shader keeps drawing the last version that compiled, until a good one comes", "[gl]")
{
    REQUIRE_GL();
    Branch branch;
    const auto invert = Invert();
    REQUIRE(engine::CompileIsf(branch.programs, invert).empty());
    const Pixels inverted = branch.Run({IsfStep(invert, 1.f)});

    // Saved with a mistake: an error to show on the node, but the old program draws it.
    const auto broken = Shader("/*{ \"INPUTS\": [ { \"NAME\": \"inputImage\", \"TYPE\": \"image\" } ] }*/\nvoid main() { gl_FragColor = nope; }\n");
    CHECK_FALSE(engine::CompileIsf(branch.programs, broken, invert).empty());
    branch.programs.erase(invert); // the lane drops the old version once no plan uses it
    CHECK(branch.Run({IsfStep(broken, 1.f)}) == inverted);

    // Fixed: the new version draws.
    const auto fixed = Shader("/*{ \"INPUTS\": [ { \"NAME\": \"inputImage\", \"TYPE\": \"image\" } ] }*/\n"
                              "void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n");
    CHECK(engine::CompileIsf(branch.programs, fixed, broken).empty());
    const Pixels red = branch.Run({IsfStep(fixed, 1.f)});
    CHECK(red != inverted);
    CHECK(red[0] == 255);
    CHECK(red[1] == 0);

    // Without a working earlier version, a bad edit passes the video through.
    engine::IsfPrograms fresh;
    CHECK_FALSE(engine::CompileIsf(fresh, broken).empty());
    CHECK_FALSE(fresh.at(broken)->Ok());
}

namespace {

/// The frame a scene with no preset publishes: its picture drawn straight in (VideoChain::DrawPicture).
struct Output
{
    Output()
    {
        target.texture = test::Texture(kSize, kSize, Pixels(static_cast<size_t>(kSize * kSize * 4), 128)); // not black
        glGenFramebuffers(1, &target.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    ~Output()
    {
        glDeleteFramebuffers(1, &target.fbo);
        glDeleteTextures(1, &target.texture);
    }

    auto Draw(Branch& branch, GLuint video, const std::vector<VideoStep>& steps) -> Pixels
    {
        branch.chain.DrawPicture(video, steps, branch.programs, branch.buffers, {}, branch.picture, branch.scratch, target.fbo, kSize, kSize);
        return Branch::Read(target.fbo);
    }

    engine::RenderTarget target;
};

} // namespace

TEST_CASE("a video with no preset lands in the output with its effects, and what the gate hides is black", "[gl]")
{
    REQUIRE_GL();
    Branch branch;
    Output output;
    const std::vector<VideoStep> look = {{{{NodeType::Contrast, 1.4f, 0.f, false}, {NodeType::LumaGate, 0.4f, 0.f, false}}}};
    const Pixels picture = branch.Run(look); // straight alpha: the gate only sets alpha
    const Pixels out = output.Draw(branch, branch.video, look);
    int hidden = 0;
    int shown = 0;
    for (size_t pixel = 0; pixel < out.size() / 4; ++pixel)
    {
        INFO("pixel " << pixel);
        const float alpha = picture[pixel * 4 + 3] / 255.f;
        hidden += alpha == 0.f;
        shown += alpha == 1.f;
        for (size_t channel = 0; channel < 3; ++channel)
            CHECK(std::abs(out[pixel * 4 + channel] - picture[pixel * 4 + channel] * alpha) <= 1.5f);
        CHECK(out[pixel * 4 + 3] == 255);
    }
    CHECK(hidden > 0);
    CHECK(shown > 0);

    // No effects: the video exactly as it is.
    CHECK(output.Draw(branch, branch.video, {}) == Colours());
}

TEST_CASE("an ISF generator with no video draws its own picture into the output", "[gl]")
{
    REQUIRE_GL();
    Branch branch;
    Output output;
    std::ifstream file(std::string(GV_TEST_FIXTURES_DIR) + "/isf/gradient.fs");
    std::stringstream text;
    text << file.rdbuf();
    const auto gradient = Shader(text.str());
    branch.Compile(gradient);
    REQUIRE(branch.programs.at(gradient)->Ok());
    const Pixels out = output.Draw(branch, 0, {{{}, engine::IsfStep{"isf-1", "gradient.fs", gradient, {}}}});
    for (int y = 0; y < kSize; ++y)
    {
        for (int x = 0; x < kSize; ++x)
        {
            INFO("pixel " << x << ", " << y);
            const size_t at = static_cast<size_t>((y * kSize + x) * 4);
            CHECK(std::abs(out[at] - (x + 0.5f) / kSize * 255.f) <= 1.5f);     // x in red
            CHECK(std::abs(out[at + 1] - (y + 0.5f) / kSize * 255.f) <= 1.5f); // y in green
            CHECK(out[at + 2] == 0);
            CHECK(out[at + 3] == 255);
        }
    }
}

TEST_CASE("an ISF float moved by a steady signal renders as its slider set to the moved value", "[gl][node:isf-shader]")
{
    REQUIRE_GL();
    // Video -> invert -> viewport, with an LFO into Amount at depth 0.5: 0.2 + (0.8 - 0.5) * 0.5 = 0.35.
    const std::filesystem::path invert = std::filesystem::path(GV_TEST_FIXTURES_DIR) / "isf" / "invert.fs";
    model::Graph graph;
    model::AddNode(graph, NodeType::Viewport, 0.f, 0.f);
    const std::string video = model::AddNode(graph, NodeType::VideoSource, 0.f, 0.f);
    const std::string isf = model::AddNode(graph, NodeType::IsfShader, 0.f, 0.f);
    const std::string lfo = model::AddNode(graph, NodeType::Lfo, 0.f, 0.f);
    model::ParamsOf<model::IsfShaderParams>(*graph.Find(isf)) = {invert, {{"amount", {0.2f}}}};
    graph.Find(isf)->depths["amount"] = 0.5f;
    REQUIRE(model::Connect(graph, video, isf, 0));
    REQUIRE(model::Connect(graph, isf, "viewport", 0));
    REQUIRE(model::Connect(graph, lfo, isf, model::ParamInput(*graph.Find(isf), 0)));
    REQUIRE(model::ValidateGraph(graph).empty());

    const auto steps = engine::CompilePlan(graph).branches.at(0).steps;
    REQUIRE(steps.size() == 1);
    REQUIRE(steps[0].isf);
    Branch branch;
    branch.Compile(steps[0].isf->shader);
    const Pixels moved = branch.Run(engine::Modulated(steps, {{lfo, 0.8f}}));
    const float amount = model::Modulate(*graph.Find(isf), 0, 0.8f);
    CHECK(std::abs(amount - 0.35f) < 1e-6f);
    const Pixels set = branch.Run({IsfStep(steps[0].isf->shader, amount)});
    CHECK(moved == set);
    CHECK(moved != branch.Run(steps)); // the signal did move it
}
