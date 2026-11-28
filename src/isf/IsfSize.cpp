#include "isf/IsfSize.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace gv::isf {

namespace {

using Value = std::optional<double>;

/// Recursive descent over one expression: sum := product (+|- product)*, product := unary (*|/ unary)*,
/// unary := -unary | +unary | atom, atom := number | $name | ( sum ).
struct Reader
{
    std::string_view text;
    const std::map<std::string, double>& variables;
    size_t at{0};

    /// Skips spaces; true at the end.
    auto End() -> bool
    {
        while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])) != 0)
            ++at;
        return at == text.size();
    }

    auto Take(char c) -> bool
    {
        const bool taken = !End() && text[at] == c;
        at += taken ? 1 : 0;
        return taken;
    }

    auto Taken(auto isPart) -> std::string
    {
        const size_t start = at;
        while (at < text.size() && isPart(static_cast<unsigned char>(text[at])))
            ++at;
        return std::string(text.substr(start, at - start));
    }

    auto Sum() -> Value
    {
        Value value = Product();
        while (value)
        {
            const char op = Take('+') ? '+' : Take('-') ? '-' : 0;
            if (op == 0)
                break;
            const Value right = Product();
            value = right ? Value(op == '+' ? *value + *right : *value - *right) : std::nullopt;
        }
        return value;
    }

    auto Product() -> Value
    {
        Value value = Unary();
        while (value)
        {
            const char op = Take('*') ? '*' : Take('/') ? '/' : 0;
            if (op == 0)
                break;
            const Value right = Unary();
            value = !right || (op == '/' && *right == 0.0) ? std::nullopt : Value(op == '*' ? *value * *right : *value / *right);
        }
        return value;
    }

    auto Unary() -> Value
    {
        if (Take('-'))
        {
            const Value value = Unary();
            return value ? Value(-*value) : std::nullopt;
        }
        if (Take('+'))
            return Unary();
        if (Take('('))
        {
            const Value value = Sum();
            return value && Take(')') ? value : std::nullopt;
        }
        if (Take('$'))
        {
            const auto it = variables.find(Taken([](unsigned char c) { return std::isalnum(c) != 0 || c == '_'; }));
            return it == variables.end() ? std::nullopt : Value(it->second);
        }
        const std::string number = Taken([](unsigned char c) { return std::isdigit(c) != 0 || c == '.'; });
        char* end = nullptr;
        const double value = std::strtod(number.c_str(), &end);
        return !number.empty() && end == number.c_str() + number.size() ? Value(value) : std::nullopt;
    }
};

auto Dimension(const std::string& expression, const std::map<std::string, double>& variables, const char* whole) -> int
{
    const Value value = expression.empty() ? std::nullopt : EvaluateExpression(expression, variables);
    const double size = value && std::isfinite(*value) ? *value : variables.at(whole);
    return static_cast<int>(std::clamp(std::floor(size), 1.0, 16384.0));
}

} // namespace

auto EvaluateExpression(std::string_view expression, const std::map<std::string, double>& variables) -> std::optional<double>
{
    Reader reader{expression, variables};
    const Value value = reader.Sum();
    return value && reader.End() ? value : std::nullopt;
}

auto SizeVariables(const std::vector<IsfInput>& inputs, const std::map<std::string, std::vector<float>>& values, int width, int height)
    -> std::map<std::string, double>
{
    std::map<std::string, double> variables;
    for (const auto& input : inputs)
    {
        if (input.type != InputType::Float && input.type != InputType::Long && input.type != InputType::Bool)
            continue;
        const auto given = values.find(input.name);
        const auto& value = given != values.end() && !given->second.empty() ? given->second : input.defaultValue;
        variables[input.name] = value.empty() ? 0.0 : value[0];
    }
    variables["WIDTH"] = width;
    variables["HEIGHT"] = height;
    return variables;
}

auto PassSize(const IsfPass& pass, const std::map<std::string, double>& variables) -> std::pair<int, int>
{
    return {Dimension(pass.width, variables, "WIDTH"), Dimension(pass.height, variables, "HEIGHT")};
}

auto BadSizeExpression(const IsfShader& shader) -> std::string
{
    const auto variables = SizeVariables(shader.inputs, {}, 1920, 1080);
    for (const auto& pass : shader.passes)
    {
        for (const auto* expression : {&pass.width, &pass.height})
        {
            if (!expression->empty() && !EvaluateExpression(*expression, variables))
                return *expression;
        }
    }
    return {};
}

} // namespace gv::isf
