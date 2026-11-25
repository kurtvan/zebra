#pragma once

#include "model/Graph.hpp"
#include "model/nodes/NodeDef.hpp"
#include "isf/IsfHeader.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gv::engine {

/// One effect of a video branch, in the order the video passes through it.
using model::EffectPass;

/// An ISF shader node of a video branch, drawn as a pass of its own.
struct IsfStep
{
    std::string node;                             //!< The node's id.
    std::filesystem::path path;                   //!< Its file: compile errors are reported by path.
    std::shared_ptr<const isf::IsfShader> shader; //!< The parsed file (model::IsfFileAt): a new one when it changes.
    std::map<std::string, std::vector<float>> values;

    auto operator==(const IsfStep&) const -> bool = default;
};

/// One pass of a video branch: neighbouring built-in effects run together, or one ISF shader.
struct VideoStep
{
    std::vector<EffectPass> effects{}; //!< Without `isf`: in order, in one draw (VideoChain::Run).
    std::optional<IsfStep> isf{};

    auto operator==(const VideoStep&) const -> bool = default;
};

/// The video's path from the active source into one preset input.
struct VideoBranch
{
    std::vector<VideoStep> steps; //!< In the order the video passes through them; none draws the video as it is.
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

/// What a lane needs to draw a plan's video: its branches. The preset and source are chosen elsewhere.
struct LanePlan
{
    std::vector<VideoBranch> branches;

    auto operator==(const LanePlan&) const -> bool = default;
};

/// Switcher node id -> the input that passes. A switcher missing here passes its first wired input.
using SwitcherSelection = std::map<std::string, int>;

/// Walks back from the viewport, following only each switcher's selected input. `graph` must be valid
/// (model::ValidateGraph); unfinished parts compile to nothing.
auto CompilePlan(const model::Graph& graph, const SwitcherSelection& selection = {}) -> RenderPlan;

/// The selection that passes the `item`-th wired input of the switcher feeding the viewport
/// (Scene::presetEntries entry `item`). Empty when there's no such switcher.
auto SelectionForItem(const model::Graph& graph, int item) -> SwitcherSelection;

} // namespace gv::engine
