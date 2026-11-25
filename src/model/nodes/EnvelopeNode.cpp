#include "model/nodes/NodeDef.hpp"
#include "model/nodes/NodeJson.hpp"

namespace gv::model::nodes {

/// How loud the audio wired in is, as a number from 0 to 1 that rises and falls with it. The engine measures
/// it (engine/EnvelopeFollower) and hands it over in SignalInputs::levels.
auto EnvelopeDef() -> NodeDef
{
    NodeDef def;
    def.type = NodeType::Envelope;
    def.jsonName = "envelope";
    def.idPrefix = "envelope";
    def.displayName = "Envelope";
    def.defaults = EnvelopeParams{};
    def.output = [](const Node&) -> std::optional<PortKind> { return PortKind::Number; };
    def.inputCount = [](const Node&) { return 1; };
    def.inputKind = [](const Node&, int) { return PortKind::Audio; };
    def.checkParams = [](const Node& node, std::vector<GraphError>& errors) {
        const auto& params = ParamsOf<EnvelopeParams>(node);
        CheckRange(errors, node, "attack", params.attack, 0.001, 1.0, "0.001 and 1 second");
        CheckRange(errors, node, "release", params.release, 0.01, 5.0, "0.01 and 5 seconds");
        CheckRange(errors, node, "gain", params.gain, 0.1, 20.0, "0.1 and 20");
        CheckRange(errors, node, "threshold", params.threshold, 0.0, 0.99, "0 and 0.99");
    };
    def.signal = [](const Node& node, const SignalInputs& inputs) {
        const auto it = inputs.levels.find(node.id);
        return it != inputs.levels.end() ? it->second : 0.0f;
    };
    def.write = [](const Node& node, nlohmann::json& object) {
        const auto& params = ParamsOf<EnvelopeParams>(node);
        object["attack"] = params.attack;
        object["release"] = params.release;
        object["gain"] = params.gain;
        object["threshold"] = params.threshold;
        object["autoLevel"] = params.autoLevel;
    };
    def.read = [](const nlohmann::json& object, Node& node, const std::string& path, std::vector<Issue>& issues) {
        auto& params = ParamsOf<EnvelopeParams>(node);
        ReadField(object, "attack", params.attack, path, issues);
        ReadField(object, "release", params.release, path, issues);
        ReadField(object, "gain", params.gain, path, issues);
        ReadField(object, "threshold", params.threshold, path, issues);
        ReadField(object, "autoLevel", params.autoLevel, path, issues);
    };
    return def;
}

} // namespace gv::model::nodes
