// The ISF header parser and the GLSL translator (src/isf/IsfHeader.hpp), without GL.

#include "isf/IsfHeader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>

using namespace gv::isf;

namespace {

auto Parse(std::string_view source) -> IsfShader
{
    auto result = ParseIsf(source);
    if (const auto* error = std::get_if<IsfError>(&result))
        FAIL("line " << error->line << ": " << error->message);
    return std::get<IsfShader>(result);
}

auto ParseError(std::string_view source) -> IsfError
{
    auto result = ParseIsf(source);
    REQUIRE(std::holds_alternative<IsfError>(result));
    return std::get<IsfError>(result);
}

auto Fixture(const std::string& name) -> std::string
{
    std::ifstream file(std::string(GV_TEST_FIXTURES_DIR) + "/isf/" + name);
    REQUIRE(file.good());
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

auto Contains(const std::string& text, const std::string& part) -> bool
{
    return text.find(part) != std::string::npos;
}

constexpr const char* kEveryType = R"(/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Every input type",
    "CREDIT": "tests",
    "CATEGORIES": ["Test", "Other"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" },
        { "NAME": "level", "LABEL": "Level", "TYPE": "float", "DEFAULT": 0.25, "MIN": -1, "MAX": 2.5 },
        { "NAME": "enabled", "TYPE": "bool", "DEFAULT": true },
        { "NAME": "shape", "TYPE": "long", "VALUES": [2, 4, 8], "LABELS": ["Two", "Four", "Eight"], "DEFAULT": 4 },
        { "NAME": "tint", "TYPE": "color", "DEFAULT": [0.1, 0.2, 0.3, 1.0] },
        { "NAME": "where", "TYPE": "point2D", "DEFAULT": [3, 4], "MIN": [0, 0], "MAX": [100, 50] },
        { "NAME": "hit", "TYPE": "event" },
        { "NAME": "wave", "TYPE": "audio", "MAX": 256 },
        { "NAME": "spectrum", "TYPE": "audioFFT" }
    ],
    "PASSES": [
        { "TARGET": "half", "PERSISTENT": true, "FLOAT": 1, "WIDTH": "$WIDTH/2", "HEIGHT": 32 },
        { }
    ]
}*/
void main()
{
    gl_FragColor = texture2D(inputImage, isf_FragNormCoord) * level;
}
)";

} // namespace

TEST_CASE("every ISF input type is read with its defaults and ranges")
{
    const auto shader = Parse(kEveryType);
    CHECK(shader.version == "2");
    CHECK(shader.description == "Every input type");
    CHECK(shader.credit == "tests");
    CHECK(shader.categories == std::vector<std::string>{"Test", "Other"});
    REQUIRE(shader.inputs.size() == 9);

    const auto& image = shader.inputs[0];
    CHECK(image.type == InputType::Image);
    CHECK(image.defaultValue.empty());

    const auto& level = shader.inputs[1];
    CHECK(level.type == InputType::Float);
    CHECK(level.label == "Level");
    CHECK(level.defaultValue == std::vector<float>{0.25f});
    CHECK(level.min == std::vector<float>{-1.f});
    CHECK(level.max == std::vector<float>{2.5f});

    CHECK(shader.inputs[2].type == InputType::Bool);
    CHECK(shader.inputs[2].defaultValue == std::vector<float>{1.f});

    const auto& shape = shader.inputs[3];
    CHECK(shape.type == InputType::Long);
    CHECK(shape.values == std::vector<int>{2, 4, 8});
    CHECK(shape.labels == std::vector<std::string>{"Two", "Four", "Eight"});
    CHECK(shape.defaultValue == std::vector<float>{4.f});

    CHECK(shader.inputs[4].type == InputType::Color);
    CHECK(shader.inputs[4].defaultValue == std::vector<float>{0.1f, 0.2f, 0.3f, 1.f});

    const auto& where = shader.inputs[5];
    CHECK(where.type == InputType::Point2D);
    CHECK(where.defaultValue == std::vector<float>{3.f, 4.f});
    CHECK(where.max == std::vector<float>{100.f, 50.f});

    CHECK(shader.inputs[6].type == InputType::Event);
    CHECK(shader.inputs[6].defaultValue.empty());
    CHECK(shader.inputs[7].type == InputType::Audio);
    CHECK(shader.inputs[7].max == std::vector<float>{256.f});
    CHECK(shader.inputs[8].type == InputType::AudioFft);
    CHECK(FindInput(shader, "tint") == &shader.inputs[4]);
    CHECK(FindInput(shader, "nope") == nullptr);
}

TEST_CASE("ISF passes are stored as written")
{
    const auto shader = Parse(kEveryType);
    REQUIRE(shader.passes.size() == 2);
    CHECK(shader.passes[0].target == "half");
    CHECK(shader.passes[0].persistent);
    CHECK(shader.passes[0].floatTarget);
    CHECK(shader.passes[0].width == "$WIDTH/2");
    CHECK(shader.passes[0].height == "32");
    CHECK(shader.passes[1].target.empty());
    CHECK_FALSE(shader.passes[1].persistent);
}

TEST_CASE("a long with only LABELS gets the values 0..n-1")
{
    const auto shader = Parse(R"(/*{ "INPUTS": [ { "NAME": "m", "TYPE": "long", "LABELS": ["A", "B", "C"] } ] }*/ void main() {})");
    CHECK(shader.inputs[0].values == std::vector<int>{0, 1, 2});
}

TEST_CASE("the shader kind follows the ISF naming convention")
{
    CHECK(Parse(kEveryType).kind == ShaderKind::Filter);
    CHECK(Parse(Fixture("gradient.fs")).kind == ShaderKind::Generator);
    CHECK(Parse(R"(/*{}*/ void main() {})").kind == ShaderKind::Generator);
    CHECK(Parse(R"(/*{ "INPUTS": [
        { "NAME": "startImage", "TYPE": "image" },
        { "NAME": "endImage", "TYPE": "image" },
        { "NAME": "progress", "TYPE": "float" } ] }*/ void main() {})")
              .kind == ShaderKind::Transition);
    // inputImage that isn't an image doesn't make a filter.
    CHECK(Parse(R"(/*{ "INPUTS": [ { "NAME": "inputImage", "TYPE": "float" } ] }*/ void main() {})").kind == ShaderKind::Generator);
}

TEST_CASE("the GLSL starts after the header, on the file's own line")
{
    const auto shader = Parse(kEveryType);
    CHECK(shader.glslLine == 21); // the rest of the "}*/" line
    CHECK(Contains(shader.glsl, "void main()"));
    CHECK_FALSE(Contains(shader.glsl, "ISFVSN"));
}

TEST_CASE("broken headers say which line and what's wrong")
{
    SECTION("no header")
    {
        const auto error = ParseError("\n\nvoid main() {}");
        CHECK(error.line == 3);
        CHECK(Contains(error.message, "no ISF header"));
    }
    SECTION("empty file")
    {
        CHECK(Contains(ParseError("").message, "no ISF header"));
    }
    SECTION("unclosed comment")
    {
        const auto error = ParseError("/*{ \"INPUTS\": [] }\nvoid main() {}");
        CHECK(error.line == 1);
        CHECK(Contains(error.message, "never closed"));
    }
    SECTION("bad JSON")
    {
        const auto error = ParseError("/*{\n  \"DESCRIPTION\": \"x\",\n  \"INPUTS\": [ { \"NAME\": \"a\" \"TYPE\": \"float\" } ]\n}*/\nvoid main() {}");
        CHECK(error.line == 3);
        CHECK(Contains(error.message, "isn't valid JSON"));
    }
    SECTION("not an object")
    {
        CHECK(Contains(ParseError("/*[1, 2]*/").message, "{ } object"));
    }
    SECTION("INPUTS not a list")
    {
        const auto error = ParseError("/*{\n\"INPUTS\": 3 }*/");
        CHECK(error.line == 2);
        CHECK(Contains(error.message, "INPUTS"));
    }
    SECTION("input without NAME")
    {
        CHECK(ParseError("/*{ \"INPUTS\": [ { \"TYPE\": \"float\" } ] }*/").message == "input 1 has no NAME");
    }
    SECTION("unknown TYPE, on the input's line")
    {
        const auto error = ParseError("/*{ \"INPUTS\": [\n { \"NAME\": \"a\", \"TYPE\": \"float\" },\n { \"NAME\": \"b\", \"TYPE\": \"vec3\" } ] }*/");
        CHECK(error.line == 3);
        CHECK(error.message == "input \"b\" has an unknown TYPE \"vec3\"");
    }
    SECTION("DEFAULT of the wrong shape")
    {
        CHECK(ParseError(R"(/*{ "INPUTS": [ { "NAME": "c", "TYPE": "color", "DEFAULT": [1, 0] } ] }*/)").message ==
              "input \"c\": DEFAULT should be an array of 4 numbers");
        CHECK(ParseError(R"(/*{ "INPUTS": [ { "NAME": "f", "TYPE": "float", "MAX": "big" } ] }*/)").message ==
              "input \"f\": MAX should be a number");
    }
    SECTION("two inputs with one name")
    {
        CHECK(Contains(ParseError(R"(/*{ "INPUTS": [ { "NAME": "a", "TYPE": "float" }, { "NAME": "a", "TYPE": "bool" } ] }*/)").message,
                       "two inputs are called \"a\""));
    }
}

TEST_CASE("the translation declares every uniform and speaks GLSL 3.30 core")
{
    const auto glsl = TranslateIsf(Parse(kEveryType));
    CHECK(glsl.rfind("#version 330 core\n", 0) == 0);
    for (const char* uniform : {"uniform sampler2D inputImage;", "uniform float level;", "uniform bool enabled;", "uniform int shape;",
                                "uniform vec4 tint;", "uniform vec2 where;", "uniform bool hit;", "uniform sampler2D wave;",
                                "uniform sampler2D spectrum;", "uniform float TIME;", "uniform float TIMEDELTA;", "uniform int FRAMEINDEX;",
                                "uniform vec2 RENDERSIZE;", "uniform int PASSINDEX;", "uniform vec4 DATE;"})
    {
        INFO(uniform);
        CHECK(Contains(glsl, uniform));
    }
    CHECK(Contains(glsl, "in vec2 isf_FragNormCoord;"));
    CHECK(Contains(glsl, "out vec4 isf_FragColor;"));
    for (const char* function : {"IMG_NORM_PIXEL(", "IMG_PIXEL(", "IMG_THIS_PIXEL(", "IMG_THIS_NORM_PIXEL(", "IMG_SIZE("})
        CHECK(Contains(glsl, std::string("vec") + (std::string(function) == "IMG_SIZE(" ? "2 " : "4 ") + function));
    CHECK_FALSE(Contains(glsl, "gl_FragColor"));
    CHECK_FALSE(Contains(glsl, "texture2D"));
    CHECK(Contains(glsl, "isf_FragColor = texture(inputImage, isf_FragNormCoord) * level;"));
    CHECK(Contains(glsl, "#line 21\n")); // the next line is 21, where the GLSL starts
}

TEST_CASE("the translation rewrites only whole words of the old GLSL")
{
    const auto glsl = TranslateIsf(Parse(R"(/*{}*/
varying vec2 uv;
uniform sampler2D my_texture2D;
void main() { vec4 a = texture2DLod(my_texture2D, vv_FragNormCoord, 0.0); gl_FragColor = a + texture2D(my_texture2D, uv); }
)"));
    CHECK(Contains(glsl, "in vec2 uv;"));
    CHECK(Contains(glsl, "textureLod(my_texture2D, isf_FragNormCoord, 0.0)"));
    CHECK(Contains(glsl, "isf_FragColor = a + texture(my_texture2D, uv);"));
}

TEST_CASE("the fixture shaders parse")
{
    for (const char* name : {"passthrough.fs", "invert.fs", "box_blur.fs", "gradient.fs"})
    {
        INFO(name);
        const auto shader = Parse(Fixture(name));
        CHECK_FALSE(shader.description.empty());
        CHECK_FALSE(Contains(TranslateIsf(shader), "gl_FragColor"));
    }
    CHECK(Parse(Fixture("invert.fs")).kind == ShaderKind::Filter);
}
