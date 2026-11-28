#pragma once

// The math nodes' maths, shared by the node (its value and its note) and its tests. One node type, Math, does
// all six operations (MathParams::op), so they share their inputs, saving and safety rules.

#include "model/Graph.hpp"

#include <optional>

namespace gv::model {

/// "Sum": the node's name for `op`, its title on the canvas and in the Add menu.
auto MathOpName(MathOp op) -> const char*;
/// What an unwired input of `op` is to start with: 0 for Sum, Difference, Log and Absolute; 1 for Multiply
/// and Divide, so it changes nothing.
auto MathIdentity(MathOp op) -> float;
/// How many inputs a node of `op` has when `lastWired` is its last wired input (-1: none). Sum and Multiply
/// have one empty input after it (at least 2, at most kMaxMathInputs); Difference and Divide 2; the others 1.
auto MathInputCount(MathOp op, int lastWired) -> int;
/// A new node of `op`: its inputs at the identity.
auto MathDefaults(MathOp op) -> MathParams;

/// Log's curve: `x` (0..1) bent towards the top, so the top end is softer. `curve` 0 is a straight line, 1 the
/// strongest bend. Always 0..1.
auto LogCurve(float x, float curve) -> float;

/// A math node's answer this frame, or, with no value, why it has none: then it holds its last good one.
struct MathAnswer
{
    std::optional<float> value; //!< Always finite.
    const char* holding{nullptr}; //!< Plain words for the node, while there's no value.
};
/// The answer of `params`' operation on its values (a wired input's value is its wire's: ModulatedNode). Log
/// takes inputs above 1 as 1; an input below 0 has no answer. Dividing by 0, an input that isn't a finite
/// number or an answer too big for one have none either.
auto MathAnswerOf(const MathParams& params) -> MathAnswer;

} // namespace gv::model
