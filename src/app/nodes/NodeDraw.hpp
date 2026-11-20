#pragma once

#include "model/Graph.hpp"
#include "model/Performance.hpp"

#include <imgui.h>

#include <functional>
#include <string>
#include <vector>

namespace gv::app {

class AppActions;
class GraphEditor;
struct ShellState;

/// One input pin of a node: which input, what it takes, and its row label.
struct InputSpec
{
    int input{0};
    std::string texture; //!< Preset texture inputs only.
    model::PortKind kind{model::PortKind::Video};
    std::string label;
};

/// The state dot in a node's title bar.
struct NodeStatus
{
    ImVec4 color;
    std::string tooltip;
};

/// What a node body draws with. GraphEditor makes one per frame and points it at each node in turn.
///
/// Bodies draw inside the zoomed canvas, so anything that opens a window (combo lists, menus, tooltips) is
/// requested here and drawn by GraphEditor outside the canvas transform.
class NodeContext
{
public:
    NodeContext(GraphEditor& editor, ShellState& state, AppActions& actions, int scene);

    ShellState& state;
    AppActions& actions;
    const int scene;                       //!< Scene index.
    const model::Performance& performance;
    const model::Scene& sceneData;
    const bool live;                       //!< The scene is live.
    float width{0.f};                      //!< Content width of the node being drawn.

    /// Edits `node` through the graph gate as one undo step; consecutive edits of the same field coalesce
    /// (a slider drag). Shows the errors if the edit is refused.
    void Edit(const model::Node& node, const char* label, const char* field, const std::function<void(model::Node&)>& change);
    /// Edits `node` as one undo step of its own (a checkbox, a choice from a list).
    void EditOnce(const model::Node& node, const char* label, const std::function<void(model::Node&)>& change);
    /// Ends a coalesced edit when the last item (a slider) is released.
    void EndEditIfReleased();

    /// A row with input pin `spec` of `node` at the left and its label.
    void InputRow(const model::Node& node, const InputSpec& spec);

    /// Opens popup `kind` for `node` (DrawNodePopup draws it, outside the canvas).
    void OpenPopup(const model::Node& node, int kind);
    /// Shows `text` as a tooltip if the last item is hovered.
    void Tooltip(const char* text);
    /// "(?)" after the last item, with `text` as its tooltip.
    void Hint(const char* text);

    /// The preset node on screen, when the scene is live.
    auto IsLivePreset(const model::Node& node) const -> bool;
    /// The texture names `preset` samples (cached).
    auto TexturesOf(const std::filesystem::path& preset) -> const std::vector<std::string>&;
    /// Asks for `preset`'s library thumbnail this frame; returns its texture, or 0 if there's none yet.
    auto Thumbnail(const std::filesystem::path& preset) -> unsigned int;

private:
    GraphEditor& m_editor;
};

// Per node type, in nodes/*.cpp.

auto NodeWidth(model::NodeType type) -> float;
auto NodeTitle(const model::Node& node, NodeContext& context) -> std::string;
auto NodeStatusOf(const model::Node& node, NodeContext& context) -> NodeStatus;
/// Every input pin of `node`, in row order (preset textures included).
auto NodeInputs(const model::Node& node, NodeContext& context) -> std::vector<InputSpec>;
/// The node below its title bar: input rows and controls.
void DrawNodeBody(const model::Node& node, NodeContext& context);
/// Popup `kind` that the node's body opened.
void DrawNodePopup(const model::Node& node, int kind, NodeContext& context);

void DrawVideoSourceBody(const model::Node& node, NodeContext& context);
void DrawVideoSourcePopup(const model::Node& node, int kind, NodeContext& context);
auto VideoSourceTitle(const model::Node& node, const NodeContext& context) -> std::string;
auto VideoSourceStatus(const model::Node& node, NodeContext& context) -> NodeStatus;

void DrawEffectBody(const model::Node& node, NodeContext& context);

void DrawPresetBody(const model::Node& node, NodeContext& context);
auto PresetTitle(const model::Node& node) -> std::string;
auto PresetStatus(const model::Node& node, NodeContext& context) -> NodeStatus;
auto PresetInputs(const model::Node& node, NodeContext& context) -> std::vector<InputSpec>;

void DrawSwitcherBody(const model::Node& node, NodeContext& context);
void DrawSwitcherPopup(const model::Node& node, int kind, NodeContext& context);
auto SwitcherTitle(const model::Node& node) -> std::string;

void DrawViewportBody(const model::Node& node, NodeContext& context);

} // namespace gv::app
