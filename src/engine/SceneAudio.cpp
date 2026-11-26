#include "engine/SceneAudio.hpp"

#include <variant>

namespace gv::engine {

void SceneAudio::Process(const model::Graph& graph, std::span<const float> stereo, int sampleRate)
{
    m_input = stereo;
    m_sampleRate = sampleRate;
    m_bandsUsed.clear();
    m_filtered.clear();
    std::map<std::string, EnvelopeFollower> followers; // only the Envelopes still there
    for (const auto& node : graph.nodes)
    {
        if (node.type != model::NodeType::Envelope || !std::holds_alternative<model::EnvelopeParams>(node.params))
            continue;
        EnvelopeFollower follower = m_followers[node.id];
        follower.SetParams(model::ParamsOf<model::EnvelopeParams>(node));
        if (const auto audio = AudioOf(graph, graph.Upstream(node.id, 0), 0))
            follower.Process(*audio, sampleRate);
        else
            follower.Reset();
        followers[node.id] = follower;
    }
    m_followers = std::move(followers);
    m_bands = std::move(m_bandsUsed); // a Band nothing follows starts from silence when it's followed again
    m_bandsUsed.clear();
}

auto SceneAudio::AudioOf(const model::Graph& graph, const model::Node* node, int depth) -> std::optional<std::span<const float>>
{
    if (node == nullptr || depth > static_cast<int>(graph.nodes.size()))
        return std::nullopt; // nothing wired in, or a loop (ValidateGraph refuses those)
    if (node->type == model::NodeType::AudioIn)
        return m_input;
    if (node->type != model::NodeType::Band || !std::holds_alternative<model::BandParams>(node->params))
        return std::nullopt;
    if (const auto it = m_filtered.find(node->id); it != m_filtered.end())
        return std::span<const float>(it->second);
    const auto upstream = AudioOf(graph, graph.Upstream(node->id, 0), depth + 1);
    if (!upstream)
        return std::nullopt;
    BandFilter& filter = m_bandsUsed[node->id] = m_bands[node->id];
    const auto& params = model::ParamsOf<model::BandParams>(*node);
    filter.SetRange(params.low, params.high, m_sampleRate);
    std::vector<float>& out = m_filtered[node->id];
    filter.Process(*upstream, out);
    return std::span<const float>(out);
}

auto SceneAudio::Levels() const -> std::map<std::string, float>
{
    std::map<std::string, float> levels;
    for (const auto& [id, follower] : m_followers)
        levels[id] = follower.Level();
    return levels;
}

} // namespace gv::engine
