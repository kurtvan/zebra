#include "app/nodes/NodeUi.hpp"

#include "model/nodes/IsfShaderNode.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <mutex>

namespace gv::app::nodes {

namespace {

using isf::InputType;

auto PathOf(const model::Node& node) -> const std::filesystem::path&
{
    return model::ParamsOf<model::IsfShaderParams>(node).path;
}

auto NameOf(const isf::IsfInput& input) -> const std::string&
{
    return input.label.empty() ? input.name : input.label;
}

/// "Missing file", "can't read it" or "doesn't compile" as one line, or "" when the shader is fine (or none
/// is chosen). The dot's tooltip has the compiler's whole message.
auto FileProblem(const model::Node& node, NodeContext& context) -> std::string
{
    const auto file = model::IsfFileAt(PathOf(node));
    if (file.missing)
        return "File not found.";
    if (!file.error.empty())
        return "Can't read the shader: " + file.error;
    const std::string error = context.ShaderError(PathOf(node));
    return error.empty() ? std::string() : "Doesn't compile, so the video passes through: " + error.substr(0, error.find('\n'));
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

/// One input's control: a slider for a float, a checkbox for a bool, a choice for a long. Its strings live
/// in `strings` (and `options`), which outlive it.
auto InputControl(const isf::IsfInput& input, std::deque<std::string>& strings, std::deque<std::vector<const char*>>& options)
    -> std::optional<Control>
{
    auto keep = [&](std::string text) { return strings.emplace_back(std::move(text)).c_str(); };
    Control control;
    control.id = keep("##" + input.name);
    control.field = keep(input.name);
    control.undo = keep("Change " + NameOf(input));
    control.get = [input](const model::Node& node) {
        return static_cast<double>(model::IsfValue(model::ParamsOf<model::IsfShaderParams>(node).values, input));
    };
    control.set = [name = input.name](model::Node& node, double value) {
        model::ParamsOf<model::IsfShaderParams>(node).values[name] = {static_cast<float>(value)};
    };
    switch (input.type)
    {
        case InputType::Float:
        {
            control.kind = Control::Kind::Slider;
            control.min = input.min.empty() ? 0.0 : input.min[0];
            control.max = input.max.empty() ? 1.0 : input.max[0];
            if (control.max <= control.min)
                control.max = control.min + 1.0;
            std::string label = NameOf(input);
            for (size_t at = label.find('%'); at != std::string::npos; at = label.find('%', at + 2))
                label.insert(at, "%"); // the format's own % comes after
            control.format = keep(label + " %.2f");
            return control;
        }
        case InputType::Bool:
            control.kind = Control::Kind::Checkbox;
            control.label = keep(NameOf(input));
            control.undo = keep("Turn On " + NameOf(input));
            control.undoOff = keep("Turn Off " + NameOf(input));
            return control;
        case InputType::Long:
        {
            // Choices by index; the shader gets VALUES[index].
            std::vector<int> values = input.values;
            if (values.empty())
                return std::nullopt;
            auto& labels = options.emplace_back();
            for (size_t i = 0; i < values.size(); ++i)
                labels.push_back(keep(i < input.labels.size() ? input.labels[i] : std::to_string(values[i])));
            control.kind = Control::Kind::Choice;
            control.label = keep(NameOf(input));
            control.options = labels;
            control.get = [input, values](const model::Node& node) {
                const int value = static_cast<int>(model::IsfValue(model::ParamsOf<model::IsfShaderParams>(node).values, input));
                const auto it = std::ranges::find(values, value);
                return it == values.end() ? -1.0 : static_cast<double>(it - values.begin());
            };
            control.set = [name = input.name, values](model::Node& node, double index) {
                const size_t i = std::min(static_cast<size_t>(std::max(index, 0.0)), values.size() - 1);
                model::ParamsOf<model::IsfShaderParams>(node).values[name] = {static_cast<float>(values[i])};
            };
            return control;
        }
        default:
            return std::nullopt; // colour, point, event: D3; images are input rows; audio comes from the app
    }
}

/// The controls of `shader`'s inputs, made once per parsed file (the cache keeps their strings alive).
auto InputControls(const std::shared_ptr<const isf::IsfShader>& shader) -> std::vector<Control>
{
    struct Made
    {
        std::shared_ptr<const isf::IsfShader> shader;
        std::deque<std::string> strings;
        std::deque<std::vector<const char*>> options;
        std::vector<Control> controls;
    };
    static std::mutex mutex;
    static std::map<const isf::IsfShader*, Made> made;
    std::lock_guard<std::mutex> lock(mutex);
    auto [it, added] = made.try_emplace(shader.get());
    if (added)
    {
        it->second.shader = shader;
        for (const auto& input : shader->inputs)
        {
            if (auto control = InputControl(input, it->second.strings, it->second.options))
                it->second.controls.push_back(std::move(*control));
        }
    }
    return it->second.controls;
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
            return {NodeState::Missing, "Can't read the shader: " + file.error};
        if (const std::string error = context.ShaderError(PathOf(node)); !error.empty())
            return {NodeState::Missing, "The shader doesn't compile, so the video passes through it:\n" + error};
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
                inputs.push_back({static_cast<int>(inputs.size()), {}, model::PortKind::Video, NameOf(*image)});
        }
        for (int i = static_cast<int>(inputs.size()); i < model::InputCount(node); ++i)
            inputs.push_back({i, {}, model::PortKind::Video, "Input " + std::to_string(i + 1) + " (not in the shader)"});
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
        if (const auto file = model::IsfFileAt(PathOf(node)); file.shader != nullptr)
        {
            const auto inputs = InputControls(file.shader);
            controls.insert(controls.end(), inputs.begin(), inputs.end());
        }
        return controls;
    };
    ui.addItems = {{"ISF shader", nullptr, "A shader from an ISF file (.fs): choose the file on the node."}};
    return ui;
}

} // namespace gv::app::nodes
