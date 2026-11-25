#pragma once

// The LFO's wave, shared by its output (NodeDef::signal) and the plot on its node, so the two never disagree.

#include "model/nodes/NodeDef.hpp"

#include <vector>

namespace gv::model {

/// How far into its cycle (0..1, before the phase) the LFO is: by the clock's beats, or its fallback period.
auto LfoPosition(const LfoParams& params, const SignalInputs& inputs) -> double;
/// The LFO's value at `position` (0..1) into its cycle, the phase applied: its output there.
auto LfoValueAt(const LfoParams& params, double position) -> float;
/// One cycle of the wave, `count` values at positions i / count (LfoValueAt): what the node plots.
auto LfoCurve(const LfoParams& params, int count) -> std::vector<float>;

} // namespace gv::model
