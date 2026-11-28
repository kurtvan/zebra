#pragma once

#include "model/Graph.hpp"
#include "model/Performance.hpp"
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
    int divisor{1}; //!< The node's Resolution: it renders at the canvas size divided by this (1, 2 or 4).

    auto operator==(const IsfStep&) const -> bool = default;
};

/// An effect whose settings Number wires move: what the lane needs to redo its pass each frame (Modulated).
struct PassModulation
{
    size_t effect{0}; //!< Its index in VideoStep::effects.
    model::Node node; //!< The effect node's id, type, settings and depths (not its place on the canvas).
    std::vector<std::pair<int, std::string>> signals; //!< Param index (NodeDef::params) -> the node it's wired from.

    auto operator==(const PassModulation&) const -> bool = default;
};

/// One pass of a video branch: neighbouring built-in effects run together, or one ISF shader.
struct VideoStep
{
    std::vector<EffectPass> effects{}; //!< Without `isf`: in order, in one draw (VideoChain::Run).
    std::optional<IsfStep> isf{};
    std::vector<PassModulation> modulations{}; //!< The effects Number wires move, redone each frame.

    auto operator==(const VideoStep&) const -> bool = default;
};

/// The video's path from the active source into one preset input, or straight onto the viewport.
struct VideoBranch
{
    std::vector<VideoStep> steps; //!< In the order the video passes through them; none draws the video as it is.
                                  //!< The first may be a generator: an ISF shader with no image input.
    model::PresetLayer layer{model::PresetLayer::Feedback};
    std::string textureName; //!< Texture layer only.

    auto operator==(const VideoBranch&) const -> bool = default;
};

/// What a scene draws for one switcher selection: a flat description compiled from its graph.
struct RenderPlan
{
    std::string presetNode;            //!< Empty: no preset; the viewport shows the picture branch, if any, else black.
    std::filesystem::path presetPath;
    std::string sourceNode;            //!< Empty: no video.
    model::VideoSource source;
    std::vector<VideoBranch> branches; //!< One per preset input the video reaches, in input order. Without a
                                       //!< preset: at most one, the picture on the viewport (its layer unused).

    /// True when the viewport shows a picture with no preset: a video, its effects, or a generator.
    auto ShowsPicture() const -> bool
    {
        return presetNode.empty() && !branches.empty();
    }

    auto operator==(const RenderPlan&) const -> bool = default;
};

/// What feeds one wired picture input of a preset step.
enum class Feed
{
    Nothing, //!< An unfinished chain: no picture.
    Video,   //!< A video branch (StepInput::video).
    Step,    //!< Another preset's finished frame (StepInput::step).
};

/// One wired picture input of a preset step and what feeds it.
struct StepInput
{
    model::PresetLayer layer{model::PresetLayer::Feedback};
    std::string textureName; //!< Texture layer only.
    Feed feed{Feed::Nothing};
    std::vector<VideoStep> video{}; //!< Feed::Video: the video's path into it, with its modulations (VideoBranch::steps).
    int step{-1};                   //!< Feed::Step: the index in RenderProgram::steps of the preset feeding it, always earlier.

    auto operator==(const StepInput&) const -> bool = default;
};

/// One preset of a render program, and what feeds each of its inputs.
struct PresetStep
{
    std::string presetNode;
    std::filesystem::path presetPath;
    std::vector<StepInput> inputs; //!< Every wired picture input, in input order (texture inputs by name).
    std::string audio;             //!< The node wired into its Audio input; empty: it hears nothing.

    auto operator==(const PresetStep&) const -> bool = default;
};

/// What a scene draws for one switcher selection: its presets as ordered steps, each one's inputs naming what
/// feeds them. Today's scenes have at most one step.
struct RenderProgram
{
    std::vector<PresetStep> steps; //!< Upstream first, ties by node id; the last is the preset on the viewport.
    std::optional<VideoBranch> picture; //!< No preset: the picture on the viewport, if any (its layer unused).
    std::string sourceNode;             //!< Empty: no video.
    model::VideoSource source;
    std::vector<model::GraphError> errors; //!< Not empty: the program was refused (a loop) and has no steps.

    /// The preset on the viewport, or null.
    auto Top() const -> const PresetStep*
    {
        return steps.empty() ? nullptr : &steps.back();
    }
    /// True when the viewport shows a picture with no preset (RenderPlan::ShowsPicture).
    auto ShowsPicture() const -> bool
    {
        return steps.empty() && picture.has_value();
    }

    auto operator==(const RenderProgram&) const -> bool = default;
};

/// What a lane needs to draw a plan's video: its branches. The preset and source are chosen elsewhere.
struct LanePlan
{
    std::vector<VideoBranch> branches;
    bool picture{false}; //!< No preset: branches[0] is drawn straight into the frame (RenderPlan::ShowsPicture).
    std::string audio;   //!< The key of the tick's audio its preset hears (Lane::HeardAudio); not there: nothing.

    auto operator==(const LanePlan&) const -> bool = default;
};

/// Switcher node id -> the input that passes. A switcher missing here passes its first wired input.
using SwitcherSelection = std::map<std::string, int>;

/// Walks back from the viewport, following only each switcher's selected input. `graph` must be valid
/// (model::ValidateGraph); unfinished parts compile to nothing.
auto CompilePlan(const model::Graph& graph, const SwitcherSelection& selection = {}) -> RenderPlan;

/// Walks back from the viewport as CompilePlan does, also through presets wired into other presets' inputs, and
/// orders the presets upstream first. A loop of presets is refused (RenderProgram::errors). Pure: no GL.
auto CompileProgram(const model::Graph& graph, const SwitcherSelection& selection = {}) -> RenderProgram;

/// What the lane draws for `program`: the video branches into its top preset (the lane renders that one only
/// for now), or its picture with no preset. `audio` is the key of what that preset hears (LanePlan::audio).
auto LanePlanOf(const RenderProgram& program, std::string audio) -> LanePlan;

/// `steps` with every modulated effect's pass redone from this frame's `signals`. A signal missing from
/// `signals` leaves its setting as it is.
auto Modulated(const std::vector<VideoStep>& steps, const model::SignalValues& signals) -> std::vector<VideoStep>;
/// True if any of `steps` has a modulated effect.
auto HasModulations(const std::vector<VideoStep>& steps) -> bool;

/// True if `scene` can be played: its first preset entry has a file that was found, or it has no preset and
/// shows a picture on the viewport.
auto Playable(const model::Scene& scene) -> bool;

/// True when the instances prepared for `before` (at its first entry) can't show `after` and must be prepared
/// again (ScenePool::Remap's presetChanged): its first preset changed, or it went between a preset and a picture
/// with no preset, either way, including through a step with nothing wired.
auto NeedsNewInstances(const model::Scene& before, const model::Scene& after) -> bool;

/// The selection that passes the `item`-th wired input of the switcher feeding the viewport
/// (Scene::presetEntries entry `item`). Empty when there's no such switcher.
auto SelectionForItem(const model::Graph& graph, int item) -> SwitcherSelection;

} // namespace gv::engine
