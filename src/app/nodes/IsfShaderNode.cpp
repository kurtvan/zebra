#include "app/nodes/NodeUi.hpp"

#include "model/nodes/IsfShaderNode.hpp"

#include <algorithm>

namespace gv::app::nodes {

namespace {

auto PathOf(const model::Node& node) -> const std::filesystem::path&
{
    return model::ParamsOf<model::IsfShaderParams>(node).path;
}

auto NameOf(const isf::IsfInput& input) -> const std::string&
{
    return input.label.empty() ? input.name : input.label;
}

/// "Missing file", "can't read it", "uses a feature that can't run yet" or "doesn't compile", or "" when the
/// shader is fine (or none is chosen). Only the compiler's first line: the dot's tooltip has its whole message.
auto FileProblem(const model::Node& node, NodeContext& context) -> std::string
{
    const auto file = model::IsfFileAt(PathOf(node));
    if (file.missing)
        return "File not found.";
    if (!file.error.empty())
        return "Can't read the shader: " + file.error;
    if (!file.unsupported.empty())
        return file.unsupported;
    const std::string error = context.ShaderError(PathOf(node));
    return error.empty() ? std::string() : "Doesn't compile: " + error.substr(0, error.find('\n'));
}

/// Choose... and Reveal.
void DrawButtons(const model::Node& node, NodeContext& context)
{
    const auto& path = PathOf(node);
    if (ImGui::Button("Choose..."))
    {
        if (auto file = context.ChooseFile("Choose an ISF shader", {"fs"}))
        {
            const auto chosen = *file;
            context.EditOnce(node, "Change ISF Shader", [&](model::Node& n) { model::ParamsOf<model::IsfShaderParams>(n).path = chosen; });
        }
    }
    context.Tooltip("An ISF filter (.fs file), as made for VDMX, Resolume or isf.video.");
    ImGui::SameLine();
    ImGui::BeginDisabled(path.empty() || model::IsfFileAt(path).missing);
    if (ImGui::Button("Reveal"))
        context.Reveal(path);
    ImGui::EndDisabled();
    context.Tooltip("Show the file in Finder.");
}

constexpr const char* kResolutions[] = {"Full", "½", "¼"};

/// True if the shader draws into buffers of its own, which Reset clears.
auto HasBuffers(const model::Node& node) -> bool
{
    const auto file = model::IsfFileAt(PathOf(node));
    return file.shader != nullptr && !isf::Targets(*file.shader).empty();
}

/// Resolution, and Reset for a shader with buffers.
auto SettingControls() -> std::vector<Control>
{
    Control resolution;
    resolution.kind = Control::Kind::Choice;
    resolution.id = "##resolution";
    resolution.label = "Resolution";
    resolution.options = kResolutions;
    resolution.undo = "Change Resolution";
    resolution.tooltip = "Renders the shader smaller and scales it up: lighter on the GPU for heavy shaders.";
    resolution.get = [](const model::Node& node) {
        return static_cast<double>(model::ParamsOf<model::IsfShaderParams>(node).resolution);
    };
    resolution.set = [](model::Node& node, double value) {
        model::ParamsOf<model::IsfShaderParams>(node).resolution = static_cast<model::IsfResolution>(std::clamp(static_cast<int>(value), 0, 2));
    };
    Control restart;
    restart.id = "##restart";
    restart.visible = HasBuffers;
    restart.draw = [](const model::Node& node, NodeContext& context) {
        if (ImGui::Button("Reset"))
            context.ResetShader(node);
        context.Tooltip("Clear the shader's buffers, so a simulation or trail starts again.");
    };
    return {resolution, restart};
}

} // namespace

auto IsfShaderUi() -> NodeUi
{
    NodeUi ui;
    ui.type = model::NodeType::IsfShader;
    ui.width = 220.f;
    ui.header = ImVec4(0.18f, 0.38f, 0.42f, 0.60f);
    ui.menuGroup = 1;
    ui.title = [](const model::Node& node, NodeContext&) -> std::string {
        return PathOf(node).empty() ? "ISF shader (none chosen)" : PathOf(node).stem().string();
    };
    ui.status = [](const model::Node& node, NodeContext& context) -> NodeStatus {
        const auto file = model::IsfFileAt(PathOf(node));
        if (PathOf(node).empty())
            return {NodeState::Empty, "No shader chosen."};
        if (file.missing)
            return {NodeState::Missing, "File not found: " + PathOf(node).string()};
        if (!file.error.empty())
            return {NodeState::Missing, "Can't read the shader: " + file.error +
                                            (file.shader != nullptr ? "\nThe last version that could be read keeps running." : "")};
        if (!file.unsupported.empty())
            return {NodeState::Missing, file.unsupported + " The video passes through it."};
        if (const std::string error = context.ShaderError(PathOf(node)); !error.empty())
            return {NodeState::Missing, "The shader doesn't compile. The last version that did keeps running; without one, the "
                                        "video passes through it:\n" + error};
        if (context.Live())
            return {NodeState::Live, "The scene is live."};
        return {};
    };
    // One row per image input, then any wired input the file no longer has (so the wire shows).
    ui.inputs = [](const model::Node& node, NodeContext&) {
        std::vector<InputSpec> inputs;
        if (const auto file = model::IsfFileAt(PathOf(node)); file.shader != nullptr)
        {
            for (const auto* image : model::IsfImageInputs(*file.shader))
                inputs.push_back({static_cast<int>(inputs.size()), {}, model::PortKind::Picture, NameOf(*image)});
        }
        for (int i = static_cast<int>(inputs.size()); i < model::ParamInput(node, 0); ++i) // its modulation pins are on its params' rows
            inputs.push_back({i, {}, model::PortKind::Picture, "Input " + std::to_string(i + 1) + " (not in the shader)"});
        return inputs;
    };
    ui.controlsFor = [](const model::Node& node, NodeContext&) {
        Control buttons;
        buttons.id = "##buttons";
        buttons.draw = DrawButtons;
        Control problem;
        problem.kind = Control::Kind::Text;
        problem.warning = true;
        problem.text = FileProblem;
        std::vector<Control> controls = {{Control::Kind::Inputs}, buttons, problem};
        const auto settings = SettingControls();
        controls.insert(controls.end(), settings.begin(), settings.end());
        controls.push_back({Control::Kind::Params}); // its file's sliders, checkboxes and choices (model::IsfParams)
        return controls;
    };
    ui.addItems = {{"ISF shader", nullptr, "A shader from an ISF file (.fs): choose the file on the node."}};
    return ui;
}

} // namespace gv::app::nodes
