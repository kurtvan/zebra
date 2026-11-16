#pragma once

#include "model/Graph.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace gv::engine {

/// One effect of a video branch, in the order the video passes through it.
struct EffectPass
{
    enum class Type
    {
        Contrast,   //!< a: contrast
        Saturation, //!< a: saturation
        LumaGate,   //!< a: threshold, b: softness, flag: invert
        Mix,        //!< a: opacity
    };
    Type type{Type::Contrast};
    float a{0.0f};
    float b{0.0f};
    bool flag{false};

    auto operator==(const EffectPass&) const -> bool = default;
};

/// The video's path from the active source into one preset input.
struct VideoBranch
{
    std::vector<EffectPass> passes;
    model::PresetLayer layer{model::PresetLayer::Feedback};
    std::string textureName; //!< Texture layer only.

    auto operator==(const VideoBranch&) const -> bool = default;
};

/// What a scene draws for one switcher selection: a flat description compiled from its graph.
struct RenderPlan
{
    std::string presetNode;            //!< Empty: nothing connected (black).
    std::filesystem::path presetPath;
    std::string sourceNode;            //!< Empty: no video.
    model::VideoSource source;
    std::vector<VideoBranch> branches; //!< One per preset input the video reaches, in input order.

    auto operator==(const RenderPlan&) const -> bool = default;
};

/// A plan's video as today's lanes take it: one feedback branch (Contrast, Saturation, Luma gate, Mix) and an
/// optional on-top branch with the same look. Replaced by the lane running plans directly in N2.
struct VideoSettings
{
    bool enabled{false};
    float mix{1.0f};
    float lumaMin{0.0f};
    float contrast{1.0f};
    float saturation{1.0f};
    bool overlay{false};  //!< Also draw the video over the rendered frame, masked by lumaMin.
    float softness{0.0f}; //!< Overlay mask edge, 0 = hard.
    bool lumaInvert{false}; //!< The gate keeps pixels darker than lumaMin instead (blend and overlay).

    auto operator==(const VideoSettings&) const -> bool = default;
};

/// The VideoSettings that draw `plan` (exactly, for plans of classic-shaped graphs).
auto VideoSettingsFromPlan(const RenderPlan& plan) -> VideoSettings;

/// Switcher node id -> the input that passes. A switcher missing here passes its first wired input.
using SwitcherSelection = std::map<std::string, int>;

/// Walks back from the viewport, following only each switcher's selected input. `graph` must be valid
/// (model::ValidateGraph); unfinished parts compile to nothing.
auto CompilePlan(const model::Graph& graph, const SwitcherSelection& selection = {}) -> RenderPlan;

/// The selection that passes the `item`-th wired input of the switcher feeding the viewport (classic
/// playlist entry `item`). Empty when there's no such switcher.
auto SelectionForItem(const model::Graph& graph, int item) -> SwitcherSelection;

} // namespace gv::engine
