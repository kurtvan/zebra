#include "model/nodes/IsfShaderNode.hpp"

#include "model/Performance.hpp"
#include "model/nodes/NodeDef.hpp"
#include "model/nodes/NodeJson.hpp"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>

namespace gv::model {

namespace {

/// "0.5", "32": a number as the artist would write it.
auto Number(float value) -> std::string
{
    std::ostringstream out;
    out << value;
    return out.str();
}

auto Params(const Node& node) -> const IsfShaderParams&
{
    return ParamsOf<IsfShaderParams>(node);
}

} // namespace

auto IsfFileAt(const std::filesystem::path& path) -> IsfFile
{
    if (path.empty())
        return {};
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error)
        return {nullptr, true, {}};

    struct Cached
    {
        std::filesystem::file_time_type modified;
        IsfFile file;
    };
    static std::mutex mutex;
    static std::map<std::filesystem::path, Cached> cache;
    std::lock_guard<std::mutex> lock(mutex);
    if (auto it = cache.find(path); it != cache.end() && it->second.modified == modified)
        return it->second.file;

    IsfFile file;
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    if (in.is_open())
        text << in.rdbuf();
    if (!in.is_open())
        file.error = "the file can't be opened";
    else if (auto parsed = isf::ParseIsf(text.str()); auto* shader = std::get_if<isf::IsfShader>(&parsed))
        file.shader = std::make_shared<const isf::IsfShader>(std::move(*shader));
    else
        file.error = "line " + std::to_string(std::get<isf::IsfError>(parsed).line) + ": " + std::get<isf::IsfError>(parsed).message;
    cache[path] = {modified, file};
    return file;
}

auto IsfImageInputs(const isf::IsfShader& shader) -> std::vector<const isf::IsfInput*>
{
    std::vector<const isf::IsfInput*> images;
    for (const auto& input : shader.inputs)
    {
        if (input.type == isf::InputType::Image)
            images.push_back(&input);
    }
    return images;
}

auto IsfValue(const std::map<std::string, std::vector<float>>& values, const isf::IsfInput& input) -> float
{
    if (auto it = values.find(input.name); it != values.end() && !it->second.empty())
        return it->second[0];
    return input.defaultValue.empty() ? 0.f : input.defaultValue[0];
}

namespace nodes {

/// An ISF shader (a .fs file): a Video input per image input, a control per float, bool and long input.
auto IsfShaderDef() -> NodeDef
{
    NodeDef def;
    def.type = NodeType::IsfShader;
    def.jsonName = "isfShader";
    def.idPrefix = "isf";
    def.displayName = "ISF shader";
    def.defaults = IsfShaderParams{};
    def.output = [](const Node&) -> std::optional<PortKind> { return PortKind::Video; };
    def.inputCount = [](const Node& node) {
        const auto file = IsfFileAt(Params(node).path);
        const int images = file.shader != nullptr ? static_cast<int>(IsfImageInputs(*file.shader).size()) : 0;
        return std::max(images, Params(node).wiredInputs);
    };
    def.inputKind = [](const Node&, int) { return PortKind::Video; };
    def.followWires = [](const Graph& graph, Node& node) {
        int wired = 0;
        for (const auto& wire : graph.wires)
        {
            if (wire.toNode == node.id)
                wired = std::max(wired, wire.toInput + 1);
        }
        ParamsOf<IsfShaderParams>(node).wiredInputs = wired;
    };
    // Values the file's MIN and MAX (or a long's VALUES) allow. Values of inputs it doesn't have are ignored.
    def.checkParams = [](const Node& node, std::vector<GraphError>& errors) {
        const auto file = IsfFileAt(Params(node).path);
        if (file.shader == nullptr)
            return;
        for (const auto& input : file.shader->inputs)
        {
            if (!Params(node).values.contains(input.name))
                continue;
            const float value = IsfValue(Params(node).values, input);
            const std::string name = input.label.empty() ? input.name : input.label;
            if (input.type == isf::InputType::Float && !input.min.empty() && !input.max.empty())
            {
                const std::string range = Number(input.min[0]) + " and " + Number(input.max[0]);
                CheckRange(errors, node, name.c_str(), value, input.min[0], input.max[0], range.c_str());
            }
            else if (input.type == isf::InputType::Long && !input.values.empty() &&
                     std::ranges::find(input.values, static_cast<int>(value)) == input.values.end())
                errors.push_back({node.id, Label(node) + ": " + name + " can't be " + Number(value) + "."});
        }
    };
    def.checkInShow = [](const Performance&, const Node& node, std::vector<ShowProblem>& problems) {
        const auto file = IsfFileAt(Params(node).path);
        if (Params(node).path.empty())
            problems.push_back({false, "no shader file chosen"});
        else if (file.missing)
            problems.push_back({false, "shader file not found: " + Params(node).path.string()});
        else if (!file.error.empty())
            problems.push_back({false, "the shader file can't be read: " + file.error});
    };
    def.write = [](const Node& node, nlohmann::json& object) {
        object["path"] = Params(node).path.string();
        nlohmann::json values = nlohmann::json::object();
        for (const auto& [name, value] : Params(node).values)
            values[name] = value.size() == 1 ? nlohmann::json(value[0]) : nlohmann::json(value);
        object["values"] = std::move(values);
    };
    def.read = [](const nlohmann::json& object, Node& node, const std::string& path, std::vector<Issue>& issues) {
        auto& params = ParamsOf<IsfShaderParams>(node);
        std::string shaderPath;
        ReadField(object, "path", shaderPath, path, issues);
        params.path = shaderPath;
        const auto values = object.find("values");
        if (values == object.end() || values->is_null())
            return;
        if (!values->is_object())
        {
            issues.push_back(JsonError(path + "/values", "should be an object of values by input name"));
            return;
        }
        for (const auto& [name, value] : values->items())
        {
            if (value.is_number() || value.is_boolean())
                params.values[name] = {value.is_boolean() ? (value.get<bool>() ? 1.f : 0.f) : value.get<float>()};
            else if (value.is_array() && std::ranges::all_of(value, [](const nlohmann::json& n) { return n.is_number(); }))
                params.values[name] = value.get<std::vector<float>>();
            else
                issues.push_back(JsonError(path + "/values/" + name, "should be a number or a list of numbers"));
        }
    };
    return def;
}

} // namespace nodes

} // namespace gv::model
