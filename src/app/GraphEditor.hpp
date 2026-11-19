#pragma once

#include "app/nodes/NodeUi.hpp"
#include "model/Graph.hpp"
#include "thumbnails/ThumbnailService.hpp"

#include <imgui.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ax::NodeEditor {
struct EditorContext;
}

namespace gv::app {

class AppActions;
struct ShellState;

/// The node canvas: draws a scene's graph with imgui-node-editor and turns what the user does on it (wiring,
/// moving, deleting, adding, dropping, editing a node's controls) into graph edits through
/// AppActions::EditGraph, which validates them.
///
/// Node positions live in the graph (and its undo history); zoom and pan are kept per scene for the session
/// only. Wires are checked before they're made: inputs that would refuse the wire being dragged are dimmed, and
/// a refused wire or edit shows every reason near the mouse.
class GraphEditor
{
public:
    GraphEditor() = default;
    ~GraphEditor();

    GraphEditor(const GraphEditor&) = delete;
    auto operator=(const GraphEditor&) -> GraphEditor& = delete;

    /// Draws scene `scene` in the rest of the current window.
    void Draw(ShellState& state, AppActions& actions, int scene);

    /// The only node selected on scene `sceneId`'s canvas, or empty.
    auto SelectedNode(const std::string& sceneId) const -> std::string;

    /// Whether the mouse was over the canvas last frame (Finder drops then add nodes there).
    auto Hovered() const -> bool
    {
        return m_hovered;
    }
    /// Adds a preset or video source node for each dropped file, where the mouse was, as one undo step.
    void DropFiles(ShellState& state, AppActions& actions, int scene, const std::vector<std::filesystem::path>& files);

    /// The preset thumbnails the canvas showed this frame, for ThumbnailService::SetWanted. Cleared by Draw.
    auto WantedThumbnails() const -> const std::vector<thumbnails::Request>&
    {
        return m_wantedThumbnails;
    }
    void ClearWantedThumbnails()
    {
        m_wantedThumbnails.clear();
    }

private:
    friend class CanvasNodeContext;

    struct Pin
    {
        std::string node;
        bool output{false};
        int input{0};
        std::string texture;
        model::PortKind kind{model::PortKind::Video};
        ImVec2 center{0, 0}; //!< In canvas coordinates.
    };

    /// Per scene: the editor (with its zoom and pan) and what we last told it about node positions.
    struct Canvas
    {
        ax::NodeEditor::EditorContext* context{nullptr};
        std::map<std::string, ImVec2> pushed; //!< Node position last given to the editor, by node id.
        int frames{0};
        std::vector<std::string> selectedNodes;
        std::vector<uint64_t> selectedLinks;
    };

    struct ErrorNote
    {
        std::vector<model::GraphError> errors;
        ImVec2 position;
        std::chrono::steady_clock::time_point shown;
    };

    auto CanvasFor(const std::string& sceneId) -> Canvas&;
    void Configure();
    void FitView(const model::Graph& graph, ImVec2 size);
    /// Pans by a scroll (two fingers or the wheel), or zooms at `pointer` (relative to the canvas) when `zoom`.
    void Scroll(ImVec2 wheel, bool zoom, ImVec2 pointer, ImVec2 size);

    // Drawing.
    void DrawNode(const model::Node& node, NodeContext& context, bool compact);
    void DrawTitleRow(const model::Node& node, NodeContext& context, bool compact, const std::vector<InputSpec>& inputs);
    void DrawPin(const model::Node& node, bool output, const InputSpec* spec, model::PortKind kind, float height);
    void DrawLinks(const model::Graph& graph);
    void HandleCreate(ShellState& state, AppActions& actions, int scene, const model::Graph& graph);
    /// A wire picked up by its input end: over `target` (an input or output), or over empty canvas.
    void HandleMove(AppActions& actions, int scene, const Pin* target);
    void DrawMovingWire();
    void HandleDelete(AppActions& actions, int scene, const model::Graph& graph);
    void HandleEdits(AppActions& actions, int scene, const model::Graph& graph);
    void DrawPopups(NodeContext& context, AppActions& actions, const model::Graph& graph, int scene);
    void HandleDrop(ShellState& state, AppActions& actions, int scene, const model::Graph& graph, ImVec2 min, ImVec2 max);
    void DrawErrors();

    // Edits.
    void Delete(AppActions& actions, int scene, const model::Graph& graph, const std::vector<std::string>& nodes, const std::vector<uint64_t>& links);
    void AddNodeAt(AppActions& actions, int scene, model::NodeType type, ImVec2 at, const std::function<void(model::Node&)>& configure = {});
    void ShowErrors(std::vector<model::GraphError> errors);

    // Ids: hashes of the model's names, so they're stable across frames, edits and undo.
    static auto NodeKey(const std::string& node) -> uint64_t;
    static auto OutputKey(const std::string& node) -> uint64_t;
    static auto InputKey(const std::string& node, int input, const std::string& texture) -> uint64_t;
    static auto WireKey(const model::Wire& wire) -> uint64_t;
    /// Whether the pin being dragged (last frame) can be wired to `pin`; true when nothing is dragged.
    auto Accepts(const model::Graph& graph, const Pin& pin) -> bool;

    std::map<std::string, Canvas> m_canvases; //!< By scene id.
    Canvas* m_canvas{nullptr};                //!< The one being drawn.
    std::string m_sceneId;

    // Rebuilt every frame while drawing.
    std::map<uint64_t, Pin> m_pins;
    std::map<uint64_t, std::string> m_nodeIds;
    std::map<uint64_t, model::Wire> m_wires;
    std::map<uint64_t, bool> m_accepts; //!< Accepts() results this frame, by pin key.
    std::map<std::string, std::vector<std::string>> m_textures; //!< PresetTextures, by path.
    std::vector<thumbnails::Request> m_wantedThumbnails;
    std::string m_livePresetNode;
    std::vector<std::string> m_hoverErrorNodes; //!< Outlined while a refused wire is hovered.
    std::string m_hoverTooltip;
    std::string m_bodyTooltip;

    std::optional<Pin> m_dragStart; //!< The pin a wire is being dragged from (last frame).
    std::optional<model::Wire> m_moving;   //!< A wire picked up by its input end, while it's dragged.
    std::optional<model::Graph> m_dragGraph; //!< The graph without the picked-up wire, to check where it may go.
    ImVec4 m_movingColor{1, 1, 1, 1};
    ImVec2 m_canvasMin{0, 0}; //!< The canvas on screen.
    ImVec2 m_canvasMax{0, 0};
    std::string m_popupNode;        //!< The node whose popup is open, and which one.
    int m_popupKind{0};
    bool m_openNodePopup{false};
    ImVec2 m_addAt{0, 0};             //!< Canvas position for nodes from the add menu.
    std::string m_contextNode;        //!< The node whose context menu is open.
    ImVec2 m_mouseCanvas{0, 0};       //!< The mouse in canvas coordinates, this frame.
    std::string m_nodeUnderMouse;     //!< Hit-tested ourselves (also during external drags).
    std::optional<ErrorNote> m_errors;
    bool m_errorsPending{false};
    std::vector<model::GraphError> m_pendingErrors;
    bool m_hovered{false};
    bool m_fitRequested{false};
    bool m_previewBehind{false}; //!< The live output is drawn behind the nodes this frame.
    bool m_choosePreset{false}; //!< The add menu's "Preset..." was picked: ask for a file after the canvas.
    const model::Graph* m_graph{nullptr};
    float m_zoom{1.f};
};

} // namespace gv::app
