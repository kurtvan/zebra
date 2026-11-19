#include "model/nodes/NodeDef.hpp"
#include "model/nodes/NodeJson.hpp"
#include "model/Performance.hpp"

#include <set>

namespace gv::model::nodes {

namespace {

// Show-file names of the inputs, by PresetLayer.
constexpr const char* kInputNames[] = {"feedback", "beforeComposite", "onTop", "texture"};
static_assert(std::size(kInputNames) == kPresetInputs);

} // namespace

/// A MilkDrop preset: video inputs into its layers (and textures), and its finished frame as output.
auto PresetDef() -> NodeDef
{
    NodeDef def;
    def.type = NodeType::Preset;
    def.jsonName = "preset";
    def.idPrefix = "preset";
    def.displayName = "Preset";
    def.defaults = PresetParams{};
    def.output = [](const Node&) -> std::optional<PortKind> { return PortKind::Frame; };
    def.inputCount = [](const Node&) { return kPresetInputs; };
    def.inputKind = [](const Node&, int) { return PortKind::Video; };
    def.textureInput = static_cast<int>(PresetLayer::Texture);
    def.inputNames = kInputNames;
    // One video source at a time: every input starts from the same source or Video switcher.
    def.checkInGraph = [](const Graph& graph, const Node& node, std::vector<GraphError>& errors) {
        std::set<std::string> starts;
        for (const auto& wire : graph.wires)
        {
            if (wire.toNode != node.id)
                continue;
            if (const Node* start = ChainStart(graph, graph.Find(wire.fromNode)); start != nullptr)
                starts.insert(start->id);
        }
        if (starts.size() > 1)
            errors.push_back({node.id, "A preset can show only one video at a time, but its inputs come from " + std::to_string(starts.size()) +
                                           " different sources."});
    };
    def.checkInShow = [](const Performance&, const Node& node, std::vector<std::string>& problems) {
        if (ParamsOf<PresetParams>(node).path.empty())
            problems.emplace_back("no preset file chosen");
    };
    def.write = [](const Node& node, nlohmann::json& object) { object["path"] = ParamsOf<PresetParams>(node).path.string(); };
    def.read = [](const nlohmann::json& object, Node& node, const std::string& path, std::vector<Issue>& issues) {
        std::string presetPath;
        ReadField(object, "path", presetPath, path, issues);
        ParamsOf<PresetParams>(node).path = presetPath;
    };
    return def;
}

} // namespace gv::model::nodes
