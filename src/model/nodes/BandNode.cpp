#include "model/nodes/BandNode.hpp"

#include "model/nodes/NodeDef.hpp"
#include "model/nodes/NodeJson.hpp"

#include <cmath>
#include <cstdio>

namespace gv::model {

auto BandPresetOf(const BandParams& params) -> int
{
    for (size_t i = 0; i < kBandPresets.size(); ++i)
    {
        if (std::abs(params.low - kBandPresets[i].low) < 0.5 && std::abs(params.high - kBandPresets[i].high) < 0.5)
            return static_cast<int>(i);
    }
    return -1;
}

auto BandRangeText(const BandParams& params) -> std::string
{
    char text[48];
    std::snprintf(text, sizeof(text), "%.0f-%.0f Hz", params.low, params.high);
    return text;
}

namespace nodes {

/// Lets one frequency range of the audio wired in through (the bass, say), for an Envelope to follow. The
/// engine filters it (engine/BandFilter); a Band after a Band narrows it further.
auto BandDef() -> NodeDef
{
    NodeDef def;
    def.type = NodeType::Band;
    def.jsonName = "band";
    def.idPrefix = "band";
    def.displayName = "Band";
    def.defaults = BandParams{};
    def.output = [](const Node&) -> std::optional<PortKind> { return PortKind::Audio; };
    def.inputCount = [](const Node&) { return 1; };
    def.inputKind = [](const Node&, int) { return PortKind::Audio; };
    def.checkParams = [](const Node& node, std::vector<GraphError>& errors) {
        const auto& params = ParamsOf<BandParams>(node);
        CheckRange(errors, node, "the low end", params.low, kBandMinHz, kBandMaxHz, "20 and 20000 Hz");
        CheckRange(errors, node, "the high end", params.high, kBandMinHz, kBandMaxHz, "20 and 20000 Hz");
        if (params.low > params.high)
            errors.push_back({node.id, Label(node) + ": the low end must be below the high end."});
    };
    def.write = [](const Node& node, nlohmann::json& object) {
        const auto& params = ParamsOf<BandParams>(node);
        object["low"] = params.low;
        object["high"] = params.high;
    };
    def.read = [](const nlohmann::json& object, Node& node, const std::string& path, std::vector<Issue>& issues) {
        auto& params = ParamsOf<BandParams>(node);
        ReadField(object, "low", params.low, path, issues);
        ReadField(object, "high", params.high, path, issues);
    };
    return def;
}

} // namespace nodes

} // namespace gv::model
