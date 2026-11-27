#pragma once

// How each node type looks and behaves on the canvas: one file per type in app/nodes/, gathered by NodeUis().
// The node's model side (ports, rules, saving, rendering) is its NodeDef in model/nodes/.
//
// Nodes draw through NodeContext, an interface: GraphEditor implements it for the canvas, and the tests in
// tests/nodes/ implement it with a fake to check each node's title, state, inputs and controls.

#include "engine/SwitcherClock.hpp"
#include "model/Graph.hpp"
#include "model/Performance.hpp"
#include "model/nodes/NodeDef.hpp"

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace gv::library {
class Library;
}

namespace gv::app {

/// One input pin of a node: which input, what it takes, and its row label. Modulation inputs aren't rows:
/// their pins sit on their slider's row (Control::param).
struct InputSpec
{
    int input{0};
    std::string texture; //!< Preset texture inputs only.
    model::PortKind kind{model::PortKind::Picture};
    std::string label;
    bool passing{false}; //!< Drawn highlighted: a live switcher passes this input.
    std::string tooltip{}; //!< Shown when the row is hovered; empty: none.

    auto operator==(const InputSpec&) const -> bool = default;
};

/// The dot in a node's title bar.
enum class NodeState
{
    Idle,    //!< Grey: nothing to say.
    Empty,   //!< Dim: nothing chosen yet.
    Missing, //!< Red: its file isn't there.
    Ready,   //!< White: loaded and waiting to go live.
    Waiting, //!< Amber: live, but its video isn't playing yet.
    Live,    //!< Green: on screen.
};

struct NodeStatus
{
    NodeState state{NodeState::Idle};
    std::string tooltip;

    auto operator==(const NodeStatus&) const -> bool = default;
};

/// The dot's colour.
auto StateColor(NodeState state) -> ImVec4;

struct SyphonServer
{
    std::string appName;
    std::string serverName;
};

/// A preset node's browser (A6), as the canvas left it: not saved in the show, and not undone.
struct PresetBrowserState
{
    bool open{false};          //!< The node shows the browser.
    bool list{false};          //!< Names in a list, not a grid of pictures.
    bool wholeLibrary{false};  //!< The search looks through the whole library, not just the folder.
    std::string search;
    float folderScroll{0.f};   //!< The first folder row shown (fractions while scrolling).
    float presetScroll{0.f};   //!< The first row of presets shown.
};

/// What a node draws with and what it may do. Everything about the app around the canvas (the engine,
/// library, file dialogs, Syphon) comes through here, so a node can be tested without it.
class NodeContext
{
public:
    virtual ~NodeContext() = default;

    float width{0.f}; //!< Content width of the node being drawn.
    int control{-1};  //!< The index of the control being drawn (a Custom control opens its list with it).

    // The scene.
    virtual auto Performance() const -> const model::Performance& = 0;
    virtual auto SceneGraph() const -> const model::Graph& = 0;
    virtual auto Live() const -> bool = 0;                                //!< The scene is live.
    virtual auto IsLivePreset(const model::Node& node) const -> bool = 0; //!< This preset node is on screen.
    virtual auto IsLiveVideo(const model::Node& node) const -> bool = 0;  //!< This video source node is on screen.
    virtual auto SceneReady() const -> bool = 0;                          //!< Loaded, ready to go live.
    /// The live scene's video, as "Video: <state>" text, and whether it's playing. Nullopt: none.
    virtual auto VideoState() const -> std::optional<std::pair<bool, std::string>> = 0;
    /// Switcher `node`, while its scene is live. Nullopt: not live (or not running).
    virtual auto Switcher(const model::Node& node) const -> std::optional<engine::SwitcherState> = 0;
    /// The live preset's render time in ms, and whether that's too heavy for 60 fps. Nullopt: not measured.
    virtual auto FrameTime() const -> std::optional<std::pair<double, bool>> = 0;
    virtual auto FileExists(const std::filesystem::path& path) const -> bool = 0;
    /// Why the ISF shader at `shader` didn't compile when its scene was prepared (the compiler's message), or "".
    virtual auto ShaderError(const std::filesystem::path& shader) const -> std::string = 0;
    /// This frame's value of every Number output in the scene (model::EvaluateSignals).
    virtual auto Signals() const -> model::SignalValues = 0;
    /// What the scene's Number outputs follow now: the running clock's position and the time.
    virtual auto SignalInputs() const -> model::SignalInputs = 0;
    /// The texture names `preset` samples.
    virtual auto TexturesOf(const std::filesystem::path& preset) -> const std::vector<std::string>& = 0;

    // Pictures (GL textures; 0 for none).
    virtual auto LivePicture() -> unsigned int = 0;
    /// The library's picture of `preset`, drawn from `min` to `max` (ImGui coordinates). While that's too small
    /// to see or off the canvas, the last picture stays but no new frames are rendered for it.
    virtual auto Thumbnail(const std::filesystem::path& preset, ImVec2 min, ImVec2 max) -> unsigned int = 0;

    // The library. Nullopt: the preset isn't in it.
    virtual auto IsFavourite(const std::filesystem::path& preset) const -> std::optional<bool> = 0;
    virtual void ToggleFavourite(const std::filesystem::path& preset) = 0;
    /// The indexed library, to step through it. Null: not indexed yet.
    virtual auto PresetLibrary() const -> const library::Library* = 0;
    /// Random numbers (the Random preset button).
    virtual auto Random() -> std::mt19937& = 0;

    // Edits.
    /// Edits `node` as one undo step; consecutive edits of the same field coalesce (a slider drag). Shows the
    /// errors if the edit is refused.
    virtual void Edit(const model::Node& node, const char* label, const char* field, const std::function<void(model::Node&)>& change) = 0;
    /// Edits `node` as one undo step of its own (a checkbox, a choice from a list).
    virtual void EditOnce(const model::Node& node, const char* label, const std::function<void(model::Node&)>& change) = 0;
    /// Ends a coalesced edit when the last item (a slider) is released.
    virtual void EndEditIfReleased() = 0;
    /// Changes more than the graph (the video pool, say) as one undo step.
    virtual void EditPerformance(const std::function<void(model::Performance& performance, model::Graph& graph)>& change, const char* label) = 0;

    // The Mac.
    virtual auto ChooseFile(const char* title, const std::vector<std::string>& extensions) -> std::optional<std::filesystem::path> = 0;
    virtual void Reveal(const std::filesystem::path& path) = 0;
    virtual auto SyphonServers() -> std::vector<SyphonServer> = 0;

    // Playing.
    /// Switches switcher `node` to `input` now: a preset switcher goes live on that preset (entering its
    /// scene if needed), a video switcher of the live scene jumps to that video.
    virtual void JumpSwitcher(const model::Node& node, int input) = 0;

    // The canvas.
    /// A row with input pin `spec` of `node` at the left and its label. True if the label was double-clicked.
    virtual auto InputRow(const model::Node& node, const InputSpec& spec) -> bool = 0;
    /// A modulation input's pin `spec`, alone at the start of its setting's row (a slider with Control::param).
    virtual void ParamPin(const model::Node& node, const InputSpec& spec) = 0;
    /// Opens the list of control `kind` (its index) of `node`: DrawNodePopup draws it, outside the zoomed canvas.
    virtual void OpenPopup(const model::Node& node, int kind) = 0;
    /// Shows `text` as a tooltip if the last item is hovered.
    virtual void Tooltip(const char* text) = 0;
    /// The scroll (wheel steps, up positive) over the area from `min` to `max` this frame. While the pointer is
    /// over that area, scrolling scrolls it instead of panning the canvas.
    virtual auto Wheel(ImVec2 min, ImVec2 max) -> float = 0;
    /// `node`'s preset browser, kept by the canvas from frame to frame.
    virtual auto PresetBrowser(const model::Node& node) -> PresetBrowserState& = 0;
};

/// One control in a node's body. Most are declared (a checkbox, a choice) and drawn by DrawNodeBody; `Custom`
/// ones draw themselves. A node's sliders are its params (model::ParamDef): `Params` puts their rows here.
struct Control
{
    enum class Kind
    {
        Inputs,   //!< The node's input rows.
        Params,   //!< A slider row per model::ParamsFor entry (or just `param`'s), each with its pin (ParamSlider).
        Slider,   //!< A number: get/set, min..max. Hand-written only where it can't be a param yet.
        Checkbox, //!< On or off: get/set as 0 or 1.
        Choice,   //!< One of `options`: get/set the index. Radio buttons, or a button that opens a list.
        Text,     //!< A dim line: `text`.
        Custom,   //!< `draw` draws it (and `popup` its list, if it opens one).
    };
    Kind kind{Kind::Custom};
    const char* id{"##control"}; //!< The ImGui id, unique in the node.

    std::function<double(const model::Node&)> get{};   //!< Per-node controls (NodeUi::controlsFor) may capture.
    std::function<void(model::Node&, double)> set{};
    double min{0.0};
    double max{1.0};
    bool integer{false};
    bool logarithmic{false};
    const char* format{"%.2f"};                 //!< Slider format; formatFor overrides it.
    /// Sliders made by ParamSlider: the model::ParamsFor entry it edits. Its modulation pin starts the row and,
    /// while wired, the row also shows the modulated value (a marker on the slider) and the depth. -1: none.
    /// A `Params` control: the one param whose row goes here, so other controls can sit between; -1: all.
    int param{-1};
    const char* (*formatFor)(double){nullptr};  //!< The format for a value (contrast: decimals below 10).

    std::span<const char* const> options{}; //!< Choice labels, by index.
    bool radio{false};                    //!< Choice as radio buttons; else a button with a list.

    const char* label{nullptr};    //!< Checkbox label; Slider/Choice: text before it on the row ("Every").
    const char* undo{""};          //!< Undo label ("Change Contrast"); checkboxes: when turned on.
    const char* undoOff{nullptr};  //!< Checkboxes: the undo label when turned off; null: `undo`.
    const char* field{nullptr};    //!< Sliders: edits of this field coalesce while dragging.
    const char* tooltip{nullptr};
    bool sameLine{false};          //!< On the previous control's row.
    float width{0.f};              //!< 0: the rest of the row.

    bool (*visible)(const model::Node&){nullptr}; //!< Null: always shown.
    std::string (*text)(const model::Node&, NodeContext&){nullptr}; //!< Text controls; empty: not shown.
    bool warning{false};                                              //!< Text controls: red, not dim.

    void (*draw)(const model::Node&, NodeContext&){nullptr};  //!< Custom controls.
    void (*popup)(const model::Node&, NodeContext&){nullptr}; //!< Custom controls' list (OpenPopup with their index).
};

/// An entry of the canvas's right-click "Add" menu.
struct AddItem
{
    const char* label{""};
    void (*configure)(model::Node&){nullptr}; //!< Applied after the defaults; null: none.
    const char* tooltip{nullptr};
    bool chooseFile{false}; //!< The canvas asks for a preset file and adds nothing if none is chosen.
    bool (*available)(const model::Graph&){nullptr}; //!< Null: always offered.
};

/// Everything the canvas knows about one node type.
struct NodeUi
{
    model::NodeType type{model::NodeType::Viewport};
    float width{180.f};                        //!< Canvas width.
    ImVec4 header{0.30f, 0.32f, 0.37f, 0.60f}; //!< Title bar colour.
    int menuGroup{0};                          //!< Add menu: items are grouped (separated) by this, in order.

    /// The title bar's text. Null: model::Label(node).
    std::string (*title)(const model::Node&, NodeContext&){nullptr};
    /// The title bar's dot. Null: Live while the scene is live, else Idle.
    NodeStatus (*status)(const model::Node&, NodeContext&){nullptr};
    /// The input rows, in order. Null: one row per input, labelled `inputLabel`.
    std::vector<InputSpec> (*inputs)(const model::Node&, NodeContext&){nullptr};
    const char* inputLabel{"Input"};
    /// An input row's label was double-clicked. Null: nothing happens.
    void (*inputDoubleClicked)(const model::Node&, int input, NodeContext&){nullptr};

    std::vector<Control> controls; //!< The body below the title bar, top to bottom.
    /// The body for this node, in place of `controls` (an ISF shader's come from its file). Null: `controls`.
    std::vector<Control> (*controlsFor)(const model::Node&, NodeContext&){nullptr};
    std::vector<AddItem> addItems; //!< Empty: not in the Add menu.
};

/// The canvas's definition of `type`.
auto Ui(model::NodeType type) -> const NodeUi&;
/// Every node type's, in NodeType order.
auto NodeUis() -> std::span<const NodeUi>;

// Through the type's definition.
auto NodeTitle(const model::Node& node, NodeContext& context) -> std::string;
auto NodeStatusOf(const model::Node& node, NodeContext& context) -> NodeStatus;
auto NodeInputs(const model::Node& node, NodeContext& context) -> std::vector<InputSpec>;
/// A `Params` control for just param `param`'s row, so other controls can sit between a node's params.
inline auto ParamRow(int param) -> Control
{
    Control control;
    control.kind = Control::Kind::Params;
    control.param = param;
    return control;
}
/// The slider for `node`'s param `param` (NodeDef::params), all from its ParamDef: what a `Params` control
/// draws, one per param.
auto ParamSlider(const model::Node& node, int param) -> Control;
/// The node's controls: its type's controlsFor, else its controls; a `Params` control becomes its sliders.
auto NodeControls(const model::Node& node, NodeContext& context) -> std::vector<Control>;
/// The node's controls (and input rows), below its title bar.
void DrawNodeBody(const model::Node& node, NodeContext& context);
/// The list control `kind` opened (Choice or Custom).
void DrawNodePopup(const model::Node& node, int kind, NodeContext& context);

namespace nodes {

auto VideoSourceUi() -> NodeUi;
auto ContrastUi() -> NodeUi;
auto SaturationUi() -> NodeUi;
auto LumaGateUi() -> NodeUi;
auto OpacityUi() -> NodeUi;
auto MilkdropPresetUi() -> NodeUi;
auto SwitcherUi() -> NodeUi;
auto ViewportUi() -> NodeUi;
auto IsfShaderUi() -> NodeUi;
auto LfoUi() -> NodeUi;
auto AudioInUi() -> NodeUi;
auto EnvelopeUi() -> NodeUi;
auto BandUi() -> NodeUi;

/// Control get/set for one field of `Params` (a pointer to member): a number, or a bool as 0 or 1.
template <class Params, auto Member>
auto ParamGet(const model::Node& node) -> double
{
    return static_cast<double>(model::ParamsOf<Params>(node).*Member);
}
template <class Params, auto Member>
void ParamSet(model::Node& node, double value)
{
    auto& field = model::ParamsOf<Params>(node).*Member;
    field = static_cast<std::remove_reference_t<decltype(field)>>(value);
}

} // namespace nodes

} // namespace gv::app
