// The sizes of an ISF shader's passes (src/isf/IsfSize.hpp): the WIDTH and HEIGHT expression evaluator.

#include "isf/IsfSize.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace gv::isf;

namespace {

const std::map<std::string, double> kCanvas = {{"WIDTH", 1920.0}, {"HEIGHT", 1080.0}, {"blur", 4.0}};

auto Value(std::string_view expression) -> std::optional<double>
{
    return EvaluateExpression(expression, kCanvas);
}

} // namespace

TEST_CASE("size expressions: numbers, $WIDTH and $HEIGHT, + - * / and parentheses, in the usual order")
{
    CHECK(Value("32") == 32.0);
    CHECK(Value(" 2.5 ") == 2.5);
    CHECK(Value("$WIDTH") == 1920.0);
    CHECK(Value("$WIDTH/2") == 960.0);
    CHECK(Value("$HEIGHT / 4 + 1") == 271.0);
    CHECK(Value("1 + $HEIGHT / 4") == 271.0);
    CHECK(Value("($WIDTH + $HEIGHT) * 0.5") == 1500.0);
    CHECK(Value("$WIDTH - 20 - 10") == 1890.0); // left to right
    CHECK(Value("$WIDTH / 2 / 2") == 480.0);
    CHECK(Value("-4 + 10") == 6.0);
    CHECK(Value("2 * -(3 - 1)") == -4.0);
    CHECK(Value("((($HEIGHT)))") == 1080.0);
    CHECK(Value("$WIDTH / $blur") == 480.0); // a float input by name
}

TEST_CASE("size expressions that can't be worked out give nothing")
{
    for (const char* bad : {"", "$", "$DEPTH", "WIDTH", "2 +", "(2", "2)", "4 / 0", "$WIDTH $HEIGHT", "1.2.3", "max(1, 2)", "2 ^ 3"})
    {
        INFO(bad);
        CHECK_FALSE(Value(bad).has_value());
    }
}

TEST_CASE("a pass's size: its expressions rounded down and at least 1, else the whole render size")
{
    IsfShader shader;
    shader.inputs.push_back({"scale", InputType::Float, {}, {3.f}});
    const auto variables = SizeVariables(shader.inputs, {}, 640, 360);
    CHECK(variables.at("scale") == 3.0);
    CHECK(PassSize({}, variables) == std::pair{640, 360});
    CHECK(PassSize({"t", false, false, "$WIDTH/3", "$HEIGHT/7"}, variables) == std::pair{213, 51});
    CHECK(PassSize({"t", false, false, "$WIDTH/$scale", "0"}, variables) == std::pair{213, 1});
    CHECK(PassSize({"t", false, false, "nonsense", "-5"}, variables) == std::pair{640, 1});
    CHECK(SizeVariables(shader.inputs, {{"scale", {2.f}}}, 640, 360).at("scale") == 2.0);

    shader.passes = {{"t", false, false, "$WIDTH/2", "$HEIGHT"}};
    CHECK(BadSizeExpression(shader).empty());
    shader.passes.push_back({"u", false, false, "", "$DEPTH * 2"});
    CHECK(BadSizeExpression(shader) == "$DEPTH * 2");
}
