// Each effect node's shader (NodeDef::shader, run by engine/VideoChain), pixel by pixel against the formula it
// promises, written out again here on the CPU. One effect per test case, over 256 colours.

#include "engine/RenderPlan.hpp"
#include "engine/VideoChain.hpp"
#include "gl/GlTestContext.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

using gv::engine::VideoChain;
using gv::model::EffectPass;
using gv::model::NodeType;

namespace {

constexpr int kSize = 16; // the video and the branch: the same size, so each pixel samples exactly one texel

struct Rgba
{
    float r, g, b, a;
};

/// 256 colours: every corner of the cube, greys, and steps in between.
auto Colours() -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> pixels;
    for (int y = 0; y < kSize; ++y)
    {
        for (int x = 0; x < kSize; ++x)
        {
            pixels.push_back(static_cast<std::uint8_t>(x * 17));
            pixels.push_back(static_cast<std::uint8_t>(y * 17));
            pixels.push_back(static_cast<std::uint8_t>(((x + y) * 37 + x * y * 11) % 256));
            pixels.push_back(200); // the chain ignores the video's alpha
        }
    }
    return pixels;
}

/// The chain with `passes` over Colours(), as RGBA in 0..1.
auto Run(const std::vector<EffectPass>& passes) -> std::vector<Rgba>
{
    const GLuint video = gv::test::Texture(kSize, kSize, Colours());
    const GLuint branch = gv::test::Texture(kSize, kSize, {});
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, branch, 0);
    {
        VideoChain chain;
        chain.Run(video, passes, fbo, kSize, kSize);
    }
    std::vector<std::uint8_t> bytes(static_cast<size_t>(kSize * kSize * 4));
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    const GLuint textures[] = {video, branch};
    glDeleteTextures(2, textures);

    std::vector<Rgba> out;
    for (size_t i = 0; i < bytes.size(); i += 4)
        out.push_back({bytes[i] / 255.f, bytes[i + 1] / 255.f, bytes[i + 2] / 255.f, bytes[i + 3] / 255.f});
    return out;
}

auto Inputs() -> std::vector<Rgba>
{
    const auto bytes = Colours();
    std::vector<Rgba> in;
    for (size_t i = 0; i < bytes.size(); i += 4)
        in.push_back({bytes[i] / 255.f, bytes[i + 1] / 255.f, bytes[i + 2] / 255.f, 1.f});
    return in;
}

auto Clamp01(float value) -> float
{
    return std::clamp(value, 0.f, 1.f);
}

auto Luma(const Rgba& c) -> float
{
    return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
}

constexpr float kTolerance = 1.01f / 255.f; // the RGBA8 branch's rounding

/// Checks the chain with `pass` against `expected` for every colour, skipping colours `skip` names.
void Compare(const EffectPass& pass, const std::function<Rgba(const Rgba&)>& expected, const std::function<bool(const Rgba&)>& skip = {})
{
    const auto in = Inputs();
    const auto out = Run({pass});
    REQUIRE(out.size() == in.size());
    int compared = 0;
    for (size_t i = 0; i < in.size(); ++i)
    {
        if (skip && skip(in[i]))
            continue;
        const Rgba want = expected(in[i]);
        const Rgba got = out[i];
        INFO("colour " << in[i].r << ", " << in[i].g << ", " << in[i].b << ": got " << got.r << ", " << got.g << ", " << got.b << ", "
                       << got.a << "; want " << want.r << ", " << want.g << ", " << want.b << ", " << want.a);
        CHECK(std::abs(got.r - want.r) <= kTolerance);
        CHECK(std::abs(got.g - want.g) <= kTolerance);
        CHECK(std::abs(got.b - want.b) <= kTolerance);
        CHECK(std::abs(got.a - want.a) <= kTolerance);
        ++compared;
    }
    CHECK(compared > 200);
}

} // namespace

TEST_CASE("contrast: (colour - 0.5) * contrast + 0.5, clamped; 1 leaves the video as it is", "[gl][node:contrast]")
{
    REQUIRE_GL();
    for (float contrast : {0.f, 0.5f, 1.f, 2.5f, 100.f})
    {
        INFO("contrast " << contrast);
        Compare({NodeType::Contrast, contrast, 0.f, false}, [&](const Rgba& c) {
            auto f = [&](float v) { return Clamp01((v - 0.5f) * contrast + 0.5f); };
            return Rgba{f(c.r), f(c.g), f(c.b), 1.f};
        });
    }
}

TEST_CASE("saturation: from the pixel's grey (0) through itself (1) to twice as colourful (2)", "[gl][node:saturation]")
{
    REQUIRE_GL();
    for (float saturation : {0.f, 0.4f, 1.f, 2.f})
    {
        INFO("saturation " << saturation);
        Compare({NodeType::Saturation, saturation, 0.f, false}, [&](const Rgba& c) {
            const float grey = Luma(c);
            auto f = [&](float v) { return Clamp01(grey + (v - grey) * saturation); };
            return Rgba{f(c.r), f(c.g), f(c.b), 1.f};
        });
    }
}

TEST_CASE("luma gate: cuts pixels darker than the threshold (brighter ones inverted), feathered by softness", "[gl][node:luma-gate]")
{
    REQUIRE_GL();
    struct Case
    {
        float threshold, softness;
        bool invert;
    };
    for (const Case& gate : {Case{0.f, 0.f, false}, Case{0.4f, 0.f, false}, Case{0.4f, 0.f, true}, Case{0.5f, 0.3f, false}, Case{0.5f, 0.3f, true},
                             Case{1.f, 1.f, false}})
    {
        INFO("threshold " << gate.threshold << ", softness " << gate.softness << (gate.invert ? ", inverted" : ""));
        const float edge = gate.softness * 0.5f;
        auto mask = [&](float luma) {
            if (edge <= 0.f)
                return luma >= gate.threshold ? 1.f : 0.f;
            const float t = Clamp01((luma - (gate.threshold - edge)) / (2.f * edge));
            return t * t * (3.f - 2.f * t);
        };
        Compare(
            {NodeType::LumaGate, gate.threshold, gate.softness, gate.invert},
            [&](const Rgba& c) {
                const float m = mask(Luma(c));
                return Rgba{c.r, c.g, c.b, gate.invert ? 1.f - m : m}; // colour unchanged: only alpha is cut
            },
            // A hard edge is a step: colours right on it may land either side in float.
            [&](const Rgba& c) { return edge <= 0.f && gate.threshold > 0.f && std::abs(Luma(c) - gate.threshold) < 0.002f; });
    }
}

TEST_CASE("mix: the video's alpha is the opacity, its colour unchanged", "[gl][node:opacity]")
{
    REQUIRE_GL();
    for (float opacity : {0.f, 0.25f, 1.f})
    {
        INFO("opacity " << opacity);
        Compare({NodeType::Opacity, opacity, 0.f, false}, [&](const Rgba& c) { return Rgba{c.r, c.g, c.b, opacity}; });
    }
}

TEST_CASE("an LFO driving opacity: a square at its high and low gives the pixels of opacity 1 and 0", "[gl][node:opacity][node:lfo]")
{
    REQUIRE_GL();
    using namespace gv::model;
    Graph graph;
    const auto preset = AddNode(graph, NodeType::MilkdropPreset, 0, 0);
    const auto video = AddNode(graph, NodeType::VideoSource, 0, 0);
    const auto opacity = AddNode(graph, NodeType::Opacity, 0, 0);
    const auto lfo = AddNode(graph, NodeType::Lfo, 0, 0);
    ParamsOf<OpacityParams>(*graph.Find(opacity)).opacity = 0.5f;
    ParamsOf<LfoParams>(*graph.Find(lfo)).shape = LfoShape::Square;
    Connect(graph, preset, AddNode(graph, NodeType::Viewport, 0, 0), 0);
    Connect(graph, video, opacity, 0);
    Connect(graph, opacity, preset, 0);
    Connect(graph, lfo, opacity, ParamInput(*graph.Find(opacity), 0));
    REQUIRE(ValidateGraph(graph).empty());
    const auto steps = gv::engine::CompilePlan(graph).branches.at(0).steps;
    for (const auto& [phase, still] : {std::pair{0.25f, 1.f}, std::pair{0.75f, 0.f}})
    {
        INFO("phase " << phase);
        ParamsOf<LfoParams>(*graph.Find(lfo)).phase = phase;
        const auto modulated = gv::engine::Modulated(steps, EvaluateSignals(graph, {0.0, 0.0}));
        const auto got = Run(modulated.at(0).effects);
        const auto want = Run({{NodeType::Opacity, still, 0.f, false}});
        REQUIRE(got.size() == want.size());
        for (size_t i = 0; i < got.size(); ++i)
        {
            CHECK(got[i].r == want[i].r);
            CHECK(got[i].a == want[i].a);
        }
    }
}

TEST_CASE("every effect param moved by a steady signal renders as its slider set to the moved value", "[gl]")
{
    REQUIRE_GL();
    using namespace gv::model;
    for (const auto& def : NodeDefs())
    {
        for (int param = 0; def.effect && param < static_cast<int>(def.params.size()); ++param)
        {
            const ParamDef& p = def.params[static_cast<size_t>(param)];
            INFO(def.displayName << ": " << p.name);
            Graph graph;
            const auto preset = AddNode(graph, NodeType::MilkdropPreset, 0, 0);
            const auto video = AddNode(graph, NodeType::VideoSource, 0, 0);
            const auto effect = AddNode(graph, def.type, 0, 0);
            const auto lfo = AddNode(graph, NodeType::Lfo, 0, 0);
            p.set(*graph.Find(effect), p, p.low + (p.high - p.low) * 0.3f);
            Connect(graph, preset, AddNode(graph, NodeType::Viewport, 0, 0), 0);
            Connect(graph, video, effect, 0);
            Connect(graph, effect, preset, 0);
            Connect(graph, lfo, effect, ParamInput(*graph.Find(effect), param));
            REQUIRE(ValidateGraph(graph).empty());

            // A signal held at 0.7 moves the slider up a fifth of its range (depth 1).
            const auto steps = gv::engine::CompilePlan(graph).branches.at(0).steps;
            const auto got = Run(gv::engine::Modulated(steps, {{lfo, 0.7f}}).at(0).effects);
            Node slid = *graph.Find(effect);
            p.set(slid, p, p.low + (p.high - p.low) * 0.5f);
            const auto want = Run({def.pass(slid)});
            REQUIRE(got.size() == want.size());
            int differ = 0;
            for (size_t i = 0; i < got.size(); ++i)
                differ += std::abs(got[i].r - want[i].r) + std::abs(got[i].g - want[i].g) + std::abs(got[i].a - want[i].a) > kTolerance ? 1 : 0;
            CHECK(differ == 0);
        }
    }
}

TEST_CASE("every effect node has a shader, and the chain compiles with all of them", "[gl]")
{
    REQUIRE_GL();
    std::vector<EffectPass> all;
    for (const auto& def : gv::model::NodeDefs())
    {
        if (!def.effect)
            continue;
        INFO(def.displayName);
        CHECK(def.shader != nullptr);
        CHECK(def.pass != nullptr);
        all.push_back({def.type, 1.f, 0.f, false});
    }
    // Contrast 1, saturation 1, gate at 1 (cuts all but white), mix 1: compiles, runs and gives the gate's alpha.
    const auto out = Run(all);
    CHECK(out.front().a == 0.f);
}
