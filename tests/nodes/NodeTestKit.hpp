#pragma once

// What every node test uses: a scene with one node of the type under test, a fake NodeContext that applies
// and records edits, and a headless ImGui frame to draw the node's body in (and click it).

#include "app/nodes/NodeUi.hpp"
#include "library/Library.hpp"
#include "model/Graph.hpp"
#include "model/Performance.hpp"
#include "model/PerformanceJson.hpp"
#include "model/nodes/NodeDef.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace gv::test {

/// A performance with one scene whose graph holds a viewport and the nodes the test adds.
class NodeScene
{
public:
    NodeScene()
    {
        performance.scenes.emplace_back();
        performance.scenes[0].id = "scene";
        model::AddNode(Graph(), model::NodeType::Viewport, 0.f, 0.f);
    }

    auto Graph() -> model::Graph&
    {
        return performance.scenes[0].graph;
    }

    /// Adds a node of `type` with its default settings; returns its id.
    auto Add(model::NodeType type, const std::function<void(model::Node&)>& configure = {}) -> std::string
    {
        const std::string id = model::AddNode(Graph(), type, 0.f, 0.f);
        if (configure)
            configure(*Graph().Find(id));
        return id;
    }

    auto Node(const std::string& id) -> model::Node&
    {
        model::Node* node = Graph().Find(id);
        REQUIRE(node != nullptr);
        return *node;
    }

    model::Performance performance;
};

/// One edit a node made.
struct RecordedEdit
{
    std::string label;
    std::string coalesce; //!< The field, for coalesced (slider) edits; empty for one-step edits.
};

/// NodeContext for tests: everything about the app is a field the test sets, and edits apply to `scene`'s
/// graph (refused, as the canvas would, if they make it invalid).
class FakeNodeContext final : public app::NodeContext
{
public:
    explicit FakeNodeContext(NodeScene& nodeScene)
        : scene(nodeScene)
    {
        width = 200.f;
    }

    NodeScene& scene;

    // What the test says the app looks like.
    bool live{false};
    std::string livePreset;
    std::string liveVideo;
    bool ready{false};
    std::optional<std::pair<bool, std::string>> videoState;
    std::map<std::string, engine::SwitcherState> switchers;    //!< Running switchers, by node id.
    std::set<int> doubleClickedInputs;                    //!< Input rows whose label is double-clicked.
    std::optional<std::pair<double, bool>> frameTime;
    std::set<std::string> files;                          //!< Paths that exist.
    std::map<std::string, std::vector<std::string>> textures; //!< By preset path.
    std::map<std::string, std::string> shaderErrors;          //!< Compiler messages, by ISF shader path.
    std::map<std::string, bool> favourites;               //!< Library presets, by path.
    std::optional<std::filesystem::path> chosenFile;      //!< What the file dialog returns.
    const library::Library* presetLibrary{nullptr};
    std::mt19937 random{1};                               //!< Seeded: the same picks every run.
    std::vector<app::SyphonServer> servers;
    float wheel{0.f};                                     //!< The scroll over any area under the pointer.
    std::map<std::string, app::PresetBrowserState> browsers; //!< By node id.
    model::SignalValues signals;                          //!< Number outputs' values this frame.
    model::SignalInputs signalInputs;                     //!< The clock and time they follow.

    // What the node did.
    std::vector<RecordedEdit> edits;
    std::vector<std::vector<model::GraphError>> refused;
    int endedEdits{0};
    std::vector<app::InputSpec> inputRows;
    std::vector<app::InputSpec> paramPins;                //!< Modulation pins, on their sliders' rows.
    std::vector<std::pair<std::string, int>> jumps;       //!< JumpSwitcher calls: node id, input.
    std::vector<std::string> resets;                      //!< ResetShader calls: node id.
    std::vector<int> popups;
    std::vector<std::string> tooltips;
    std::vector<std::string> fileDialogs;
    std::vector<std::filesystem::path> revealed;
    std::vector<std::string> toggledFavourites;
    std::map<std::string, std::pair<ImVec2, ImVec2>> itemRects; //!< The item before each tooltip, by tooltip.
    std::vector<std::pair<ImVec2, ImVec2>> thumbnailRects;      //!< Where each asked-for thumbnail is drawn.

    auto Performance() const -> const model::Performance& override
    {
        return scene.performance;
    }
    auto SceneGraph() const -> const model::Graph& override
    {
        return scene.performance.scenes[0].graph;
    }
    auto Live() const -> bool override
    {
        return live;
    }
    auto IsLivePreset(const model::Node& node) const -> bool override
    {
        return node.id == livePreset;
    }
    auto IsLiveVideo(const model::Node& node) const -> bool override
    {
        return node.id == liveVideo;
    }
    auto Switcher(const model::Node& node) const -> std::optional<engine::SwitcherState> override
    {
        if (auto it = switchers.find(node.id); it != switchers.end())
            return it->second;
        return std::nullopt;
    }
    void JumpSwitcher(const model::Node& node, int input) override
    {
        jumps.emplace_back(node.id, input);
    }
    void ResetShader(const model::Node& node) override
    {
        resets.push_back(node.id);
    }
    auto SceneReady() const -> bool override
    {
        return ready;
    }
    auto VideoState() const -> std::optional<std::pair<bool, std::string>> override
    {
        return videoState;
    }
    auto FrameTime() const -> std::optional<std::pair<double, bool>> override
    {
        return frameTime;
    }
    auto FileExists(const std::filesystem::path& path) const -> bool override
    {
        return files.contains(path.string());
    }
    auto ShaderError(const std::filesystem::path& shader) const -> std::string override
    {
        const auto it = shaderErrors.find(shader.string());
        return it != shaderErrors.end() ? it->second : std::string();
    }
    auto TexturesOf(const std::filesystem::path& preset) -> const std::vector<std::string>& override
    {
        return textures[preset.string()];
    }
    auto Signals() const -> model::SignalValues override
    {
        return signals;
    }
    auto SignalInputs() const -> model::SignalInputs override
    {
        return signalInputs;
    }
    void ParamPin(const model::Node&, const app::InputSpec& spec) override
    {
        paramPins.push_back(spec);
        if (ImGui::GetCurrentContext() != nullptr)
            ImGui::Dummy(ImVec2(12.f, ImGui::GetFrameHeight()));
    }
    auto LivePicture() -> unsigned int override
    {
        return 7;
    }
    auto Thumbnail(const std::filesystem::path&, ImVec2 min, ImVec2 max) -> unsigned int override
    {
        thumbnailRects.emplace_back(min, max);
        return 0;
    }
    auto IsFavourite(const std::filesystem::path& preset) const -> std::optional<bool> override
    {
        auto it = favourites.find(preset.string());
        return it != favourites.end() ? std::optional<bool>(it->second) : std::nullopt;
    }
    void ToggleFavourite(const std::filesystem::path& preset) override
    {
        toggledFavourites.push_back(preset.string());
    }
    auto PresetLibrary() const -> const library::Library* override
    {
        return presetLibrary;
    }
    auto Random() -> std::mt19937& override
    {
        return random;
    }

    void Edit(const model::Node& node, const char* label, const char* field, const std::function<void(model::Node&)>& change) override
    {
        Apply(node, label, field, change);
    }
    void EditOnce(const model::Node& node, const char* label, const std::function<void(model::Node&)>& change) override
    {
        Apply(node, label, "", change);
    }
    void EndEditIfReleased() override
    {
        RememberItem("(slider)");
        if (ImGui::GetCurrentContext() != nullptr && ImGui::IsItemDeactivatedAfterEdit())
            ++endedEdits;
    }
    void EditPerformance(const std::function<void(model::Performance&, model::Graph&)>& change, const char* label) override
    {
        auto edited = scene.performance;
        change(edited, edited.scenes[0].graph);
        scene.performance = std::move(edited);
        edits.push_back({label, {}});
    }

    auto ChooseFile(const char* title, const std::vector<std::string>&) -> std::optional<std::filesystem::path> override
    {
        fileDialogs.emplace_back(title);
        return chosenFile;
    }
    void Reveal(const std::filesystem::path& path) override
    {
        revealed.push_back(path);
    }
    auto SyphonServers() -> std::vector<app::SyphonServer> override
    {
        return servers;
    }

    auto InputRow(const model::Node&, const app::InputSpec& spec) -> bool override
    {
        inputRows.push_back(spec);
        if (ImGui::GetCurrentContext() != nullptr)
            ImGui::TextUnformatted(spec.label.c_str());
        return doubleClickedInputs.count(spec.input) > 0;
    }
    void OpenPopup(const model::Node&, int kind) override
    {
        popups.push_back(kind);
    }
    void Tooltip(const char* text) override
    {
        tooltips.emplace_back(text);
        RememberItem(text);
    }
    auto Wheel(ImVec2 min, ImVec2 max) -> float override
    {
        return ImGui::IsMouseHoveringRect(min, max, false) ? wheel : 0.f;
    }
    auto PresetBrowser(const model::Node& node) -> app::PresetBrowserState& override
    {
        return browsers[node.id];
    }

    /// The last edit's label, or "" if there was none.
    auto LastEdit() const -> std::string
    {
        return edits.empty() ? std::string() : edits.back().label;
    }

private:
    void Apply(const model::Node& node, const char* label, const char* field, const std::function<void(model::Node&)>& change)
    {
        model::Graph edited = scene.Graph();
        if (model::Node* target = edited.Find(node.id))
            change(*target);
        if (auto errors = model::ValidateGraph(edited); !errors.empty())
        {
            refused.push_back(std::move(errors));
            return;
        }
        scene.Graph() = std::move(edited);
        edits.push_back({label, field});
    }

    void RememberItem(const std::string& key)
    {
        if (ImGui::GetCurrentContext() != nullptr)
            itemRects[key] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
    }
};

/// A headless ImGui: no window or GPU, but real widgets, layout and mouse input.
class HeadlessUi
{
public:
    HeadlessUi()
    {
        m_context = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(800.f, 600.f);
        io.IniFilename = nullptr;
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~HeadlessUi()
    {
        ImGui::DestroyContext(m_context);
    }
    HeadlessUi(const HeadlessUi&) = delete;
    auto operator=(const HeadlessUi&) -> HeadlessUi& = delete;

    /// One frame with `node`'s body in a window, the way the canvas lays it out; then its popup `popup`, if any.
    void Frame(FakeNodeContext& context, const std::string& node, std::optional<int> popup = std::nullopt)
    {
        ImGui::GetIO().DeltaTime = 1.f / 60.f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
        ImGui::SetNextWindowSize(ImVec2(400.f, 600.f));
        ImGui::Begin("node", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        // Copied: an edit replaces the graph, and the node with it.
        const model::Node copy = context.scene.Node(node);
        ImGui::PushItemWidth(context.width);
        app::DrawNodeBody(copy, context);
        ImGui::PopItemWidth();
        if (popup)
        {
            ImGui::OpenPopup("list");
            if (ImGui::BeginPopup("list"))
            {
                app::DrawNodePopup(copy, *popup, context);
                ImGui::EndPopup();
            }
        }
        ImGui::End();
        ImGui::Render();
    }

    /// Clicks at `at` (press, then release a frame later), drawing `node` each frame.
    void Click(FakeNodeContext& context, const std::string& node, ImVec2 at, std::optional<int> popup = std::nullopt)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(at.x, at.y);
        Frame(context, node, popup);
        io.AddMouseButtonEvent(0, true);
        Frame(context, node, popup);
        io.AddMouseButtonEvent(0, false);
        Frame(context, node, popup);
        if (popup)
            Frame(context, node, popup); // a choice closes the list; the next frame opens it again
    }

    /// The centre of row `row` (from 0) of the open popup's list of selectables.
    static auto PopupRow(int row) -> ImVec2
    {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        REQUIRE_FALSE(g.OpenPopupStack.empty());
        const ImGuiWindow* window = g.OpenPopupStack.back().Window;
        REQUIRE(window != nullptr);
        const float line = ImGui::GetTextLineHeight();
        const float step = line + g.Style.ItemSpacing.y;
        return ImVec2(window->DC.CursorStartPos.x + 10.f, window->DC.CursorStartPos.y + static_cast<float>(row) * step + line * 0.5f);
    }

    /// The centre of the open popup's last row.
    static auto PopupLastRow() -> ImVec2
    {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        REQUIRE_FALSE(g.OpenPopupStack.empty());
        const ImGuiWindow* window = g.OpenPopupStack.back().Window;
        REQUIRE(window != nullptr);
        return ImVec2(window->DC.CursorStartPos.x + 10.f, window->DC.CursorMaxPos.y - ImGui::GetTextLineHeight() * 0.5f);
    }

    /// The centre of the item drawn just before tooltip `tooltip` (or "(slider)": the last slider).
    static auto Centre(const FakeNodeContext& context, const std::string& tooltip) -> ImVec2
    {
        auto it = context.itemRects.find(tooltip);
        REQUIRE(it != context.itemRects.end());
        return ImVec2((it->second.first.x + it->second.second.x) * 0.5f, (it->second.first.y + it->second.second.y) * 0.5f);
    }

private:
    ImGuiContext* m_context{nullptr};
};

/// Every control of `type` whose kind is `kind`, in order.
inline auto ControlsOf(model::NodeType type, app::Control::Kind kind) -> std::vector<const app::Control*>
{
    std::vector<const app::Control*> found;
    for (const auto& control : app::Ui(type).controls)
    {
        if (control.kind == kind)
            found.push_back(&control);
    }
    return found;
}

/// The sliders a `type` node with its default settings draws for its params (app::ParamSlider), in order.
inline auto ParamSliders(model::NodeType type) -> std::vector<app::Control>
{
    model::Node node;
    node.type = type;
    node.params = model::Def(type).defaults;
    std::vector<app::Control> sliders;
    for (int param = 0; param < static_cast<int>(model::Def(type).params.size()); ++param)
        sliders.push_back(app::ParamSlider(node, param));
    return sliders;
}

/// The control of `type` with ImGui id `id`.
inline auto ControlById(model::NodeType type, const std::string& id) -> const app::Control&
{
    for (const auto& control : app::Ui(type).controls)
    {
        if (id == control.id)
            return control;
    }
    FAIL("no control " << id);
    return app::Ui(type).controls.front();
}

/// The index of `control` in its node type's controls (what OpenPopup is given).
inline auto IndexOf(model::NodeType type, const app::Control& control) -> int
{
    return static_cast<int>(&control - app::Ui(type).controls.data());
}

/// `node` saved and loaded again through its definition.
inline auto RoundTrip(const model::Node& node) -> model::Node
{
    const model::NodeDef& def = model::Def(node.type);
    nlohmann::json object = nlohmann::json::object();
    if (def.write != nullptr)
        def.write(node, object);
    model::Node loaded;
    loaded.id = node.id;
    loaded.type = node.type;
    loaded.x = node.x;
    loaded.y = node.y;
    loaded.expanded = node.expanded;
    loaded.params = def.defaults;
    std::vector<model::Issue> issues;
    if (def.read != nullptr)
        def.read(object, loaded, "/node", issues);
    CHECK(issues.empty());
    return loaded;
}

/// `type`'s settings read from `json` (on top of its defaults), and the problems reported.
inline auto ReadNode(model::NodeType type, const nlohmann::json& object) -> std::pair<model::Node, std::vector<model::Issue>>
{
    const model::NodeDef& def = model::Def(type);
    model::Node node;
    node.id = "node";
    node.type = type;
    node.params = def.defaults;
    std::vector<model::Issue> issues;
    if (def.read != nullptr)
        def.read(object, node, "/node", issues);
    return {node, issues};
}

/// The messages of `errors`.
inline auto Messages(const std::vector<model::GraphError>& errors) -> std::vector<std::string>
{
    std::vector<std::string> messages;
    for (const auto& error : errors)
        messages.push_back(error.message);
    return messages;
}

/// The errors of a lone node with params changed by `change` (just the node's own checks).
inline auto ParamErrors(model::NodeType type, const std::function<void(model::Node&)>& change) -> std::vector<std::string>
{
    model::Node node;
    node.id = "node";
    node.type = type;
    node.params = model::Def(type).defaults;
    change(node);
    std::vector<model::GraphError> errors;
    if (model::Def(type).checkParams != nullptr)
        model::Def(type).checkParams(node, errors);
    return Messages(errors);
}

} // namespace gv::test
