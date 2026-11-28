#include "model/nodes/IsfShaderNode.hpp"

#include "model/Performance.hpp"
#include "model/nodes/NodeDef.hpp"
#include "model/nodes/NodeJson.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>

namespace gv::model {

namespace {

constexpr const char* kResolutionNames[] = {"full", "half", "quarter"};

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

auto NameOf(const isf::IsfInput& input) -> const std::string&
{
    return input.label.empty() ? input.name : input.label;
}

/// A float, a bool, or a long with VALUES: the inputs that are params.
auto IsParam(const isf::IsfInput& input) -> bool
{
    return input.type == isf::InputType::Float || input.type == isf::InputType::Bool ||
           (input.type == isf::InputType::Long && !input.values.empty());
}

auto InputOf(const ParamDef& def) -> const isf::IsfInput&
{
    return *static_cast<const isf::IsfInput*>(def.data);
}

/// A param's value: a float's or bool's own, a long's choice (an index into its VALUES; -1 if none).
auto GetValue(const Node& node, const ParamDef& def) -> float
{
    if (def.data == nullptr)
        return 0.f; // lost
    const auto& input = InputOf(def);
    const float value = IsfValue(Params(node).values, input);
    if (input.type != isf::InputType::Long)
        return value;
    const auto it = std::ranges::find(input.values, static_cast<int>(std::lround(value)));
    return it == input.values.end() ? -1.f : static_cast<float>(it - input.values.begin());
}

/// Sets a param: a bool is on from 0.5, a long takes the VALUE of the nearest choice.
void SetValue(Node& node, const ParamDef& def, float value)
{
    if (def.data == nullptr)
        return;
    const auto& input = InputOf(def);
    auto& values = ParamsOf<IsfShaderParams>(node).values;
    if (input.type == isf::InputType::Bool)
        values[input.name] = {value >= 0.5f ? 1.f : 0.f};
    else if (input.type == isf::InputType::Long)
        values[input.name] = {static_cast<float>(input.values[static_cast<size_t>(
            std::clamp(static_cast<int>(std::lround(value)), 0, static_cast<int>(input.values.size()) - 1))])};
    else
        values[input.name] = {value};
}

/// The params of one header's inputs, alone and with lost params after them (by the lost inputs' names). Made
/// once per header and kept: ParamsFor's spans and the canvas's sliders point into it.
struct HeaderParams
{
    std::vector<isf::IsfInput> inputs; //!< The inputs that are params: their ParamDef::data.
    std::deque<std::string> strings;
    std::deque<std::vector<const char*>> options;
    std::vector<ParamDef> params;
    std::map<std::vector<std::string>, std::vector<ParamDef>> withLost;

    auto Keep(std::string text) -> const char*
    {
        return strings.emplace_back(std::move(text)).c_str();
    }
};

/// What makes two headers' params the same: their param inputs, written out.
auto HeaderKey(const isf::IsfShader* shader) -> std::string
{
    std::ostringstream key;
    for (const auto& input : shader != nullptr ? shader->inputs : std::vector<isf::IsfInput>{})
    {
        if (!IsParam(input))
            continue;
        key << input.name << '\x1f' << static_cast<int>(input.type) << '\x1f' << input.label;
        for (const auto* list : {&input.defaultValue, &input.min, &input.max})
        {
            key << '\x1f';
            for (float value : *list)
                key << value << ',';
        }
        key << '\x1f';
        for (int value : input.values)
            key << value << ',';
        for (const auto& label : input.labels)
            key << '\x1f' << label;
        key << '\x1e';
    }
    return key.str();
}

void MakeParams(HeaderParams& header, const isf::IsfShader* shader)
{
    if (shader == nullptr)
        return;
    for (const auto& input : shader->inputs)
    {
        if (IsParam(input))
            header.inputs.push_back(input);
    }
    for (const auto& input : header.inputs) // not resized from here on: data points into it
    {
        ParamDef def;
        def.name = header.Keep(input.name);
        def.label = header.Keep(NameOf(input));
        def.get = GetValue;
        def.set = SetValue;
        def.data = &input;
        def.undo = header.Keep("Change " + NameOf(input));
        if (input.type == isf::InputType::Float)
        {
            def.low = input.min.empty() ? 0.f : input.min[0];
            def.high = input.max.empty() ? 1.f : input.max[0];
            if (def.high <= def.low)
                def.high = def.low + 1.f;
            std::string label = NameOf(input);
            for (size_t at = label.find('%'); at != std::string::npos; at = label.find('%', at + 2))
                label.insert(at, "%"); // the format's own % comes after
            def.format = header.Keep(label + " %.2f");
        }
        else if (input.type == isf::InputType::Bool)
        {
            def.kind = ParamKind::Checkbox;
            def.undo = header.Keep("Turn On " + NameOf(input));
            def.undoOff = header.Keep("Turn Off " + NameOf(input));
        }
        else
        {
            def.kind = ParamKind::Choice;
            def.integer = true;
            def.high = static_cast<float>(input.values.size() - 1);
            auto& labels = header.options.emplace_back();
            for (size_t i = 0; i < input.values.size(); ++i)
                labels.push_back(header.Keep(i < input.labels.size() ? input.labels[i] : std::to_string(input.values[i])));
            def.options = labels;
        }
        header.params.push_back(def);
    }
}

} // namespace

auto IsfParams(const Node& node) -> std::span<const ParamDef>
{
    const auto file = IsfFileAt(Params(node).path);
    static std::mutex mutex;
    static std::map<std::string, HeaderParams> headers; // by HeaderKey: an edit of the GLSL alone adds none
    // The header each parsed file has, so the key is made once per file version. Versions no one holds go.
    static std::map<const isf::IsfShader*, std::pair<std::weak_ptr<const isf::IsfShader>, HeaderParams*>> byShader;
    std::lock_guard<std::mutex> lock(mutex);
    HeaderParams* header = nullptr;
    if (const auto it = byShader.find(file.shader.get()); it != byShader.end() && it->second.first.lock() == file.shader)
        header = it->second.second;
    else
    {
        std::erase_if(byShader, [](const auto& entry) { return entry.second.first.expired(); });
        auto [made, added] = headers.try_emplace(HeaderKey(file.shader.get()));
        if (added)
            MakeParams(made->second, file.shader.get());
        header = &made->second;
        byShader[file.shader.get()] = {file.shader, header};
    }

    std::vector<std::string> lost;
    for (const auto& name : Params(node).wiredParams)
    {
        if (std::ranges::none_of(header->params, [&](const ParamDef& def) { return name == def.name; }))
            lost.push_back(name);
    }
    if (lost.empty())
        return header->params;
    auto [it, added] = header->withLost.try_emplace(lost);
    if (added)
    {
        it->second = header->params;
        for (const auto& name : lost)
        {
            ParamDef def;
            def.name = header->Keep(name);
            def.label = def.name;
            def.get = GetValue;
            def.set = SetValue;
            def.lost = true;
            def.tooltip = header->Keep("'" + name + "' isn't in this shader any more.");
            it->second.push_back(def);
        }
    }
    return it->second;
}

auto IsfFileAt(const std::filesystem::path& path) -> IsfFile
{
    if (path.empty())
        return {};
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error)
        return {nullptr, true, {}, {}};

    struct Cached
    {
        std::filesystem::file_time_type modified;
        IsfFile file;
    };
    static std::mutex mutex;
    static std::map<std::filesystem::path, Cached> cache;
    std::lock_guard<std::mutex> lock(mutex);
    const auto cached = cache.find(path);
    if (cached != cache.end() && cached->second.modified == modified)
        return cached->second.file;

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
    if (file.shader == nullptr && cached != cache.end())
        file.shader = cached->second.file.shader; // a bad edit keeps the last version that could be read
    if (file.shader != nullptr)
        file.unsupported = isf::UnsupportedFeature(*file.shader);
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

auto IsfDivisor(IsfResolution resolution) -> int
{
    switch (resolution)
    {
        case IsfResolution::Full: return 1;
        case IsfResolution::Half: return 2;
        case IsfResolution::Quarter: return 4;
    }
    return 1;
}

auto IsfValue(const std::map<std::string, std::vector<float>>& values, const isf::IsfInput& input) -> float
{
    if (auto it = values.find(input.name); it != values.end() && !it->second.empty())
        return it->second[0];
    return input.defaultValue.empty() ? 0.f : input.defaultValue[0];
}

namespace nodes {

/// An ISF shader (a .fs file): a picture input per image input, a param (slider, checkbox or choice, each
/// with its modulation pin) per float, bool and long input.
auto IsfShaderDef() -> NodeDef
{
    NodeDef def;
    def.type = NodeType::IsfShader;
    def.jsonName = "isfShader";
    def.idPrefix = "isf";
    def.displayName = "ISF shader";
    def.defaults = IsfShaderParams{};
    def.output = [](const Node&) -> std::optional<PortKind> { return PortKind::Picture; };
    def.inputCount = [](const Node& node) {
        const auto file = IsfFileAt(Params(node).path);
        const int images = file.shader != nullptr ? static_cast<int>(IsfImageInputs(*file.shader).size()) : 0;
        return std::max(images, Params(node).wiredInputs);
    };
    def.inputKind = [](const Node&, int) { return PortKind::Picture; };
    def.followWires = [](const Graph& graph, Node& node) {
        int wired = 0;
        std::set<std::string> params;
        for (const auto& wire : graph.wires)
        {
            if (wire.toNode == node.id && wire.param.empty()) // not its modulation inputs, which come after
                wired = std::max(wired, wire.toInput + 1);
            else if (wire.toNode == node.id)
                params.insert(wire.param);
        }
        ParamsOf<IsfShaderParams>(node).wiredInputs = wired;
        ParamsOf<IsfShaderParams>(node).wiredParams.assign(params.begin(), params.end());
    };
    def.paramsFor = IsfParams;
    def.keepsLostParams = true;
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
        else if (!file.unsupported.empty())
            problems.push_back({false, "the shader can't run: " + file.unsupported});
    };
    def.write = [](const Node& node, nlohmann::json& object) {
        object["path"] = Params(node).path.string();
        object["resolution"] = kResolutionNames[static_cast<size_t>(Params(node).resolution)];
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
        std::string resolution = kResolutionNames[0]; // older shows have none: full
        ReadField(object, "resolution", resolution, path, issues);
        if (const auto* it = std::ranges::find(kResolutionNames, resolution); it != std::end(kResolutionNames))
            params.resolution = static_cast<IsfResolution>(it - std::begin(kResolutionNames));
        else
            issues.push_back(JsonError(path + "/resolution", "unknown resolution \"" + resolution + "\" (expected full, half or quarter)"));
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
