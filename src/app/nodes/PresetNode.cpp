#include "app/nodes/NodeDraw.hpp"

#include "app/Shell.hpp"
#include "app/Widgets.hpp"
#include "app/macos/FileDialog.hpp"

#include <algorithm>

namespace gv::app {

namespace {

auto PathOf(const model::Node& node) -> const std::filesystem::path&
{
    return model::ParamsOf<model::PresetParams>(node).path;
}

auto Missing(const model::Node& node) -> bool
{
    std::error_code ec;
    return !PathOf(node).empty() && !std::filesystem::is_regular_file(PathOf(node), ec);
}

} // namespace

auto PresetTitle(const model::Node& node) -> std::string
{
    return PathOf(node).empty() ? "Preset (none chosen)" : PathOf(node).stem().string();
}

auto PresetStatus(const model::Node& node, NodeContext& context) -> NodeStatus
{
    if (PathOf(node).empty())
        return {kDim, "No preset chosen."};
    if (Missing(node))
        return {kRed, "File not found: " + PathOf(node).string()};
    if (context.IsLivePreset(node))
        return {kGreen, "On screen."};
    if (context.state.engine != nullptr && context.state.engine->Scene(context.scene).state == engine::ScenePool::State::Ready)
        return {ImVec4(0.85f, 0.86f, 0.9f, 1.f), "Ready."};
    return {ImVec4(0.35f, 0.36f, 0.40f, 1.f), {}};
}

auto PresetInputs(const model::Node& node, NodeContext& context) -> std::vector<InputSpec>
{
    using model::PresetLayer;
    std::vector<InputSpec> inputs = {
        {static_cast<int>(PresetLayer::Feedback), {}, model::PortKind::Video, "Feedback"},
        {static_cast<int>(PresetLayer::BeforeComposite), {}, model::PortKind::Video, "Before composite"},
        {static_cast<int>(PresetLayer::OnTop), {}, model::PortKind::Video, "On top"},
    };
    // One input per texture the preset samples, plus any wired texture it no longer has (so the wire shows).
    std::vector<std::string> names = context.TexturesOf(PathOf(node));
    for (const auto& wire : context.sceneData.graph.wires)
    {
        if (wire.toNode == node.id && wire.toInput == static_cast<int>(PresetLayer::Texture) &&
            std::find(names.begin(), names.end(), wire.textureName) == names.end())
            names.push_back(wire.textureName);
    }
    for (const auto& name : names)
        inputs.push_back({static_cast<int>(PresetLayer::Texture), name, model::PortKind::Video, "Texture: " + name});
    return inputs;
}

void DrawPresetBody(const model::Node& node, NodeContext& context)
{
    const auto& path = PathOf(node);
    const bool live = context.IsLivePreset(node);

    // The live output on the node that's on screen, else the library thumbnail (D5).
    const float width = context.width;
    const float height = width * 9.f / 16.f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, height));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 corner(at.x + width, at.y + height);
    draw->AddRectFilled(at, corner, ImGui::GetColorU32(ImVec4(0.06f, 0.06f, 0.07f, 1.f)), 4.f);
    const unsigned int texture = live && context.state.preview != 0 ? context.state.preview : context.Thumbnail(path);
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

    // Favourite, choose, reveal.
    if (context.state.library != nullptr && context.state.favourites != nullptr && !path.empty())
    {
        if (const std::string id = context.state.library->IdForPath(path); !id.empty() && context.state.library->Find(id) != nullptr)
        {
            // The star, centred on the buttons' row.
            const ImVec2 row = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(row.x, row.y + (ImGui::GetFrameHeight() - 15.f) * 0.5f));
            if (StarButton("##favourite", context.state.favourites->Contains(id)))
                context.actions.ToggleFavourite(id);
            context.Tooltip("Favourite");
            ImGui::SetCursorScreenPos(ImVec2(row.x + 15.f + ImGui::GetStyle().ItemSpacing.x, row.y));
        }
    }
    if (ImGui::Button("Choose..."))
    {
        if (auto file = ChooseFile("Choose a preset", {"milk"}))
        {
            const auto chosen = *file;
            context.EditOnce(node, "Change Preset", [&](model::Node& n) { model::ParamsOf<model::PresetParams>(n).path = chosen; });
        }
    }
    context.Tooltip("Or drop a preset from the Presets panel on this node.");
    ImGui::SameLine();
    ImGui::BeginDisabled(path.empty() || Missing(node));
    if (ImGui::Button("Reveal"))
        RevealInFinder(path);
    ImGui::EndDisabled();
    if (Missing(node))
        ImGui::TextColored(kRed, "File not found.");
    if (live && context.state.engine != nullptr)
    {
        const auto info = context.state.engine->Scene(context.scene);
        if (info.frameMs > 0.0)
            ImGui::TextColored(info.heavy ? kAmber : kDim, "%.1f ms per frame%s", info.frameMs, info.heavy ? ": too heavy" : "");
    }

    for (const auto& spec : PresetInputs(node, context))
        context.InputRow(node, spec);
}

} // namespace gv::app
