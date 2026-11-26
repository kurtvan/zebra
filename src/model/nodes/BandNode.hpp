#pragma once

// The Band node's ranges, shared by its node (the preset buttons, the strip) and its tests.

#include "model/Graph.hpp"

#include <array>
#include <string>

namespace gv::model {

constexpr double kBandMinHz = 20.0;    //!< The lowest a Band reaches: the bottom of hearing.
constexpr double kBandMaxHz = 20000.0; //!< The highest.

/// A ready-made range: picking it sets the Band's low and high ends.
struct BandPreset
{
    const char* name;
    double low;
    double high;
};
constexpr std::array<BandPreset, 3> kBandPresets{{
    {"Bass", kBandMinHz, 250.0},
    {"Mid", 250.0, 4000.0},
    {"Treble", 4000.0, kBandMaxHz},
}};

/// The preset `params` is (its index in kBandPresets), or -1 for a range set by hand.
auto BandPresetOf(const BandParams& params) -> int;
/// "60-250 Hz": the range, for the node.
auto BandRangeText(const BandParams& params) -> std::string;

} // namespace gv::model
