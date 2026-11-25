#include "model/nodes/NodeDef.hpp"
#include "model/nodes/NodeJson.hpp"
#include "model/Performance.hpp"

#include <set>

namespace gv::model::nodes {

namespace {

// Show-file names of the inputs, by PresetLayer.
constexpr const char* kInputNames[] = {"feedback", "beforeComposite", "onTop", "texture"};
static_assert(std::size(kInputNames) == kPresetInputs);

constexpr const char* kFormerJsonNames[] = {"preset"};

} // namespace

/// A MilkDrop preset: video inputs into its layers (and textures), and its finished frame as output.
auto MilkdropPresetDef() -> NodeDef
{
    NodeDef def;
    def.type = NodeType::MilkdropPreset;
    def.jsonName = "milkdropPreset";
    def.formerJsonNames = kFormerJsonNames;
    def.idPrefix = "milkdrop";
    def.displayName = "Milkdrop preset";
    def.defaults = MilkdropPresetParams{};
    def.output = [](const Node&) -> std::optional<PortKind> { return PortKind::Picture; };
    def.inputCount = [](const Node&) { return kPresetInputs; };
    def.inputKind = [](const Node&, int) { return PortKind::Picture; };
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
    def.checkInShow = [](const Performance&, const Node& node, std::vector<ShowProblem>& problems) {
        if (ParamsOf<MilkdropPresetParams>(node).path.empty())
            problems.push_back({false, "no preset file chosen"});
    };
    def.write = [](const Node& node, nlohmann::json& object) {
        const auto& params = ParamsOf<MilkdropPresetParams>(node);
        object["path"] = params.path.string();
        if (params.folder)
            object["folder"] = *params.folder;
    };
    def.read = [](const nlohmann::json& object, Node& node, const std::string& path, std::vector<Issue>& issues) {
        std::string presetPath;
        ReadField(object, "path", presetPath, path, issues);
        ParamsOf<MilkdropPresetParams>(node).path = presetPath;
        // Older files have no folder: ◀ ▶ step through the preset's own.
        if (const auto it = object.find("folder"); it != object.end() && !it->is_null())
        {
            std::string folder;
            const size_t before = issues.size();
            ReadField(object, "folder", folder, path, issues);
            if (issues.size() == before)
                ParamsOf<MilkdropPresetParams>(node).folder = folder;
        }
    };
    return def;
}

} // namespace gv::model::nodes
