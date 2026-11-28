#pragma once

// The sizes of an ISF shader's passes: its WIDTH and HEIGHT expressions, such as "$WIDTH/2". Pure: no GL.

#include "isf/IsfHeader.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace gv::isf {

/// The value of a size expression: numbers, $names from `variables` (without the $), + - * /, unary minus and
/// parentheses. Nothing if it can't be worked out: a syntax error, an unknown $name, or division by zero.
auto EvaluateExpression(std::string_view expression, const std::map<std::string, double>& variables) -> std::optional<double>;

/// The variables a pass's size can use: $WIDTH and $HEIGHT (`width` x `height`, what the shader renders at)
/// and the shader's float, long and bool inputs at `values` (else their DEFAULT).
auto SizeVariables(const std::vector<IsfInput>& inputs, const std::map<std::string, std::vector<float>>& values, int width, int height)
    -> std::map<std::string, double>;

/// The size `pass` renders at, with `variables` from SizeVariables: its WIDTH and HEIGHT, rounded down and at
/// least 1, or $WIDTH and $HEIGHT where it has none or one can't be worked out.
auto PassSize(const IsfPass& pass, const std::map<std::string, double>& variables) -> std::pair<int, int>;

/// The first WIDTH or HEIGHT of `shader` that can't be worked out, as written, or "" if all can.
auto BadSizeExpression(const IsfShader& shader) -> std::string;

} // namespace gv::isf
