// The ISF loader end to end: the fixture shaders in tests/fixtures/isf, parsed, translated, compiled and drawn
// into a 16 x 16 texture, then read back pixel by pixel.

#include "gl/GlTestContext.hpp"
#include "isf/IsfProgram.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace gv::isf;

namespace {

constexpr int kSize = 16; // the image and the output: the same size, so each pixel samples exactly one texel
constexpr int kTolerance = 1; // RGBA8 rounding

using Pixels = std::vector<std::uint8_t>;

auto Load(const std::string& name) -> IsfShader
{
    std::ifstream file(std::string(GV_TEST_FIXTURES_DIR) + "/isf/" + name);
    REQUIRE(file.good());
    std::stringstream text;
    text << file.rdbuf();
    auto result = ParseIsf(text.str());
    REQUIRE(std::holds_alternative<IsfShader>(result));
    return std::get<IsfShader>(result);
}

/// Colours that differ in every pixel and channel.
auto Colours() -> Pixels
{
    Pixels pixels;
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
            for (const int value : {x * 17, y * 17, (x * 7 + y * 13) % 256, 255 - x * 5})
                pixels.push_back(static_cast<std::uint8_t>(value));
    return pixels;
}

/// Black on the left half, white on the right.
auto Halves() -> Pixels
{
    Pixels pixels;
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
            for (int c = 0; c < 4; ++c)
                pixels.push_back(c == 3 || x >= kSize / 2 ? 255 : 0);
    return pixels;
}

/// Draws `shader` with `input` (if any) as inputImage, and reads the result back as RGBA8.
auto Render(const IsfShader& shader, const Pixels& input, const std::map<std::string, std::vector<float>>& values = {}) -> Pixels
{
    const GLuint image = input.empty() ? 0 : gv::test::Texture(kSize, kSize, input);
    const GLuint output = gv::test::Texture(kSize, kSize, {});
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0);
    {
        IsfProgram program(shader);
        INFO(program.Error());
        REQUIRE(program.Ok());
        std::map<std::string, GLuint> images;
        if (image != 0)
            images["inputImage"] = image;
        program.Render(images, values, IsfClock{}, fbo, kSize, kSize);
    }
    Pixels bytes(static_cast<size_t>(kSize * kSize * 4));
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &output);
    if (image != 0)
        glDeleteTextures(1, &image);
    return bytes;
}

auto Near(int got, int want) -> bool
{
    return std::abs(got - want) <= kTolerance;
}

} // namespace

TEST_CASE("ISF passthrough reproduces the input pixels")
{
    REQUIRE_GL();
    const auto in = Colours();
    CHECK(Render(Load("passthrough.fs"), in) == in);
}

TEST_CASE("ISF invert: amount 0 keeps the image, amount 1 inverts it")
{
    REQUIRE_GL();
    const auto shader = Load("invert.fs");
    const auto in = Colours();
    CHECK(Render(shader, in) == in); // DEFAULT 0.0
    const auto out = Render(shader, in, {{"amount", {1.f}}});
    for (size_t i = 0; i < in.size(); ++i)
    {
        const int want = i % 4 == 3 ? in[i] : 255 - in[i];
        INFO("byte " << i);
        CHECK(Near(out[i], want));
    }
}

TEST_CASE("ISF box blur changes the image only at its edge")
{
    REQUIRE_GL();
    const auto in = Halves();
    const auto out = Render(Load("box_blur.fs"), in);
    for (int y = 0; y < kSize; ++y)
    {
        for (int x = 0; x < kSize; ++x)
        {
            const size_t i = static_cast<size_t>((y * kSize + x) * 4);
            INFO("pixel " << x << ", " << y << ": got " << int(out[i]));
            if (x == kSize / 2 - 1) // one white column of three
                CHECK(Near(out[i], 85));
            else if (x == kSize / 2) // two white columns of three
                CHECK(Near(out[i], 170));
            else
                CHECK(out[i] == in[i]); // the image's border clamps, so it stays sharp
            CHECK(out[i + 3] == 255);
        }
    }
}

TEST_CASE("ISF generator: the gradient and every kind of value input reach the shader")
{
    REQUIRE_GL();
    const auto shader = Load("gradient.fs");
    const auto pixel = [](const Pixels& p, int x, int y) { return &p[static_cast<size_t>((y * kSize + x) * 4)]; };
    const auto coord = [](int i) { return static_cast<int>(std::lround((i + 0.5) / kSize * 255.0)); };

    const auto plain = Render(shader, {});
    for (int y = 0; y < kSize; ++y)
    {
        for (int x = 0; x < kSize; ++x)
        {
            INFO("pixel " << x << ", " << y);
            const auto* p = pixel(plain, x, y);
            CHECK(Near(p[0], coord(x))); // bottom-up: y = 0 is the first row read back
            CHECK(Near(p[1], coord(y)));
            CHECK(p[2] == 0);
            CHECK(p[3] == 255);
        }
    }

    const auto tinted = Render(shader, {}, {{"tint", {1.f, 0.f, 1.f, 1.f}}, {"mode", {1.f}}});
    CHECK(Near(pixel(tinted, 3, 9)[0], 128));
    CHECK(pixel(tinted, 3, 9)[1] == 0);

    const auto spot = Render(shader, {}, {{"spot", {1.f}}, {"center", {5.5f, 2.5f}}});
    CHECK(pixel(spot, 5, 2)[2] == 255);
    CHECK(pixel(spot, 6, 2)[2] == 0);

    const auto flash = Render(shader, {}, {{"flash", {1.f}}});
    CHECK(pixel(flash, 0, 0)[0] == 255);
    CHECK(pixel(flash, 15, 15)[1] == 255);
}

TEST_CASE("a broken ISF shader returns the compiler's message on its own line")
{
    REQUIRE_GL();
    auto result = ParseIsf("/*{\n  \"INPUTS\": []\n}*/\n\nvoid main()\n{\n    gl_FragColor = vec4(undefinedThing);\n}\n");
    REQUIRE(std::holds_alternative<IsfShader>(result));
    IsfProgram program(std::get<IsfShader>(result));
    CHECK_FALSE(program.Ok());
    INFO(program.Error());
    CHECK(program.Error().find("undefinedThing") != std::string::npos);
    CHECK(program.Error().find(":7:") != std::string::npos); // line 7 of the file

    // Rendering a failed program draws nothing and doesn't crash.
    program.Render({}, {}, IsfClock{}, 0, kSize, kSize);
}
