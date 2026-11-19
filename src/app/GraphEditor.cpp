#include "app/GraphEditor.hpp"

#include "app/Shell.hpp"
#include "app/Widgets.hpp"
#include "app/macos/FileDialog.hpp"
#include "library/PresetParsing.hpp"

#include <imgui_internal.h>
#include <imgui_node_editor.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace ed = ax::NodeEditor;

namespace gv::app {

namespace {

const ImVec4 kVideoWire{0.42f, 0.64f, 0.98f, 1.f};
const ImVec4 kFrameWire{0.96f, 0.62f, 0.30f, 1.f};
constexpr float kCompactZoom = 0.75f; //!< Below this zoom, nodes show only their title bar (N4.3).
constexpr float kPinSize = 12.f;
constexpr float kArrowWidth = 14.f;
constexpr float kNodeRounding = 6.f;
constexpr float kErrorDismissDistance = 80.f; //!< The error list goes when the mouse moves this far away.

auto KindColor(model::PortKind kind) -> ImVec4
{
    return kind == model::PortKind::Video ? kVideoWire : kFrameWire;
}

auto Faded(ImVec4 color, float alpha) -> ImVec4
{
    color.w *= alpha;
    return color;
}

auto HeaderColor(model::NodeType type) -> ImVec4
{
    switch (type)
    {
        case model::NodeType::VideoSource:
            return ImVec4(0.19f, 0.32f, 0.52f, 0.60f);
        case model::NodeType::Preset:
            return ImVec4(0.40f, 0.26f, 0.52f, 0.60f);
        case model::NodeType::Switcher:
            return ImVec4(0.50f, 0.37f, 0.16f, 0.60f);
        case model::NodeType::Viewport:
            return ImVec4(0.16f, 0.42f, 0.32f, 0.60f);
        default:
            return ImVec4(0.30f, 0.32f, 0.37f, 0.60f); // effects
    }
}

/// 64-bit FNV-1a, never 0 (an invalid id for the editor).
auto Hash(const std::string& text) -> uint64_t
{
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : text)
    {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash != 0 ? hash : 1;
}

/// `text`, shortened with "..." to fit `width`.
auto Fit(std::string text, float width) -> std::string
{
    if (ImGui::CalcTextSize(text.c_str()).x <= width)
        return text;
    while (!text.empty() && ImGui::CalcTextSize((text + "...").c_str()).x > width)
        text.pop_back();
    return text + "...";
}

auto Joined(const std::vector<model::GraphError>& errors) -> std::string
{
    std::string text;
    for (const auto& error : errors)
        text += (text.empty() ? "" : "\n") + error.message;
    return text;
}

} // namespace

// --- NodeContext ----------------------------------------------------------------------------------

NodeContext::NodeContext(GraphEditor& editor, ShellState& shellState, AppActions& appActions, int sceneIndex)
    : state(shellState)
    , actions(appActions)
    , scene(sceneIndex)
    , performance(shellState.document->Get())
    , sceneData(performance.scenes[static_cast<size_t>(sceneIndex)])
    , live(shellState.engine != nullptr && shellState.engine->LiveScene() == sceneIndex)
    , m_editor(editor)
{
}

void NodeContext::Edit(const model::Node& node, const char* label, const char* field, const std::function<void(model::Node&)>& change)
{
    const std::string id = node.id;
    auto errors = actions.EditGraph(
        scene,
        [&](model::Graph& graph) {
            if (model::Node* edited = graph.Find(id))
                change(*edited);
        },
        label, std::string(field) + ":" + sceneData.id + ":" + id);
    if (!errors.empty())
        m_editor.ShowErrors(std::move(errors));
}

void NodeContext::EditOnce(const model::Node& node, const char* label, const std::function<void(model::Node&)>& change)
{
    const std::string id = node.id;
    auto errors = actions.EditGraph(
        scene,
        [&](model::Graph& graph) {
            if (model::Node* edited = graph.Find(id))
                change(*edited);
        },
        label);
    if (!errors.empty())
        m_editor.ShowErrors(std::move(errors));
}

void NodeContext::EndEditIfReleased()
{
    if (ImGui::IsItemDeactivatedAfterEdit())
        actions.EndEdit();
}

void NodeContext::InputRow(const model::Node& node, const InputSpec& spec)
{
    const float height = ImGui::GetTextLineHeight() + 4.f;
    m_editor.DrawPin(node, false, &spec, spec.kind, height);
    ImGui::SameLine(0.f, 6.f);
    const bool accepts = m_editor.Accepts(*m_editor.m_graph, GraphEditor::Pin{node.id, false, spec.input, spec.texture, spec.kind});
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.f);
    ImGui::TextColored(accepts ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : kDim, "%s", spec.label.c_str());
}

void NodeContext::OpenPopup(const model::Node& node, int kind)
{
    m_editor.m_popupNode = node.id;
    m_editor.m_popupKind = kind;
    m_editor.m_openNodePopup = true;
}

void NodeContext::Tooltip(const char* text)
{
    if (ImGui::IsItemHovered() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        m_editor.m_bodyTooltip = text;
}

void NodeContext::Hint(const char* text)
{
    ImGui::SameLine();
    ImGui::TextColored(kDim, "(?)");
    Tooltip(text);
}

auto NodeContext::IsLivePreset(const model::Node& node) const -> bool
{
    return !m_editor.m_livePresetNode.empty() && node.id == m_editor.m_livePresetNode;
}

auto NodeContext::TexturesOf(const std::filesystem::path& preset) -> const std::vector<std::string>&
{
    auto it = m_editor.m_textures.find(preset.string());
    if (it == m_editor.m_textures.end())
    {
        std::error_code ec;
        std::vector<std::string> names;
        if (!preset.empty() && std::filesystem::is_regular_file(preset, ec))
            names = library::PresetTextures(preset);
        it = m_editor.m_textures.emplace(preset.string(), std::move(names)).first;
    }
    return it->second;
}

auto NodeContext::Thumbnail(const std::filesystem::path& preset) -> unsigned int
{
    if (state.library == nullptr || state.thumbnails == nullptr || preset.empty())
        return 0;
    const std::string id = state.library->IdForPath(preset);
    const library::PresetInfo* info = id.empty() ? nullptr : state.library->Find(id);
    if (info == nullptr)
        return 0;
    m_editor.m_wantedThumbnails.push_back({id, preset, std::to_string(info->modified) + ":" + std::to_string(info->size)});
    const auto tile = state.thumbnails->Get(id);
    return tile ? tile->texture : 0;
}

// --- GraphEditor ---------------------------------------------------------------------------------

GraphEditor::~GraphEditor()
{
    for (auto& [id, canvas] : m_canvases)
        ed::DestroyEditor(canvas.context);
}

auto GraphEditor::NodeKey(const std::string& node) -> uint64_t
{
    return Hash("n|" + node);
}

auto GraphEditor::OutputKey(const std::string& node) -> uint64_t
{
    return Hash("o|" + node);
}

auto GraphEditor::InputKey(const std::string& node, int input, const std::string& texture) -> uint64_t
{
    return Hash("i|" + node + "|" + std::to_string(input) + "|" + texture);
}

auto GraphEditor::WireKey(const model::Wire& wire) -> uint64_t
{
    return Hash("w|" + wire.toNode + "|" + std::to_string(wire.toInput) + "|" + wire.textureName);
}

auto GraphEditor::CanvasFor(const std::string& sceneId) -> Canvas&
{
    auto it = m_canvases.find(sceneId);
    if (it != m_canvases.end())
        return it->second;
    ed::Config config;
    config.SettingsName = nullptr; // positions are in the graph; zoom and pan aren't saved (plan §5)
    config.CustomZoomLevels.clear();
    for (float level : {0.25f, 0.35f, 0.5f, 0.6f, 0.75f, 0.9f, 1.0f, 1.25f, 1.5f, 2.0f})
        config.CustomZoomLevels.push_back(level);
    Canvas canvas;
    canvas.context = ed::CreateEditor(&config);
    ed::SetCurrentEditor(canvas.context);
    Configure();
    ed::SetCurrentEditor(nullptr);
    return m_canvases.emplace(sceneId, std::move(canvas)).first->second;
}

void GraphEditor::Configure()
{
    ed::Style& style = ed::GetStyle();
    style.NodePadding = ImVec4(8, 6, 8, 8);
    style.NodeRounding = kNodeRounding;
    style.NodeBorderWidth = 1.f;
    style.HoveredNodeBorderWidth = 2.f;
    style.SelectedNodeBorderWidth = 2.f;
    style.PinRounding = 4.f;
    style.LinkStrength = 80.f;
    style.Colors[ed::StyleColor_Bg] = ImVec4(0.085f, 0.09f, 0.10f, 1.f);
    style.Colors[ed::StyleColor_Grid] = ImVec4(1.f, 1.f, 1.f, 0.035f);
    style.Colors[ed::StyleColor_NodeBg] = ImVec4(0.15f, 0.155f, 0.17f, 0.97f);
    style.Colors[ed::StyleColor_NodeBorder] = ImVec4(1.f, 1.f, 1.f, 0.10f);
    style.Colors[ed::StyleColor_HovNodeBorder] = Faded(kAccent, 0.8f);
    style.Colors[ed::StyleColor_SelNodeBorder] = kAccent;
    style.Colors[ed::StyleColor_HovLinkBorder] = Faded(kAccent, 0.8f);
    style.Colors[ed::StyleColor_SelLinkBorder] = kAccent;
    style.Colors[ed::StyleColor_NodeSelRect] = Faded(kAccent, 0.15f);
    style.Colors[ed::StyleColor_NodeSelRectBorder] = Faded(kAccent, 0.5f);
    style.Colors[ed::StyleColor_PinRect] = Faded(kAccent, 0.25f);
    style.Colors[ed::StyleColor_PinRectBorder] = Faded(kAccent, 0.6f);
}

auto GraphEditor::SelectedNode(const std::string& sceneId) const -> std::string
{
    auto it = m_canvases.find(sceneId);
    if (it == m_canvases.end() || it->second.selectedNodes.size() != 1)
        return {};
    return it->second.selectedNodes.front();
}

void GraphEditor::Draw(ShellState& state, AppActions& actions, int sceneIndex)
{
    const auto& performance = state.document->Get();
    const auto& scene = performance.scenes[static_cast<size_t>(sceneIndex)];
    const model::Graph& graph = scene.graph;
    m_graph = &graph;
    m_sceneId = scene.id;

    // Forget the canvases of deleted scenes once a few have piled up (undo brings a scene back with a fresh view).
    if (m_canvases.size() > performance.scenes.size() + 8)
    {
        std::set<std::string> ids;
        for (const auto& s : performance.scenes)
            ids.insert(s.id);
        for (auto it = m_canvases.begin(); it != m_canvases.end();)
        {
            if (ids.count(it->first) == 0)
            {
                ed::DestroyEditor(it->second.context);
                it = m_canvases.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    Canvas& canvas = CanvasFor(scene.id);
    m_canvas = &canvas;

    // Toolbar.
    if (ImGui::SmallButton("Fit"))
        m_fitRequested = true;
    ImGui::SameLine();
    ImGui::TextColored(kDim, "%.0f%%   Right-click: add a node.  Drag from a pin: wire.  Delete: remove the selection.", m_zoom * 100.f);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(ImGui::GetContentRegionAvail().x, 50.f), std::max(ImGui::GetContentRegionAvail().y, 50.f));
    const ImVec2 corner(origin.x + size.x, origin.y + size.y);
    m_hovered = ImGui::IsMouseHoveringRect(origin, corner) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    ed::SetCurrentEditor(canvas.context);
    ed::Begin("graph", size);
    if (canvas.frames == 1 || m_fitRequested) // node sizes are known after the first frame
    {
        FitView(graph, size);
        m_fitRequested = false;
    }
    m_zoom = ed::GetCurrentZoom();
    const bool compact = m_zoom < kCompactZoom;
    m_mouseCanvas = ImGui::GetMousePos(); // inside the canvas, ImGui's mouse is in canvas coordinates
    m_pins.clear();
    m_nodeIds.clear();
    m_wires.clear();
    m_accepts.clear();
    m_bodyTooltip.clear();
    m_openNodePopup = false;

    // The preset node on screen.
    m_livePresetNode.clear();
    if (state.engine != nullptr && state.engine->LiveScene() == sceneIndex)
    {
        const auto presets = scene.Presets();
        if (!presets.empty())
        {
            const int item = std::clamp(state.engine->LiveItem(), 0, static_cast<int>(presets.size()) - 1);
            for (const auto& node : graph.nodes)
            {
                if (node.type == model::NodeType::Preset && model::ParamsOf<model::PresetParams>(node).path == presets[static_cast<size_t>(item)])
                {
                    m_livePresetNode = node.id;
                    break;
                }
            }
        }
    }

    // Nodes named by the errors on show, or by the refused wire hovered last frame, get a red outline.
    std::set<std::string> outlined(m_hoverErrorNodes.begin(), m_hoverErrorNodes.end());
    if (m_errors)
    {
        for (const auto& error : m_errors->errors)
            outlined.insert(error.nodeId);
    }

    NodeContext context(*this, state, actions, sceneIndex);
    m_nodeUnderMouse.clear();
    for (const auto& node : graph.nodes)
    {
        const uint64_t key = NodeKey(node.id);
        m_nodeIds[key] = node.id;
        const ImVec2 position(node.x, node.y);
        auto pushed = canvas.pushed.find(node.id);
        if (pushed == canvas.pushed.end() || pushed->second.x != position.x || pushed->second.y != position.y)
        {
            ed::SetNodePosition(key, position); // new, undone, or moved by an edit
            canvas.pushed[node.id] = position;
        }
        const bool red = outlined.count(node.id) > 0;
        if (red)
        {
            ed::PushStyleColor(ed::StyleColor_NodeBorder, kRed);
            ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 2.5f);
        }
        DrawNode(node, context, compact);
        if (red)
        {
            ed::PopStyleVar();
            ed::PopStyleColor();
        }
        const ImVec2 at = ed::GetNodePosition(key);
        const ImVec2 extent = ed::GetNodeSize(key);
        if (m_mouseCanvas.x >= at.x && m_mouseCanvas.y >= at.y && m_mouseCanvas.x < at.x + extent.x && m_mouseCanvas.y < at.y + extent.y)
            m_nodeUnderMouse = node.id; // the last one drawn is on top
    }
    DrawLinks(graph);
    HandleCreate(state, actions, sceneIndex, graph);
    HandleDelete(actions, sceneIndex, graph);
    HandleEdits(actions, sceneIndex, graph);

    canvas.selectedNodes.clear();
    canvas.selectedLinks.clear();
    if (const int count = ed::GetSelectedObjectCount(); count > 0)
    {
        std::vector<ed::NodeId> nodes(static_cast<size_t>(count));
        nodes.resize(static_cast<size_t>(ed::GetSelectedNodes(nodes.data(), count)));
        for (const auto& id : nodes)
        {
            if (auto it = m_nodeIds.find(id.Get()); it != m_nodeIds.end())
                canvas.selectedNodes.push_back(it->second);
        }
        std::vector<ed::LinkId> links(static_cast<size_t>(count));
        links.resize(static_cast<size_t>(ed::GetSelectedLinks(links.data(), count)));
        for (const auto& id : links)
            canvas.selectedLinks.push_back(id.Get());
    }

    DrawPopups(context, graph, sceneIndex);
    ed::End();
    ed::SetCurrentEditor(nullptr);

    HandleDrop(state, actions, sceneIndex, graph, origin, corner);

    // Backspace deletes too (the editor itself handles Delete).
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
        ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && (!canvas.selectedNodes.empty() || !canvas.selectedLinks.empty()))
    {
        Delete(actions, sceneIndex, graph, canvas.selectedNodes, canvas.selectedLinks);
    }

    if (m_choosePreset)
    {
        m_choosePreset = false;
        if (auto file = ChooseFile("Choose a preset", {"milk"}))
        {
            const auto path = *file;
            AddNodeAt(actions, sceneIndex, model::NodeType::Preset, m_addAt,
                      [&](model::Node& node) { model::ParamsOf<model::PresetParams>(node).path = path; });
        }
    }

    DrawErrors();
    canvas.frames++;
}

void GraphEditor::FitView(const model::Graph& graph, ImVec2 size)
{
    if (graph.nodes.empty())
        return;
    ImVec2 min(FLT_MAX, FLT_MAX);
    ImVec2 max(-FLT_MAX, -FLT_MAX);
    for (const auto& node : graph.nodes)
    {
        ImVec2 extent = ed::GetNodeSize(NodeKey(node.id));
        if (extent.x <= 0.f)
            extent = ImVec2(NodeWidth(node.type), 120.f);
        min = ImVec2(std::min(min.x, node.x), std::min(min.y, node.y));
        max = ImVec2(std::max(max.x, node.x + extent.x), std::max(max.y, node.y + extent.y));
    }
    constexpr float kMargin = 40.f;
    const float width = max.x - min.x + kMargin * 2.f;
    const float height = max.y - min.y + kMargin * 2.f;
    // Fit the whole graph, but never so small that titles can't be read, nor bigger than 100%.
    const float zoom = std::clamp(std::min(size.x / width, size.y / height), 0.6f, 1.0f);
    const ImVec2 visible(size.x / zoom, size.y / zoom);
    // Centred when it fits; otherwise from the top left, where the graph starts.
    const float left = visible.x >= width ? (min.x + max.x - visible.x) * 0.5f : min.x - kMargin;
    const float top = visible.y >= height ? (min.y + max.y - visible.y) * 0.5f : min.y - kMargin;
    ed::SetViewState({ImVec2(left, top), ImVec2(left + visible.x, top + visible.y)});
}

void GraphEditor::DrawNode(const model::Node& node, NodeContext& context, bool compact)
{
    const uint64_t key = NodeKey(node.id);
    const ImVec4 padding = ed::GetStyle().NodePadding;
    context.width = NodeWidth(node.type) - padding.x - padding.z;
    const auto inputs = NodeInputs(node, context);
    const bool titleOnly = compact || !node.expanded;

    ed::BeginNode(key);
    const float titleTop = ImGui::GetCursorScreenPos().y;
    DrawTitleRow(node, context, titleOnly, inputs);
    const float titleBottom = titleTop + ImGui::GetFrameHeight();
    if (!titleOnly)
    {
        ImGui::Dummy(ImVec2(0.f, 1.f));
        ImGui::PushItemWidth(context.width);
        DrawNodeBody(node, context);
        ImGui::PopItemWidth();
    }
    ed::EndNode();

    // The title bar's colour, behind the node's contents.
    if (ImDrawList* draw = ed::GetNodeBackgroundDrawList(key))
    {
        const ImVec2 at = ed::GetNodePosition(key);
        const ImVec2 extent = ed::GetNodeSize(key);
        if (extent.x > 0.f)
        {
            const ImU32 color = ImGui::GetColorU32(HeaderColor(node.type));
            if (titleOnly)
            {
                draw->AddRectFilled(at, ImVec2(at.x + extent.x, at.y + extent.y), color, kNodeRounding);
            }
            else
            {
                const float bottom = std::min(titleBottom + 4.f, at.y + extent.y);
                draw->AddRectFilled(at, ImVec2(at.x + extent.x, bottom), color, kNodeRounding, ImDrawFlags_RoundCornersTop);
            }
        }
    }
}

void GraphEditor::DrawTitleRow(const model::Node& node, NodeContext& context, bool compact, const std::vector<InputSpec>& inputs)
{
    const float height = ImGui::GetFrameHeight();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const auto output = model::OutputKind(node);

    // Collapsed nodes keep their input pins, in a row before the title.
    if (compact)
    {
        for (const auto& spec : inputs)
        {
            DrawPin(node, false, &spec, spec.kind, height);
            ImGui::SameLine(0.f, 2.f);
        }
    }

    const NodeStatus status = NodeStatusOf(node, context);
    const ImVec2 dot = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(10.f, height));
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(dot.x + 5.f, dot.y + height * 0.5f), 4.f, ImGui::GetColorU32(status.color));
    if (!status.tooltip.empty())
        context.Tooltip(status.tooltip.c_str());
    ImGui::SameLine(0.f, 6.f);

    const float right = kArrowWidth + (output ? 4.f + kPinSize : 0.f);
    const float available = start.x + context.width - right - ImGui::GetCursorScreenPos().x - 4.f;
    const std::string full = NodeTitle(node, context);
    const std::string title = Fit(full, std::max(available, 10.f));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(title.c_str());
    if (title != full)
        context.Tooltip(full.c_str());

    // Expand / collapse, then the output pin, at the right edge.
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(start.x + context.width - right, start.y));
    const ImVec2 arrow = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##expand", ImVec2(kArrowWidth, height)))
        context.EditOnce(node, node.expanded ? "Collapse Node" : "Expand Node", [](model::Node& n) { n.expanded = !n.expanded; });
    const ImU32 arrowColor = ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : kDim);
    const ImVec2 c(arrow.x + kArrowWidth * 0.5f, arrow.y + height * 0.5f);
    if (node.expanded)
        ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(c.x - 4.f, c.y - 2.f), ImVec2(c.x + 4.f, c.y - 2.f), ImVec2(c.x, c.y + 3.f), arrowColor);
    else
        ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(c.x - 2.f, c.y - 4.f), ImVec2(c.x - 2.f, c.y + 4.f), ImVec2(c.x + 3.f, c.y), arrowColor);
    context.Tooltip(node.expanded ? "Collapse to the title bar" : "Expand");
    if (output)
    {
        ImGui::SameLine(0.f, 4.f);
        DrawPin(node, true, nullptr, *output, height);
    }
}

auto GraphEditor::Accepts(const model::Graph& graph, const Pin& pin) -> bool
{
    if (!m_dragStart)
        return true;
    const Pin& from = *m_dragStart;
    const uint64_t key = pin.output ? OutputKey(pin.node) : InputKey(pin.node, pin.input, pin.texture);
    const uint64_t fromKey = from.output ? OutputKey(from.node) : InputKey(from.node, from.input, from.texture);
    if (key == fromKey)
        return true;
    if (from.output == pin.output)
        return false;
    if (auto it = m_accepts.find(key); it != m_accepts.end())
        return it->second;
    const Pin& out = from.output ? from : pin;
    const Pin& in = from.output ? pin : from;
    const bool accepts = model::ConnectErrors(graph, out.node, in.node, in.input, in.texture).empty();
    m_accepts[key] = accepts;
    return accepts;
}

void GraphEditor::DrawPin(const model::Node& node, bool output, const InputSpec* spec, model::PortKind kind, float height)
{
    Pin pin{node.id, output, spec != nullptr ? spec->input : 0, spec != nullptr ? spec->texture : std::string(), kind};
    const uint64_t key = output ? OutputKey(node.id) : InputKey(node.id, pin.input, pin.texture);
    const bool accepts = Accepts(*m_graph, pin);
    bool connected = false;
    if (output)
    {
        connected = std::any_of(m_graph->wires.begin(), m_graph->wires.end(), [&](const model::Wire& wire) { return wire.fromNode == node.id; });
    }
    else
    {
        connected = m_graph->WireInto(node.id, pin.input, pin.texture) != nullptr;
    }
    m_pins[key] = std::move(pin);

    ed::BeginPin(key, output ? ed::PinKind::Output : ed::PinKind::Input);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(kPinSize, height));
    const ImVec2 center(at.x + kPinSize * 0.5f, at.y + height * 0.5f);
    ed::PinPivotRect(center, center);
    ed::PinRect(ImVec2(center.x - 7.f, center.y - 7.f), ImVec2(center.x + 7.f, center.y + 7.f));
    ed::EndPin();

    const ImU32 color = ImGui::GetColorU32(Faded(KindColor(kind), accepts ? 1.f : 0.25f));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (connected)
        draw->AddCircleFilled(center, 5.f, color);
    else
        draw->AddCircle(center, 4.5f, color, 0, 1.6f);
}

void GraphEditor::DrawLinks(const model::Graph& graph)
{
    for (const auto& wire : graph.wires)
    {
        const model::Node* from = graph.Find(wire.fromNode);
        const auto kind = from != nullptr ? model::OutputKind(*from) : std::nullopt;
        if (!kind)
            continue;
        const uint64_t key = WireKey(wire);
        m_wires[key] = wire;
        ed::Link(key, OutputKey(wire.fromNode), InputKey(wire.toNode, wire.toInput, wire.textureName), KindColor(*kind), 2.f);
    }
}

void GraphEditor::HandleCreate(ShellState& /*state*/, AppActions& actions, int scene, const model::Graph& graph)
{
    m_hoverErrorNodes.clear();
    m_hoverTooltip.clear();
    if (ed::BeginCreate(ImVec4(1.f, 1.f, 1.f, 0.8f), 2.f))
    {
        ed::PinId a;
        ed::PinId b;
        if (ed::QueryNewLink(&a, &b))
        {
            auto first = m_pins.find(a.Get());
            auto second = m_pins.find(b.Get());
            if (first != m_pins.end() && second != m_pins.end())
            {
                m_dragStart = first->second;
                const Pin out = first->second.output ? first->second : second->second;
                const Pin in = first->second.output ? second->second : first->second;
                std::vector<model::GraphError> errors;
                if (first->second.output == second->second.output)
                    errors.push_back({"", first->second.output ? "Both ends are outputs: wire an output (right) to an input (left)."
                                                               : "Both ends are inputs: wire an output (right) to an input (left)."});
                else
                    errors = model::ConnectErrors(graph, out.node, in.node, in.input, in.texture);
                if (!errors.empty())
                {
                    ed::RejectNewItem(kRed, 2.f);
                    m_hoverTooltip = Joined(errors);
                    for (const auto& error : errors)
                        m_hoverErrorNodes.push_back(error.nodeId);
                    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                        ShowErrors(std::move(errors));
                }
                else if (ed::AcceptNewItem(kGreen, 3.f))
                {
                    auto refused = actions.EditGraph(
                        scene, [&](model::Graph& g) { model::Connect(g, out.node, in.node, in.input, in.texture); }, "Connect");
                    if (!refused.empty())
                        ShowErrors(std::move(refused));
                }
            }
        }
        else if (ed::QueryNewNode(&a))
        {
            if (auto it = m_pins.find(a.Get()); it != m_pins.end())
                m_dragStart = it->second;
        }
    }
    else
    {
        m_dragStart.reset();
    }
    ed::EndCreate();
}

void GraphEditor::HandleDelete(AppActions& actions, int scene, const model::Graph& graph)
{
    std::vector<std::string> nodes;
    std::vector<uint64_t> links;
    if (ed::BeginDelete())
    {
        // Rejected in the editor: the graph edit below removes them, or refuses with reasons.
        ed::LinkId link;
        while (ed::QueryDeletedLink(&link))
        {
            links.push_back(link.Get());
            ed::RejectDeletedItem();
        }
        ed::NodeId node;
        while (ed::QueryDeletedNode(&node))
        {
            if (auto it = m_nodeIds.find(node.Get()); it != m_nodeIds.end())
                nodes.push_back(it->second);
            ed::RejectDeletedItem();
        }
    }
    ed::EndDelete();
    if (!nodes.empty() || !links.empty())
        Delete(actions, scene, graph, nodes, links);
}

void GraphEditor::HandleEdits(AppActions& actions, int scene, const model::Graph& /*graph*/)
{
    if (!ed::BeginEdits())
        return;
    std::vector<std::pair<std::string, ImVec2>> moved;
    ed::Edit edit;
    while (ed::QueryEdit(&edit))
    {
        if (edit.Type != ed::EditType::Move)
            continue;
        for (int i = 0; i < edit.NodeCount; ++i)
        {
            if (auto it = m_nodeIds.find(edit.NodesAfter[i].Id.Get()); it != m_nodeIds.end())
                moved.emplace_back(it->second, edit.NodesAfter[i].Position);
        }
    }
    ed::EndEdits();
    if (moved.empty())
        return;
    // One undo step per drag: the editor reports a drag once it ends.
    for (const auto& [id, position] : moved)
        m_canvas->pushed[id] = position;
    actions.EditGraph(
        scene,
        [&](model::Graph& g) {
            for (const auto& [id, position] : moved)
            {
                if (model::Node* node = g.Find(id))
                {
                    node->x = position.x;
                    node->y = position.y;
                }
            }
        },
        moved.size() == 1 ? "Move Node" : "Move Nodes");
}

void GraphEditor::DrawPopups(NodeContext& context, const model::Graph& graph, int scene)
{
    ed::Suspend(); // popups and tooltips draw outside the zoomed canvas
    ed::NodeId contextNode;
    if (ed::ShowNodeContextMenu(&contextNode))
    {
        if (auto it = m_nodeIds.find(contextNode.Get()); it != m_nodeIds.end())
        {
            m_contextNode = it->second;
            ImGui::OpenPopup("node-context");
        }
    }
    else if (ed::ShowBackgroundContextMenu())
    {
        m_addAt = m_mouseCanvas;
        ImGui::OpenPopup("add-node");
    }
    if (m_openNodePopup)
        ImGui::OpenPopup("node-popup");

    if (ImGui::BeginPopup("add-node"))
    {
        using model::NodeType;
        ImGui::TextColored(kDim, "Add");
        auto item = [&](const char* label, NodeType type, const std::function<void(model::Node&)>& configure = {}) {
            if (ImGui::MenuItem(label))
                AddNodeAt(context.actions, scene, type, m_addAt, configure);
        };
        item("Video file", NodeType::VideoSource);
        item("Syphon video", NodeType::VideoSource, [](model::Node& node) { model::ParamsOf<model::VideoSourceParams>(node).source.type = model::VideoSourceType::Syphon; });
        ImGui::Separator();
        item("Contrast", NodeType::Contrast);
        item("Saturation", NodeType::Saturation);
        item("Luma gate", NodeType::LumaGate);
        item("Mix", NodeType::Mix);
        ImGui::Separator();
        if (ImGui::MenuItem("Preset..."))
            m_choosePreset = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Or drag a preset from the Presets panel onto the canvas.");
        item("Preset switcher", NodeType::Switcher);
        item("Video switcher", NodeType::Switcher, [](model::Node& node) { model::ParamsOf<model::SwitcherParams>(node).kind = model::PortKind::Video; });
        if (graph.Viewport() == nullptr)
        {
            ImGui::Separator();
            item("Viewport", NodeType::Viewport);
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("node-context"))
    {
        if (const model::Node* node = graph.Find(m_contextNode))
        {
            ImGui::TextColored(kDim, "%s", model::DisplayName(node->type));
            if (ImGui::MenuItem(node->expanded ? "Collapse" : "Expand"))
                context.EditOnce(*node, node->expanded ? "Collapse Node" : "Expand Node", [](model::Node& n) { n.expanded = !n.expanded; });
            if (ImGui::MenuItem("Delete", nullptr, false, node->type != model::NodeType::Viewport))
                Delete(context.actions, scene, graph, {node->id}, {});
        }
        else
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("node-popup"))
    {
        if (const model::Node* node = graph.Find(m_popupNode))
            DrawNodePopup(*node, m_popupKind, context);
        else
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (!m_hoverTooltip.empty())
        ImGui::SetTooltip("%s", m_hoverTooltip.c_str());
    else if (!m_bodyTooltip.empty() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        ImGui::SetTooltip("%s", m_bodyTooltip.c_str());
    ed::Resume();
}

void GraphEditor::HandleDrop(ShellState& state, AppActions& actions, int scene, const model::Graph& graph, ImVec2 min, ImVec2 max)
{
    if (!ImGui::BeginDragDropTargetCustom(ImRect(min, max), ImGui::GetID("graph-drop")))
        return;
    const model::Node* target = graph.Find(m_nodeUnderMouse);
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPresetDragPayload))
    {
        const std::filesystem::path path = static_cast<const char*>(payload->Data);
        if (target != nullptr && target->type == model::NodeType::Preset)
        {
            const std::string id = target->id;
            auto errors = actions.EditGraph(scene, [&](model::Graph& g) { model::ParamsOf<model::PresetParams>(*g.Find(id)).path = path; }, "Replace Preset");
            if (!errors.empty())
                ShowErrors(std::move(errors));
        }
        else
        {
            AddNodeAt(actions, scene, model::NodeType::Preset, m_mouseCanvas, [&](model::Node& node) { model::ParamsOf<model::PresetParams>(node).path = path; });
        }
    }
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kVideoDragPayload))
    {
        const std::string videoId = static_cast<const char*>(payload->Data);
        auto useFile = [&](model::Node& node) {
            auto& source = model::ParamsOf<model::VideoSourceParams>(node).source;
            source = {model::VideoSourceType::File, videoId, {}, {}, {}};
        };
        if (target != nullptr && target->type == model::NodeType::VideoSource)
        {
            const std::string id = target->id;
            auto errors = actions.EditGraph(scene, [&](model::Graph& g) { useFile(*g.Find(id)); }, "Change Video");
            if (!errors.empty())
                ShowErrors(std::move(errors));
        }
        else
        {
            AddNodeAt(actions, scene, model::NodeType::VideoSource, m_mouseCanvas, useFile);
        }
    }
    (void)state;
    ImGui::EndDragDropTarget();
}

void GraphEditor::DropFiles(ShellState& state, AppActions& actions, int scene, const std::vector<std::filesystem::path>& files)
{
    auto edited = state.document->Get();
    if (scene < 0 || scene >= static_cast<int>(edited.scenes.size()))
        return;
    auto& graph = edited.scenes[static_cast<size_t>(scene)].graph;
    ImVec2 at = m_mouseCanvas;
    int added = 0;
    for (const auto& file : files)
    {
        std::string id;
        if (Lower(file.extension().string()) == ".milk")
        {
            id = model::AddNode(graph, model::NodeType::Preset, at.x, at.y);
            model::ParamsOf<model::PresetParams>(*graph.Find(id)).path = file;
        }
        else if (IsVideoFile(file))
        {
            const std::string videoId = model::AddPoolVideo(edited, file);
            id = model::AddNode(graph, model::NodeType::VideoSource, at.x, at.y);
            model::ParamsOf<model::VideoSourceParams>(*graph.Find(id)).source = {model::VideoSourceType::File, videoId, {}, {}, {}};
        }
        if (!id.empty())
        {
            added++;
            at.y += 60.f; // several files: a staggered column
            at.x += 30.f;
        }
    }
    if (added > 0 && model::ValidateGraph(graph).empty())
        actions.Edit(std::move(edited), added == 1 ? "Drop File" : "Drop Files");
}

void GraphEditor::AddNodeAt(AppActions& actions, int scene, model::NodeType type, ImVec2 at, const std::function<void(model::Node&)>& configure)
{
    auto errors = actions.EditGraph(
        scene,
        [&](model::Graph& graph) {
            const std::string id = model::AddNode(graph, type, at.x, at.y);
            if (configure)
                configure(*graph.Find(id));
        },
        std::string("Add ") + model::DisplayName(type));
    if (!errors.empty())
        ShowErrors(std::move(errors));
}

void GraphEditor::Delete(AppActions& actions, int scene, const model::Graph& graph, const std::vector<std::string>& nodes,
                         const std::vector<uint64_t>& links)
{
    std::vector<std::string> removable;
    std::vector<model::GraphError> errors;
    for (const auto& id : nodes)
    {
        const model::Node* node = graph.Find(id);
        if (node != nullptr && node->type == model::NodeType::Viewport)
            errors.push_back({id, "The viewport can't be deleted: it's what the scene shows."});
        else if (node != nullptr)
            removable.push_back(id);
    }
    std::vector<model::Wire> wires;
    for (uint64_t key : links)
    {
        if (auto it = m_wires.find(key); it != m_wires.end())
            wires.push_back(it->second);
    }
    if (!removable.empty() || !wires.empty())
    {
        const char* label = !removable.empty() ? (removable.size() == 1 ? "Delete Node" : "Delete Nodes") : (wires.size() == 1 ? "Delete Wire" : "Delete Wires");
        auto refused = actions.EditGraph(
            scene,
            [&](model::Graph& g) {
                for (const auto& wire : wires)
                    model::Disconnect(g, wire.toNode, wire.toInput, wire.textureName);
                for (const auto& id : removable)
                    model::RemoveNode(g, id);
            },
            label);
        errors.insert(errors.end(), refused.begin(), refused.end());
    }
    if (!errors.empty())
        ShowErrors(std::move(errors));
}

void GraphEditor::ShowErrors(std::vector<model::GraphError> errors)
{
    m_pendingErrors = std::move(errors);
    m_errorsPending = true;
}

void GraphEditor::DrawErrors()
{
    const auto now = std::chrono::steady_clock::now();
    const ImVec2 mouse = ImGui::GetMousePos();
    if (m_errorsPending)
    {
        m_errors = ErrorNote{std::move(m_pendingErrors), mouse, now};
        m_pendingErrors.clear();
        m_errorsPending = false;
    }
    if (!m_errors)
        return;
    // Up for 3 s plus 1 s per extra error, or until the mouse moves away.
    const double seconds = std::chrono::duration<double>(now - m_errors->shown).count();
    const double lifetime = 3.0 + static_cast<double>(m_errors->errors.size() > 0 ? m_errors->errors.size() - 1 : 0);
    const float dx = mouse.x - m_errors->position.x;
    const float dy = mouse.y - m_errors->position.y;
    if (seconds > lifetime || std::sqrt(dx * dx + dy * dy) > kErrorDismissDistance)
    {
        m_errors.reset();
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(m_errors->position.x + 14.f, m_errors->position.y + 14.f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.22f, 0.11f, 0.11f, 0.97f));
    ImGui::Begin("##graph-errors", nullptr,
                 ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
    for (const auto& error : m_errors->errors)
        ImGui::TextColored(kRed, "%s", error.message.c_str());
    ImGui::End();
    ImGui::PopStyleColor();
}

} // namespace gv::app
