#include "app/nodes/NodeUi.hpp"

#include "app/Widgets.hpp"
#include "library/PresetStepping.hpp"

#include <algorithm>

namespace gv::app::nodes {

namespace {

using model::PresetLayer;

auto PathOf(const model::Node& node) -> const std::filesystem::path&
{
    return model::ParamsOf<model::MilkdropPresetParams>(node).path;
}

auto Missing(const model::Node& node, NodeContext& context) -> bool
{
    return !PathOf(node).empty() && !context.FileExists(PathOf(node));
}

/// The live output on the node that's on screen, else the library thumbnail (D5).
void DrawPicture(const model::Node& node, NodeContext& context)
{
    const auto& path = PathOf(node);
    const bool live = context.IsLivePreset(node);
    const float width = context.width;
    const float height = width * 9.f / 16.f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, height));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 corner(at.x + width, at.y + height);
    draw->AddRectFilled(at, corner, ImGui::GetColorU32(ImVec4(0.06f, 0.06f, 0.07f, 1.f)), 4.f);
    const unsigned int livePicture = live ? context.LivePicture() : 0;
    const unsigned int texture = livePicture != 0 ? livePicture : context.Thumbnail(path, at, corner);
    if (texture != 0)
        draw->AddImageRounded(static_cast<ImTextureID>(texture), at, corner, ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, 4.f); // GL is bottom-up
    else
    {
        const char* label = path.empty() ? "no preset" : "no preview";
        const ImVec2 size = ImGui::CalcTextSize(label);
        draw->AddText(ImVec2(at.x + (width - size.x) * 0.5f, at.y + (height - size.y) * 0.5f), ImGui::GetColorU32(kDim), label);
    }
    if (live)
        draw->AddRect(at, corner, ImGui::GetColorU32(kGreen), 4.f, 0, 2.f);
    context.Tooltip(live ? "On screen now." : path.string().c_str());
}

/// ◀ ▶ through the preset's folder and Random from the whole library, skipping broken presets. Each press is
/// one undo step. ◀ ▶ need the preset to be in the library (with none chosen, they step through all of it).
void DrawStepping(const model::Node& node, NodeContext& context)
{
    const library::Library* presets = context.PresetLibrary();
    const auto& path = PathOf(node);
    const bool any = presets != nullptr && !presets->Presets().empty();
    const bool inLibrary = any && (path.empty() || library::FolderOf(*presets, path).has_value());
    auto change = [&](const char* label, const std::optional<std::filesystem::path>& to) {
        if (!to)
            return;
        const auto chosen = *to;
        context.EditOnce(node, label, [&](model::Node& n) { model::ParamsOf<model::MilkdropPresetParams>(n).path = chosen; });
    };
    ImGui::BeginDisabled(!inLibrary);
    if (ImGui::ArrowButton("##previous", ImGuiDir_Left))
        change("Previous preset", library::StepPreset(*presets, path, -1));
    context.Tooltip("The previous preset in this folder.");
    ImGui::SameLine();
    if (ImGui::ArrowButton("##next", ImGuiDir_Right))
        change("Next preset", library::StepPreset(*presets, path, 1));
    context.Tooltip("The next preset in this folder.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!any);
    if (ImGui::Button("Random"))
        change("Random preset", library::RandomPreset(*presets, path, context.Random()));
    ImGui::EndDisabled();
    context.Tooltip("A random preset from the whole library.");
}

/// Favourite (for presets in the library), Choose..., Reveal.
void DrawButtons(const model::Node& node, NodeContext& context)
{
    const auto& path = PathOf(node);
    if (const auto favourite = path.empty() ? std::nullopt : context.IsFavourite(path))
    {
        // The star, centred on the buttons' row.
        const ImVec2 row = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(row.x, row.y + (ImGui::GetFrameHeight() - 15.f) * 0.5f));
        if (StarButton("##favourite", *favourite))
            context.ToggleFavourite(path);
        context.Tooltip("Favourite");
        ImGui::SetCursorScreenPos(ImVec2(row.x + 15.f + ImGui::GetStyle().ItemSpacing.x, row.y));
    }
    if (ImGui::Button("Choose..."))
    {
        if (auto file = context.ChooseFile("Choose a preset", {"milk"}))
        {
            const auto chosen = *file;
            context.EditOnce(node, "Change Milkdrop Preset", [&](model::Node& n) { model::ParamsOf<model::MilkdropPresetParams>(n).path = chosen; });
        }
    }
    context.Tooltip("Or drop a preset from the Presets panel on this node.");
    ImGui::SameLine();
    ImGui::BeginDisabled(path.empty() || Missing(node, context));
    if (ImGui::Button("Reveal"))
        context.Reveal(path);
    ImGui::EndDisabled();
    context.Tooltip("Show the file in Finder.");
}

void DrawFrameTime(const model::Node& node, NodeContext& context)
{
    if (!context.IsLivePreset(node))
        return;
    if (const auto time = context.FrameTime(); time && time->first > 0.0)
        ImGui::TextColored(time->second ? kAmber : kDim, "%.1f ms per frame%s", time->first, time->second ? ": too heavy" : "");
}

} // namespace

auto MilkdropPresetUi() -> NodeUi
{
    NodeUi ui;
    ui.type = model::NodeType::MilkdropPreset;
    ui.width = 260.f;
    ui.header = ImVec4(0.40f, 0.26f, 0.52f, 0.60f);
    ui.menuGroup = 2;
    ui.title = [](const model::Node& node, NodeContext&) -> std::string {
        return PathOf(node).empty() ? "Milkdrop preset (none chosen)" : PathOf(node).stem().string();
    };
    ui.status = [](const model::Node& node, NodeContext& context) -> NodeStatus {
        if (PathOf(node).empty())
            return {NodeState::Empty, "No preset chosen."};
        if (Missing(node, context))
            return {NodeState::Missing, "File not found: " + PathOf(node).string()};
        if (context.IsLivePreset(node))
            return {NodeState::Live, "On screen."};
        if (context.SceneReady())
            return {NodeState::Ready, "Ready."};
        return {};
    };
    // The three layers, then one input per texture the preset samples, plus any wired texture it no longer
    // has (so the wire shows).
    ui.inputs = [](const model::Node& node, NodeContext& context) {
        std::vector<InputSpec> inputs = {
            {static_cast<int>(PresetLayer::Feedback), {}, model::PortKind::Video, "Feedback"},
            {static_cast<int>(PresetLayer::BeforeComposite), {}, model::PortKind::Video, "Before composite"},
            {static_cast<int>(PresetLayer::OnTop), {}, model::PortKind::Video, "On top"},
        };
        std::vector<std::string> names = context.TexturesOf(PathOf(node));
        for (const auto& wire : context.SceneGraph().wires)
        {
            if (wire.toNode == node.id && wire.toInput == static_cast<int>(PresetLayer::Texture) &&
                std::find(names.begin(), names.end(), wire.textureName) == names.end())
                names.push_back(wire.textureName);
        }
        for (const auto& name : names)
            inputs.push_back({static_cast<int>(PresetLayer::Texture), name, model::PortKind::Video, "Texture: " + name});
        return inputs;
    };

    Control picture;
    picture.id = "##picture";
    picture.draw = DrawPicture;
    Control stepping;
    stepping.id = "##stepping";
    stepping.draw = DrawStepping;
    Control buttons;
    buttons.id = "##buttons";
    buttons.draw = DrawButtons;
    Control missing;
    missing.kind = Control::Kind::Text;
    missing.warning = true;
    missing.text = [](const model::Node& node, NodeContext& context) -> std::string { return Missing(node, context) ? "File not found." : ""; };
    Control frameTime;
    frameTime.id = "##frame-time";
    frameTime.draw = DrawFrameTime;
    ui.controls = {picture, stepping, buttons, missing, frameTime, {Control::Kind::Inputs}};
    ui.addItems = {{"Milkdrop preset...", nullptr, "Or drag a preset from the Presets panel onto the canvas.", true}};
    return ui;
}

} // namespace gv::app::nodes
