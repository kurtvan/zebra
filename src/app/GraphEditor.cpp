#include "app/GraphEditor.hpp"

#include "app/MiniMapWidget.hpp"
#include "app/Shell.hpp"
#include "app/Widgets.hpp"
#include "app/WireLook.hpp"
#include "app/WireRoute.hpp"
#include "app/macos/FileDialog.hpp"
#include "io/VideoSource.hpp"
#include "model/Arrange.hpp"
#include "common/FitRect.hpp"
#include "common/ThumbnailVisible.hpp"
#include "library/PresetParsing.hpp"

#include <imgui_internal.h>
#include <imgui_node_editor.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace ed = ax::NodeEditor;

namespace gv::app {

namespace {

constexpr float kPinSize = 12.f;
constexpr float kArrowWidth = 14.f;
constexpr float kNodeRounding = 6.f;
constexpr float kErrorDismissDistance = 80.f; //!< The error list goes when the mouse moves this far away.
constexpr float kWireHitDistance = 6.f;       //!< Screen pixels from a wire that hover and click it.
constexpr int kUnderlayChannel = 5;           //!< imgui-node-editor's c_UserChannel_Underlay: over the grid, under nodes.

auto Faded(ImVec4 color, float alpha) -> ImVec4
{
    color.w *= alpha;
    return color;
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

// --- The canvas's NodeContext ----------------------------------------------------------------------

/// What nodes draw with on the canvas: the app's state, and edits through AppActions::EditGraph.
class CanvasNodeContext final : public NodeContext
{
public:
    CanvasNodeContext(GraphEditor& editor, ShellState& state, AppActions& actions, int scene)
        : m_editor(editor)
        , m_state(state)
        , m_actions(actions)
        , m_scene(scene)
        , m_performance(state.document->Get())
        , m_live(state.engine != nullptr && state.engine->LiveScene() == scene)
    {
    }

    auto Performance() const -> const model::Performance& override
    {
        return m_performance;
    }
    auto SceneGraph() const -> const model::Graph& override
    {
        return m_performance.scenes[static_cast<size_t>(m_scene)].graph;
    }
    auto Live() const -> bool override
    {
        return m_live;
    }
    auto IsLivePreset(const model::Node& node) const -> bool override
    {
        return !m_editor.m_livePresetNode.empty() && node.id == m_editor.m_livePresetNode;
    }
    auto IsLiveVideo(const model::Node& node) const -> bool override
    {
        return !m_editor.m_liveVideoNode.empty() && node.id == m_editor.m_liveVideoNode;
    }
    auto ShaderError(const std::filesystem::path& shader) const -> std::string override
    {
        return m_state.engine != nullptr ? m_state.engine->ShaderError(shader.string()) : std::string();
    }
    auto Switcher(const model::Node& node) const -> std::optional<engine::SwitcherState> override
    {
        if (m_state.engine == nullptr)
            return std::nullopt;
        return m_state.engine->Switcher(m_scene, node.id);
    }
    void JumpSwitcher(const model::Node& node, int input) override
    {
        m_actions.JumpSwitcher(m_scene, node.id, input);
    }
    auto SceneReady() const -> bool override
    {
        return m_state.engine != nullptr && m_state.engine->Scene(m_scene).state == engine::ScenePool::State::Ready;
    }
    auto VideoState() const -> std::optional<std::pair<bool, std::string>> override
    {
        if (m_state.video == nullptr || m_state.engine == nullptr)
            return std::nullopt;
        const auto source = m_state.engine->VideoSourceOf(m_scene);
        const auto state = source ? m_state.video->StateOf(*source) : std::nullopt;
        if (!state)
            return std::nullopt;
        return std::pair<bool, std::string>{*state == io::VideoState::Live, std::string("Video: ") + io::ToString(*state)};
    }
    auto FrameTime() const -> std::optional<std::pair<double, bool>> override
    {
        if (m_state.engine == nullptr)
            return std::nullopt;
        const auto info = m_state.engine->Scene(m_scene);
        return std::pair<double, bool>{info.frameMs, info.heavy};
    }
    auto FileExists(const std::filesystem::path& path) const -> bool override
    {
        std::error_code ec;
        return std::filesystem::is_regular_file(path, ec);
    }
    auto TexturesOf(const std::filesystem::path& preset) -> const std::vector<std::string>& override
    {
        auto it = m_editor.m_textures.find(preset.string());
        if (it == m_editor.m_textures.end())
        {
            std::vector<std::string> names;
            if (!preset.empty() && FileExists(preset))
                names = library::PresetTextures(preset);
            it = m_editor.m_textures.emplace(preset.string(), std::move(names)).first;
        }
        return it->second;
    }

    auto LivePicture() -> unsigned int override
    {
        return m_state.preview;
    }
    auto Thumbnail(const std::filesystem::path& preset, ImVec2 min, ImVec2 max) -> unsigned int override
    {
        if (m_state.library == nullptr || m_state.thumbnails == nullptr || preset.empty())
            return 0;
        const std::string id = m_state.library->IdForPath(preset);
        const library::PresetInfo* info = id.empty() ? nullptr : m_state.library->Find(id);
        if (info == nullptr)
            return 0;
        // Too small or off the canvas: keep the last picture, but don't render new frames.
        const ImVec2 screenMin = ed::CanvasToScreen(min);
        const ImVec2 screenMax = ed::CanvasToScreen(max);
        const ScreenRect picture{screenMin.x, screenMin.y, screenMax.x, screenMax.y};
        const ScreenRect canvas{m_editor.m_canvasMin.x, m_editor.m_canvasMin.y, m_editor.m_canvasMax.x, m_editor.m_canvasMax.y};
        auto& list = ThumbnailVisible(picture, canvas, kMinThumbnailWidth) ? m_editor.m_wantedThumbnails : m_editor.m_keptThumbnails;
        list.push_back({id, preset, std::to_string(info->modified) + ":" + std::to_string(info->size)});
        const auto tile = m_state.thumbnails->Get(id);
        return tile ? tile->texture : 0;
    }

    auto IsFavourite(const std::filesystem::path& preset) const -> std::optional<bool> override
    {
        const std::string id = LibraryId(preset);
        if (id.empty() || m_state.favourites == nullptr)
            return std::nullopt;
        return m_state.favourites->Contains(id);
    }
    void ToggleFavourite(const std::filesystem::path& preset) override
    {
        if (const std::string id = LibraryId(preset); !id.empty())
            m_actions.ToggleFavourite(id);
    }
    auto PresetLibrary() const -> const library::Library* override
    {
        return m_state.library;
    }
    auto Random() -> std::mt19937& override
    {
        return m_editor.m_random;
    }

    void Edit(const model::Node& node, const char* label, const char* field, const std::function<void(model::Node&)>& change) override
    {
        EditNode(node, label, std::string(field) + ":" + SceneGraphId() + ":" + node.id, change);
    }
    void EditOnce(const model::Node& node, const char* label, const std::function<void(model::Node&)>& change) override
    {
        EditNode(node, label, {}, change);
    }
    void EndEditIfReleased() override
    {
        if (ImGui::IsItemDeactivatedAfterEdit())
            m_actions.EndEdit();
    }
    void EditPerformance(const std::function<void(model::Performance&, model::Graph&)>& change, const char* label) override
    {
        auto edited = m_performance;
        change(edited, edited.scenes[static_cast<size_t>(m_scene)].graph);
        m_actions.Edit(std::move(edited), label);
        m_actions.EndEdit();
    }

    auto ChooseFile(const char* title, const std::vector<std::string>& extensions) -> std::optional<std::filesystem::path> override
    {
        return app::ChooseFile(title, extensions);
    }
    void Reveal(const std::filesystem::path& path) override
    {
        RevealInFinder(path);
    }
    auto SyphonServers() -> std::vector<SyphonServer> override
    {
        std::vector<SyphonServer> servers;
        for (const auto& server : io::ListSyphonServers())
            servers.push_back({server.appName, server.serverName});
        return servers;
    }

    auto Signals() const -> model::SignalValues override
    {
        return model::EvaluateSignals(SceneGraph(), SignalInputs());
    }
    auto SignalInputs() const -> model::SignalInputs override
    {
        return m_state.engine != nullptr ? m_state.engine->SignalInputsNow(m_scene) : model::SignalInputs{};
    }
    void ParamPin(const model::Node& node, const InputSpec& spec) override
    {
        m_editor.DrawPin(node, false, &spec, spec.kind, ImGui::GetFrameHeight());
    }

    auto InputRow(const model::Node& node, const InputSpec& spec) -> bool override
    {
        // The row grows to fit a pill, with the label centred beside it.
        const float base = ImGui::GetTextLineHeight() + 4.f;
        const float height = PinRowHeight(base, m_editor.WiresAt(node, false, &spec), PinSide::Left);
        m_editor.DrawPin(node, false, &spec, spec.kind, height);
        ImGui::SameLine(0.f, 6.f);
        const bool accepts = m_editor.Accepts(*m_editor.m_graph, GraphEditor::Pin{node.id, false, spec.input, spec.texture, spec.kind});
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.f + (height - base) * 0.5f);
        const ImVec4 color = !accepts ? kDim : spec.passing ? kAccent : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ImGui::TextColored(color, "%s", spec.label.c_str());
        return ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    }
    void OpenPopup(const model::Node& node, int kind) override
    {
        m_editor.m_popupNode = node.id;
        m_editor.m_popupKind = kind;
        m_editor.m_openNodePopup = true;
    }
    void Tooltip(const char* text) override
    {
        if (ImGui::IsItemHovered() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            m_editor.m_bodyTooltip = text;
    }
    auto Wheel(ImVec2 min, ImVec2 max) -> float override
    {
        // Remembered on screen: next frame decides, before the canvas starts, whether the wheel pans it.
        const ImVec2 screenMin = ed::CanvasToScreen(min);
        const ImVec2 screenMax = ed::CanvasToScreen(max);
        m_editor.m_wheelAreas.push_back({screenMin.x, screenMin.y, screenMax.x, screenMax.y});
        return ImGui::IsMouseHoveringRect(min, max, false) ? m_editor.m_areaWheel : 0.f;
    }
    auto PresetBrowser(const model::Node& node) -> PresetBrowserState& override
    {
        return m_editor.m_browsers[SceneGraphId() + ":" + node.id];
    }

private:
    auto SceneGraphId() const -> const std::string&
    {
        return m_performance.scenes[static_cast<size_t>(m_scene)].id;
    }
    auto LibraryId(const std::filesystem::path& preset) const -> std::string
    {
        if (m_state.library == nullptr || preset.empty())
            return {};
        std::string id = m_state.library->IdForPath(preset);
        return !id.empty() && m_state.library->Find(id) != nullptr ? id : std::string();
    }
    void EditNode(const model::Node& node, const char* label, const std::string& coalesceKey, const std::function<void(model::Node&)>& change)
    {
        const std::string id = node.id;
        auto errors = m_actions.EditGraph(
            m_scene,
            [&](model::Graph& graph) {
                if (model::Node* edited = graph.Find(id))
                    change(*edited);
            },
            label, coalesceKey);
        if (!errors.empty())
            m_editor.ShowErrors(std::move(errors));
    }

    GraphEditor& m_editor;
    ShellState& m_state;
    AppActions& m_actions;
    const int m_scene;
    const model::Performance& m_performance;
    const bool m_live;
};

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

void GraphEditor::FocusNode(const std::string& sceneId, const std::string& node)
{
    m_focusScene = sceneId;
    m_focusNode = node;
}

auto GraphEditor::TitleOf(ShellState& state, AppActions& actions, int scene, const model::Node& node) -> std::string
{
    CanvasNodeContext context(*this, state, actions, scene);
    return NodeTitle(node, context);
}

void GraphEditor::ApplyFocus(const model::Graph& graph, ImVec2 size)
{
    if (m_focusNode.empty() || m_focusScene != m_sceneId || m_canvas->frames < 2) // wait for the first fit
        return;
    const std::string id = std::exchange(m_focusNode, {});
    m_focusScene.clear();
    const model::Node* node = graph.Find(id);
    if (node == nullptr)
        return;
    const uint64_t key = NodeKey(id);
    ImVec2 extent = ed::GetNodeSize(key);
    if (extent.x <= 0.f)
        extent = ImVec2(Ui(node->type).width, 120.f);
    const ed::ViewState view = ed::GetViewState();
    const float scale = size.x / std::max(view.VisibleMax.x - view.VisibleMin.x, 1.f); // screen pixels per canvas unit
    const ImVec2 half(size.x / scale * 0.5f, size.y / scale * 0.5f);
    const ImVec2 centre(node->x + extent.x * 0.5f, node->y + extent.y * 0.5f);
    ed::SetViewState({ImVec2(centre.x - half.x, centre.y - half.y), ImVec2(centre.x + half.x, centre.y + half.y)});
    ed::SelectNode(key);
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
    if (ImGui::SmallButton("Arrange"))
        m_arrangeRequested = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Lays out the whole scene from its wires: sources on the left, the viewport on the right. Cmd+Z undoes it.");
    ImGui::SameLine();
    ImGui::TextColored(kDim, "%.0f%%", m_zoom * 100.f);
    Settings* settings = state.settings;
    if (settings != nullptr)
    {
        ImGui::SameLine(0.f, 16.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 1));
        if (ImGui::Checkbox("Preview behind", &settings->graphPreview))
            actions.SettingsChanged();
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shows the live output behind the nodes. Dimmed further while the scene on the canvas isn't the live one.");
        if (settings->graphPreview)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 1));
            float dim = settings->graphPreviewDim * 100.f;
            if (ImGui::SliderFloat("##dim", &dim, 0.f, 90.f, "dim %.0f%%"))
                settings->graphPreviewDim = dim / 100.f;
            if (ImGui::IsItemDeactivatedAfterEdit())
                actions.SettingsChanged();
            ImGui::PopStyleVar();
        }
    }
    if (settings != nullptr)
    {
        ImGui::SameLine(0.f, 16.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 1));
        if (ImGui::Checkbox("Mini-map", &settings->miniMap))
            actions.SettingsChanged();
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shows the whole graph in the canvas's corner. Click or drag on it to look there.");
    }
    ImGui::SameLine(0.f, 16.f);
    ImGui::TextColored(kDim, "Scroll: pan.  Pinch or Cmd+scroll: zoom.  F: fit.  Right-click: add.  Delete: remove.");

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(ImGui::GetContentRegionAvail().x, 50.f), std::max(ImGui::GetContentRegionAvail().y, 50.f));
    const ImVec2 corner(origin.x + size.x, origin.y + size.y);
    m_canvasMin = origin;
    m_canvasMax = corner;
    m_hovered = ImGui::IsMouseHoveringRect(origin, corner) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    // Scrolling, the Mac way: two fingers (or the wheel) pan, Cmd or Ctrl + scroll zooms at the pointer. The
    // editor's own wheel zoom is turned off by taking the wheel before it sees it.
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 wheel(io.MouseWheelH, io.MouseWheel);
    // The pointer on screen: once ed::Begin runs, ImGui's mouse is in canvas coordinates instead.
    const ImVec2 mouse = io.MousePos;
    const bool zoomKey = io.KeySuper || io.KeyCtrl;
    bool scrolled = m_hovered && (wheel.x != 0.f || wheel.y != 0.f) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    if (scrolled)
    {
        io.MouseWheel = 0.f;
        io.MouseWheelH = 0.f;
    }
    // Over a list inside a node (last frame's), the scroll is the list's instead.
    m_areaWheel = 0.f;
    if (scrolled && !zoomKey)
    {
        for (const auto& area : m_wheelAreas)
        {
            if (mouse.x >= area.left && mouse.x < area.right && mouse.y >= area.top && mouse.y < area.bottom)
            {
                m_areaWheel = wheel.y;
                scrolled = false;
            }
        }
    }
    m_wheelAreas.clear();
    // A trackpad pinch zooms at the pointer too; F fits, like the button, unless a text field is taking the keys.
    const bool noPopup = !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    const bool pinched = m_hovered && state.pinch != 1.f && noPopup;
    if (m_hovered && noPopup && !io.WantTextInput && !io.KeySuper && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
        m_fitRequested = true;

    // The live output behind the nodes, letterboxed, darkened so the nodes stay readable.
    const engine::Engine* engine = state.engine;
    const bool liveHere = engine != nullptr && engine->LiveScene() == sceneIndex;
    m_previewBehind = settings != nullptr && settings->graphPreview && state.preview != 0 && engine != nullptr;
    if (m_previewBehind)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const Rect fit = FitRect(engine->RenderWidth(), engine->RenderHeight(), static_cast<int>(size.x), static_cast<int>(size.y));
        const ImVec2 min(origin.x + static_cast<float>(fit.x), origin.y + static_cast<float>(fit.y));
        const ImVec2 max(min.x + static_cast<float>(fit.width), min.y + static_cast<float>(fit.height));
        draw->AddRectFilled(origin, corner, IM_COL32_BLACK);
        draw->AddImage(static_cast<ImTextureID>(state.preview), min, max, ImVec2(0, 1), ImVec2(1, 0)); // GL is bottom-up
        float dim = settings->graphPreviewDim;
        if (!liveHere)
            dim += (1.f - dim) * 0.6f; // another scene's output: mostly out of the way
        draw->AddRectFilled(origin, corner, IM_COL32(0, 0, 0, static_cast<int>(dim * 255.f)));
    }

    ed::SetCurrentEditor(canvas.context);
    if (m_previewBehind)
        ed::PushStyleColor(ed::StyleColor_Bg, ImVec4(0, 0, 0, 0));
    ed::Begin("graph", size);
    if (canvas.frames == 1 || m_fitRequested) // node sizes are known after the first frame
    {
        FitView(graph, size);
        m_fitRequested = false;
    }
    else if (scrolled)
    {
        Scroll(canvas.view, wheel, zoomKey, mouse);
    }
    else if (pinched)
    {
        canvas.view.Zoom(state.pinch, mouse);
    }
    std::optional<MiniMapFrame> miniMap; // before the nodes, so it takes the pointer from them
    if (settings == nullptr || settings->miniMap)
        miniMap = MiniMapInput(NodeRects(graph), origin, corner, canvas.view);
    m_zoom = ed::GetCurrentZoom();
    m_mouseCanvas = ImGui::GetMousePos(); // inside the canvas, ImGui's mouse is in canvas coordinates
    m_pins.clear();
    m_nodeIds.clear();
    m_wires.clear();
    m_accepts.clear();
    m_bodyTooltip.clear();
    m_openNodePopup = false;
    ApplyFocus(graph, size); // a row of the Nodes pane was clicked

    // The preset node and video source node on screen.
    m_livePresetNode.clear();
    m_liveVideoNode.clear();
    if (state.engine != nullptr && state.engine->LiveScene() == sceneIndex)
    {
        m_livePresetNode = state.engine->LivePlan().presetNode;
        m_liveVideoNode = state.engine->LivePlan().sourceNode;
    }

    // Nodes named by the errors on show, or by the refused wire hovered last frame, get a red outline.
    std::set<std::string> outlined(m_hoverErrorNodes.begin(), m_hoverErrorNodes.end());
    if (m_errors)
    {
        for (const auto& error : m_errors->errors)
            outlined.insert(error.nodeId);
    }

    CanvasNodeContext context(*this, state, actions, sceneIndex);
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
        DrawNode(node, context);
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
    if (m_arrangeRequested)
    {
        m_arrangeRequested = false;
        Arrange(actions, sceneIndex, graph);
    }

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

    DrawPopups(context, actions, graph, sceneIndex);
    ed::End();
    SelectClickedWire();
    if (m_previewBehind)
        ed::PopStyleColor();
    ed::SetCurrentEditor(nullptr);

    // Whose output is behind the nodes, when it isn't this scene's.
    if (m_previewBehind && !liveHere)
    {
        const int live = engine->LiveScene();
        const std::string label = live >= 0 && live < static_cast<int>(performance.scenes.size())
                                      ? "Behind: live scene \"" + performance.scenes[static_cast<size_t>(live)].DisplayName() + "\""
                                      : std::string("Behind: nothing is live");
        ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x + 10.f, origin.y + 8.f), ImGui::GetColorU32(kDim), label.c_str());
    }

    if (miniMap)
        DrawMiniMap(ImGui::GetWindowDrawList(), *miniMap);

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
            AddNodeAt(actions, sceneIndex, model::NodeType::MilkdropPreset, m_addAt,
                      [&](model::Node& node) { model::ParamsOf<model::MilkdropPresetParams>(node).path = path; });
        }
    }

    DrawErrors();
    canvas.frames++;
}

void GraphEditor::Scroll(EditorView& view, ImVec2 wheel, bool zoom, ImVec2 mouse)
{
    constexpr float kPanPixelsPerStep = 40.f; //!< Screen pixels per wheel step (trackpads send fractions).
    constexpr float kZoomPerStep = 1.12f;
    if (zoom)
    {
        view.Zoom(std::pow(kZoomPerStep, wheel.y + wheel.x), mouse);
        return;
    }
    // Like scrolling a page: the canvas follows the fingers (ImGui's wheel signs, as in its own windows).
    view.Pan(ImVec2(-wheel.x * kPanPixelsPerStep, -wheel.y * kPanPixelsPerStep));
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
            extent = ImVec2(Ui(node.type).width, 120.f);
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

auto GraphEditor::NodeRects(const model::Graph& graph) -> std::vector<CanvasRect>
{
    std::vector<CanvasRect> rects;
    rects.reserve(graph.nodes.size());
    for (const auto& node : graph.nodes)
    {
        ImVec2 extent = ed::GetNodeSize(NodeKey(node.id));
        if (extent.x <= 0.f)
            extent = ImVec2(Ui(node.type).width, 120.f);
        rects.push_back({ImVec2(node.x, node.y), ImVec2(node.x + extent.x, node.y + extent.y)});
    }
    return rects;
}

void GraphEditor::DrawNode(const model::Node& node, NodeContext& context)
{
    const uint64_t key = NodeKey(node.id);
    const ImVec4 padding = ed::GetStyle().NodePadding;
    context.width = Ui(node.type).width - padding.x - padding.z;
    const auto inputs = NodeInputs(node, context);
    const bool titleOnly = !node.expanded;

    ed::BeginNode(key);
    const float titleTop = ImGui::GetCursorScreenPos().y;
    const float titleBottom = titleTop + DrawTitleRow(node, context, inputs);
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
            const ImU32 color = ImGui::GetColorU32(Ui(node.type).header);
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

auto GraphEditor::DrawTitleRow(const model::Node& node, NodeContext& context, const std::vector<InputSpec>& inputs) -> float
{
    // The row grows to fit a pin's pill (the output's, or a collapsed node's inputs'); the rest stays centred.
    const float frame = ImGui::GetFrameHeight();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const auto output = model::OutputKind(node);
    float height = output ? PinRowHeight(frame, WiresAt(node, true, nullptr), PinSide::Right) : frame;
    if (!node.expanded)
    {
        for (const auto& spec : inputs)
            height = std::max(height, PinRowHeight(frame, WiresAt(node, false, &spec), PinSide::Left));
    }

    // Collapsed nodes keep their input pins, in a row before the title.
    if (!node.expanded)
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
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(dot.x + 5.f, dot.y + height * 0.5f), 4.f, ImGui::GetColorU32(StateColor(status.state)));
    if (!status.tooltip.empty())
        context.Tooltip(status.tooltip.c_str());
    ImGui::SameLine(0.f, 6.f);

    const float right = kArrowWidth + (output ? 4.f + kPinSize : 0.f);
    const float available = start.x + context.width - right - ImGui::GetCursorScreenPos().x - 4.f;
    const std::string full = NodeTitle(node, context);
    const std::string title = Fit(full, std::max(available, 10.f));
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, start.y + (height - frame) * 0.5f));
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
    return height;
}

auto GraphEditor::Accepts(const model::Graph& graph, const Pin& pin) -> bool
{
    if (!m_dragStart)
        return true;
    const model::Graph& checked = m_dragGraph ? *m_dragGraph : graph; // without a picked-up wire
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
    const bool accepts = model::ConnectErrors(checked, out.node, in.node, in.input, in.texture).empty();
    m_accepts[key] = accepts;
    return accepts;
}

void GraphEditor::DrawPin(const model::Node& node, bool output, const InputSpec* spec, model::PortKind kind, float height)
{
    Pin pin{node.id, output, spec != nullptr ? spec->input : 0, spec != nullptr ? spec->texture : std::string(), kind};
    const uint64_t key = output ? OutputKey(node.id) : InputKey(node.id, pin.input, pin.texture);
    const bool accepts = Accepts(*m_graph, pin);
    const int wires = WiresAt(node, output, spec);
    const PinSide side = output ? PinSide::Right : PinSide::Left;
    ed::BeginPin(key, output ? ed::PinKind::Output : ed::PinKind::Input);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(kPinSize, height));
    const ImVec2 center(at.x + kPinSize * 0.5f, at.y + height * 0.5f);
    pin.center = center;
    pin.side = side;
    pin.wires = wires;
    m_pins[key] = std::move(pin);
    // Several wires: a pill with a point for each (DrawLinks). Hover and drop on the whole pill.
    const PinShape shape = PinPill(center, wires, side);
    constexpr float kPinReach = 2.f; //!< How far round the dot or pill the pointer still finds the pin.
    ed::PinPivotRect(center, center);
    ed::PinRect(ImVec2(shape.min.x - kPinReach, shape.min.y - kPinReach), ImVec2(shape.max.x + kPinReach, shape.max.y + kPinReach));
    ed::EndPin();

    DrawPinShape(ImGui::GetWindowDrawList(), center, kind, wires, shape, ImGui::GetColorU32(Faded(WireColor(kind), accepts ? 1.f : 0.25f)));
}

auto GraphEditor::WiresAt(const model::Node& node, bool output, const InputSpec* spec) const -> int
{
    return static_cast<int>(std::count_if(m_graph->wires.begin(), m_graph->wires.end(), [&](const model::Wire& wire) {
        if (output)
            return wire.fromNode == node.id;
        return spec != nullptr && wire.toNode == node.id && model::WireInput(*m_graph, wire) == spec->input && wire.textureName == spec->texture;
    }));
}

void GraphEditor::DrawLinks(const model::Graph& graph)
{
    // The editor keeps a link per wire, for selection and delete, but with a negative thickness: it then never
    // draws it or hit-tests its curve. The wires are drawn and hit-tested here, as routed polylines.
    constexpr float kHiddenLink = -1.0e6f;
    const ImVec4 hidden(0, 0, 0, 0);
    struct Drawn
    {
        uint64_t key;
        ImVec4 color;
    };
    std::vector<Drawn> drawn;

    // The nodes, for the wires to route round.
    std::vector<WireBox> boxes;
    m_wireBox.clear();
    for (const auto& node : graph.nodes)
    {
        const ImVec2 at = ed::GetNodePosition(NodeKey(node.id));
        const ImVec2 extent = ed::GetNodeSize(NodeKey(node.id));
        m_wireBox[node.id] = static_cast<int>(boxes.size());
        boxes.push_back({at, ImVec2(at.x + extent.x, at.y + extent.y)});
    }
    // The wires with both pins on the canvas, the picked-up one too, so it keeps its place on its pins.
    struct Shown
    {
        const model::Wire* wire;
        uint64_t key;
        const Pin* out;
        const Pin* in;
        ImVec4 color;
    };
    std::vector<Shown> shown;
    for (const auto& wire : graph.wires)
    {
        const model::Node* from = graph.Find(wire.fromNode);
        if (from == nullptr || !model::OutputKind(*from))
            continue;
        const uint64_t key = WireKey(wire);
        m_wires[key] = wire;
        const bool moving = m_moving && *m_moving == wire; // picked up: DrawMovingWire draws it at the pointer
        const uint64_t out = OutputKey(wire.fromNode);
        const uint64_t in = InputKey(wire.toNode, model::WireInput(graph, wire), wire.textureName);
        if (!moving)
            ed::Link(key, out, in, hidden, kHiddenLink);
        auto a = m_pins.find(out);
        auto b = m_pins.find(in);
        const model::Node* to = wire.param.empty() ? nullptr : graph.Find(wire.toNode);
        const int param = to != nullptr ? model::ParamIndex(*to, wire.param) : -1;
        const bool broken = param >= 0 && model::ParamsFor(*to)[static_cast<size_t>(param)].lost;
        if (a != m_pins.end() && b != m_pins.end())
            shown.push_back({&wire, key, &a->second, &b->second, broken ? kBrokenWire : WireColor(*model::OutputKind(*from))});
    }

    // Each wire's ends: at a pin several wires share, its own point on the pill, in the order of the wires'
    // other ends (AttachSlots), so they don't cross at the pin and each keeps its own run out of it.
    std::vector<ImVec2> fromPoint(shown.size());
    std::vector<ImVec2> toPoint(shown.size());
    std::map<const Pin*, std::vector<size_t>> atPin;
    for (size_t i = 0; i < shown.size(); ++i)
    {
        atPin[shown[i].out].push_back(i);
        atPin[shown[i].in].push_back(i);
    }
    for (const auto& [pin, wires] : atPin)
    {
        std::vector<ImVec2> others;
        for (size_t i : wires)
            others.push_back((pin->output ? shown[i].in : shown[i].out)->center);
        const std::vector<int> slots = AttachSlots(others, pin->side);
        const std::vector<ImVec2> points = AttachPoints(pin->center, std::max(pin->wires, static_cast<int>(wires.size())), pin->side);
        for (size_t k = 0; k < wires.size(); ++k)
            (pin->output ? fromPoint : toPoint)[wires[k]] = points[static_cast<size_t>(slots[k])];
    }
    std::vector<WireEnds> ends;
    m_wireFrom.clear();
    for (size_t i = 0; i < shown.size(); ++i)
    {
        m_wireFrom[shown[i].key] = fromPoint[i];
        if (m_moving && *m_moving == *shown[i].wire)
            continue;
        drawn.push_back({shown[i].key, shown[i].color});
        ends.push_back({fromPoint[i], toPoint[i], WireBoxOf(shown[i].wire->fromNode), WireBoxOf(shown[i].wire->toNode)});
    }

    // Routed again only when a node or a wire changed.
    if (boxes != m_wireBoxes || ends != m_wireEnds)
    {
        m_wireLines = RouteWires(boxes, ends);
        m_wireBoxes = std::move(boxes);
        m_wireEnds = std::move(ends);
    }
    const std::vector<std::vector<ImVec2>>& lines = m_wireLines;

    // The wire under the pointer, over bare canvas (not over a node or pin, nor during a drag or a menu).
    m_hoveredWire = 0;
    const bool bare = m_hovered && m_nodeUnderMouse.empty() && !m_dragStart && !ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
                      !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    if (bare)
    {
        float nearest = kWireHitDistance / m_zoom;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const float distance = DistanceToPolyline(m_mouseCanvas, lines[i]);
            if (distance <= nearest)
            {
                nearest = distance;
                m_hoveredWire = drawn[i].key;
            }
        }
    }

    // Under the nodes, over the grid: the editor's underlay channel. Thicker over the preview, to stand out from
    // the visuals. One polyline per wire: its joins are mitred, so thick corners don't notch.
    const float thickness = m_previewBehind ? 3.f : 2.f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const int channel = draw->_Splitter._Current;
    draw->ChannelsSetCurrent(kUnderlayChannel);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const auto& line = lines[i];
        const int count = static_cast<int>(line.size());
        if (ed::IsLinkSelected(drawn[i].key))
            draw->AddPolyline(line.data(), count, ImGui::GetColorU32(kAccent), ImDrawFlags_None, thickness + 4.5f);
        else if (drawn[i].key == m_hoveredWire)
            draw->AddPolyline(line.data(), count, ImGui::GetColorU32(Faded(kAccent, 0.8f)), ImDrawFlags_None, thickness + 3.5f);
        draw->AddPolyline(line.data(), count, ImGui::GetColorU32(drawn[i].color), ImDrawFlags_None, thickness);
    }
    draw->ChannelsSetCurrent(channel);
}

auto GraphEditor::WireBoxOf(const std::string& node) const -> int
{
    auto it = m_wireBox.find(node);
    return it != m_wireBox.end() ? it->second : -1;
}

void GraphEditor::SelectClickedWire()
{
    // After ed::End: the editor took the click for the bare canvas and cleared the selection.
    if (m_hoveredWire == 0 || ed::GetBackgroundClickButtonIndex() != ImGuiMouseButton_Left)
        return;
    std::vector<ed::LinkId> links;
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeySuper || io.KeyCtrl) // add to the wires that were selected, or take it out
    {
        bool found = false;
        for (uint64_t link : m_canvas->selectedLinks)
        {
            if (link == m_hoveredWire)
                found = true;
            else
                links.emplace_back(link);
        }
        if (!found)
            links.emplace_back(m_hoveredWire);
    }
    else
    {
        links.emplace_back(m_hoveredWire);
    }
    ed::SetSelection(nullptr, 0, links.data(), static_cast<int>(links.size()));
}

void GraphEditor::HandleCreate(ShellState& /*state*/, AppActions& actions, int scene, const model::Graph& graph)
{
    m_hoverErrorNodes.clear();
    m_hoverTooltip.clear();
    // The editor's own line is hidden: DrawMovingWire draws the wire being dragged, routed like the others (a
    // moved wire from its source), in m_movingColor.
    const ImVec4 hidden(0, 0, 0, 0);
    if (ed::BeginCreate(hidden, 2.f))
    {
        ed::PinId a;
        ed::PinId b;
        const Pin* start = nullptr;
        const Pin* target = nullptr;
        bool overPin = false;
        if (ed::QueryNewLink(&a, &b))
        {
            overPin = true;
            if (auto it = m_pins.find(a.Get()); it != m_pins.end())
                start = &it->second;
            if (auto it = m_pins.find(b.Get()); it != m_pins.end())
                target = &it->second;
        }
        else if (ed::QueryNewNode(&a))
        {
            if (auto it = m_pins.find(a.Get()); it != m_pins.end())
                start = &it->second;
        }
        m_dragEnd = overPin && target != nullptr ? target->center : m_mouseCanvas;
        m_dragEndNode = overPin && target != nullptr && start != nullptr && target->output != start->output ? target->node : std::string();

        // Dragging from a wired input picks up that wire.
        const model::Wire* wire = start != nullptr && !start->output ? graph.WireInto(start->node, start->input, start->texture) : nullptr;
        if (wire != nullptr)
        {
            m_moving = *wire;
            m_dragGraph = graph;
            model::Disconnect(*m_dragGraph, wire->toNode, wire->toInput, wire->textureName);
            model::FollowWires(*m_dragGraph);
            if (auto source = m_pins.find(OutputKey(wire->fromNode)); source != m_pins.end())
                m_dragStart = source->second;
            HandleMove(actions, scene, overPin ? target : nullptr);
        }
        else if (start != nullptr)
        {
            m_moving.reset();
            m_dragGraph.reset();
            m_dragStart = *start;
            m_movingColor = ImVec4(1.f, 1.f, 1.f, 0.8f);
            if (overPin && target != nullptr)
            {
                const Pin out = start->output ? *start : *target;
                const Pin in = start->output ? *target : *start;
                std::vector<model::GraphError> errors;
                if (start->output == target->output)
                    errors.push_back({"", start->output ? "Both ends are outputs: wire an output (right) to an input (left)."
                                                        : "Both ends are inputs: wire an output (right) to an input (left)."});
                else
                    errors = model::ConnectErrors(graph, out.node, in.node, in.input, in.texture);
                m_movingColor = errors.empty() ? kGreen : kRed;
                if (!errors.empty())
                {
                    ed::RejectNewItem(hidden, 2.f);
                    m_hoverTooltip = Joined(errors);
                    for (const auto& error : errors)
                        m_hoverErrorNodes.push_back(error.nodeId);
                    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                        ShowErrors(std::move(errors));
                }
                else if (ed::AcceptNewItem(hidden, 3.f))
                {
                    auto refused = actions.EditGraph(
                        scene, [&](model::Graph& g) { model::Connect(g, out.node, in.node, in.input, in.texture); }, "Connect");
                    if (!refused.empty())
                        ShowErrors(std::move(refused));
                }
            }
        }
    }
    else
    {
        m_dragStart.reset();
        m_moving.reset();
        m_dragGraph.reset();
    }
    ed::EndCreate();
    DrawMovingWire();
}

void GraphEditor::HandleMove(AppActions& actions, int scene, const Pin* target)
{
    const ImVec4 hidden(0, 0, 0, 0);
    const model::Wire wire = *m_moving;

    if (target == nullptr)
    {
        // Over empty canvas: letting go disconnects it.
        const model::Node* from = m_dragGraph->Find(wire.fromNode);
        const auto kind = from != nullptr ? model::OutputKind(*from) : std::nullopt;
        m_movingColor = Faded(WireColor(kind.value_or(model::PortKind::Picture)), 0.6f);
        m_hoverTooltip = "Let go to disconnect.";
        if (ed::AcceptNewItem(hidden, 3.f))
        {
            actions.EditGraph(
                scene, [&](model::Graph& g) { model::Disconnect(g, wire.toNode, wire.toInput, wire.textureName); }, "Disconnect Wire");
        }
        return;
    }

    std::vector<model::GraphError> errors;
    const bool home = !target->output && target->node == wire.toNode && target->input == wire.toInput && target->texture == wire.textureName;
    if (target->output)
        errors.push_back({"", "Drop the wire on an input, or on empty canvas to disconnect it."});
    else if (!home)
        errors = model::ConnectErrors(*m_dragGraph, wire.fromNode, target->node, target->input, target->texture);
    if (!errors.empty())
    {
        m_movingColor = kRed;
        ed::RejectNewItem(hidden, 2.f);
        m_hoverTooltip = Joined(errors);
        for (const auto& error : errors)
            m_hoverErrorNodes.push_back(error.nodeId);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            ShowErrors(std::move(errors));
        return;
    }
    m_movingColor = kGreen;
    if (ed::AcceptNewItem(hidden, 3.f) && !home)
    {
        const Pin to = *target;
        auto refused = actions.EditGraph(
            scene,
            [&](model::Graph& g) {
                model::Disconnect(g, wire.toNode, wire.toInput, wire.textureName);
                model::Connect(g, wire.fromNode, to.node, to.input, to.texture);
            },
            "Move Wire");
        if (!refused.empty())
            ShowErrors(std::move(refused));
    }
}

void GraphEditor::DrawMovingWire()
{
    if (!m_dragStart)
        return;
    // Routed like the wires, round the nodes, from an output to an input: the pointer (or the pin under it)
    // stands in for the end not reached yet. On top of everything, in screen space, clipped to the canvas.
    // A picked-up wire leaves from its own point on its output's pill.
    const int start = WireBoxOf(m_dragStart->node);
    const int end = WireBoxOf(m_dragEndNode);
    ImVec2 from = m_dragStart->center;
    if (auto it = m_moving ? m_wireFrom.find(WireKey(*m_moving)) : m_wireFrom.end(); it != m_wireFrom.end())
        from = it->second;
    const WireEnds ends = m_dragStart->output ? WireEnds{from, m_dragEnd, start, end} : WireEnds{m_dragEnd, from, end, start};
    std::vector<ImVec2> line = RouteWires(m_wireBoxes, {ends}).front();
    for (ImVec2& point : line)
        point = ed::CanvasToScreen(point);
    const ImU32 color = ImGui::GetColorU32(m_movingColor);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->PushClipRect(m_canvasMin, m_canvasMax, true);
    draw->AddPolyline(line.data(), static_cast<int>(line.size()), color, ImDrawFlags_None, 3.f);
    draw->AddCircleFilled(ed::CanvasToScreen(m_dragEnd), 4.f, color);
    draw->PopClipRect();
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

void GraphEditor::Arrange(AppActions& actions, int scene, const model::Graph& graph)
{
    std::map<std::string, model::ArrangeSize> sizes;
    for (const auto& node : graph.nodes)
    {
        const ImVec2 extent = ed::GetNodeSize(NodeKey(node.id));
        if (extent.x > 0.f && extent.y > 0.f)
            sizes[node.id] = {extent.x, extent.y};
    }
    const auto positions = model::ArrangeLayered(graph, sizes);
    const bool moves = std::any_of(graph.nodes.begin(), graph.nodes.end(), [&](const model::Node& node) {
        const auto& at = positions.at(node.id);
        return at.x != node.x || at.y != node.y;
    });
    if (moves)
    {
        actions.EditGraph(
            scene,
            [&](model::Graph& g) {
                for (auto& node : g.nodes)
                {
                    if (auto it = positions.find(node.id); it != positions.end())
                    {
                        node.x = it->second.x;
                        node.y = it->second.y;
                    }
                }
            },
            "Arrange");
    }
    m_fitRequested = true; // next frame, once the nodes are at their new places
}

void GraphEditor::DrawPopups(NodeContext& context, AppActions& actions, const model::Graph& graph, int scene)
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
        ImGui::TextColored(kDim, "Add");
        // Each node type's items, grouped as the types ask.
        int groups = 0;
        for (const NodeUi& ui : NodeUis())
            groups = std::max(groups, ui.menuGroup + 1);
        int lastGroup = -1;
        for (int group = 0; group < groups; ++group)
        {
            for (const NodeUi& ui : NodeUis())
            {
                if (ui.menuGroup != group)
                    continue;
                for (const AddItem& item : ui.addItems)
                {
                    if (item.available != nullptr && !item.available(graph))
                        continue;
                    if (lastGroup != -1 && lastGroup != group)
                        ImGui::Separator();
                    lastGroup = group;
                    if (ImGui::MenuItem(item.label))
                    {
                        if (item.chooseFile)
                            m_choosePreset = true;
                        else
                            AddNodeAt(actions, scene, ui.type, m_addAt, item.configure != nullptr ? std::function<void(model::Node&)>(item.configure) : nullptr);
                    }
                    if (item.tooltip != nullptr && ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", item.tooltip);
                }
            }
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
                Delete(actions, scene, graph, {node->id}, {});
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
        if (target != nullptr && target->type == model::NodeType::MilkdropPreset)
        {
            const std::string id = target->id;
            auto errors = actions.EditGraph(scene, [&](model::Graph& g) { model::ParamsOf<model::MilkdropPresetParams>(*g.Find(id)).path = path; }, "Replace Milkdrop Preset");
            if (!errors.empty())
                ShowErrors(std::move(errors));
        }
        else
        {
            AddNodeAt(actions, scene, model::NodeType::MilkdropPreset, m_mouseCanvas, [&](model::Node& node) { model::ParamsOf<model::MilkdropPresetParams>(node).path = path; });
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
            id = model::AddPreset(graph, at.x, at.y, file);
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
            if (type == model::NodeType::MilkdropPreset)
                model::WireToAudioIn(graph, id); // a new preset hears the scene's audio
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
