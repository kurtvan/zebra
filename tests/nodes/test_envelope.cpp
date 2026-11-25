// The Envelope node: model/nodes/EnvelopeNode.cpp, app/nodes/EnvelopeNode.cpp. Its analysis:
// tests/engine/test_envelope_follower.cpp. Wiring rules: tests/model/test_graph.cpp.

#include "nodes/NodeTestKit.hpp"

using namespace gv;
using model::EnvelopeParams;
using model::NodeType;

namespace {

const char* const kGainTip = "How much the music's level is raised: turn it up until the loudest parts reach 1.";
const char* const kAutoLevelTip =
    "The loudest of the last few seconds is 1, so quiet and loud passages both use the whole range. Off: the gain sets it.";
const char* const kLevelTip = "How loud the audio is now, from 0 to 1, over the last two seconds. Wire it into a setting's diamond to move that setting.";

} // namespace

TEST_CASE("envelope: one Audio input, a Number output; auto-level on by default", "[node:envelope]")
{
    test::NodeScene scene;
    const auto& node = scene.Node(scene.Add(NodeType::Envelope));
    CHECK(node.id == "envelope-1");
    CHECK(model::OutputKind(node) == model::PortKind::Number);
    CHECK(model::InputCount(node) == 1);
    CHECK(model::InputKind(node, 0) == model::PortKind::Audio);
    CHECK(model::ParamsOf<EnvelopeParams>(node) == EnvelopeParams{0.01, 0.25, 1.0, 0.0, true});
}

TEST_CASE("envelope: its value is the level the engine measured for it, 0 before any", "[node:envelope]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::Envelope);
    const std::string other = scene.Add(NodeType::Envelope);
    model::SignalInputs inputs;
    CHECK(model::EvaluateSignals(scene.Graph(), inputs).at(id) == 0.f);
    inputs.levels[id] = 0.7f;
    const auto values = model::EvaluateSignals(scene.Graph(), inputs);
    CHECK(values.at(id) == 0.7f);
    CHECK(values.at(other) == 0.f);
}

TEST_CASE("envelope: its settings' ranges", "[node:envelope]")
{
    auto set = [](EnvelopeParams params) { return [=](model::Node& node) { model::ParamsOf<EnvelopeParams>(node) = params; }; };
    CHECK(test::ParamErrors(NodeType::Envelope, set({0.001, 5.0, 20.0, 0.99, false})).empty());
    CHECK(test::ParamErrors(NodeType::Envelope, set({0.0, 0.0, 0.0, 1.0, true})) ==
          std::vector<std::string>{"Envelope: attack must be between 0.001 and 1 second.", "Envelope: release must be between 0.01 and 5 seconds.",
                                   "Envelope: gain must be between 0.1 and 20.", "Envelope: threshold must be between 0 and 0.99."});
}

TEST_CASE("envelope: saved and read back", "[node:envelope]")
{
    test::NodeScene scene;
    auto& node = scene.Node(scene.Add(NodeType::Envelope, [](model::Node& n) {
        model::ParamsOf<EnvelopeParams>(n) = {0.05, 1.5, 4.0, 0.2, false};
    }));
    CHECK(test::RoundTrip(node) == node);
    const auto [read, issues] = test::ReadNode(NodeType::Envelope, {{"release", 2.0}, {"autoLevel", false}});
    CHECK(model::ParamsOf<EnvelopeParams>(read) == EnvelopeParams{0.01, 2.0, 1.0, 0.0, false});
    CHECK(issues.empty());
    CHECK(test::ReadNode(NodeType::Envelope, {{"autoLevel", "yes"}}).second.size() == 1);
}

TEST_CASE("envelope: on the canvas, attack, release, auto-level (gain only without it), threshold and a plot", "[node:envelope]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::Envelope);
    test::FakeNodeContext context(scene);
    context.signals[id] = 0.5f;
    test::HeadlessUi ui;
    ui.Frame(context, id);
    CHECK(context.edits.empty());
    CHECK(context.inputRows == std::vector<app::InputSpec>{{0, {}, model::PortKind::Audio, "Audio"}});
    CHECK(context.itemRects.count(kGainTip) == 0);
    REQUIRE(context.itemRects.count(kLevelTip) == 1);
    const auto [min, max] = context.itemRects.at(kLevelTip);
    CHECK(max.y - min.y == 34.f);

    // Turning auto-level off shows the gain.
    ui.Click(context, id, test::HeadlessUi::Centre(context, kAutoLevelTip));
    CHECK(context.LastEdit() == "Turn Off Envelope Auto-level");
    CHECK_FALSE(model::ParamsOf<EnvelopeParams>(scene.Node(id)).autoLevel);
    context.itemRects.clear();
    ui.Frame(context, id);
    CHECK(context.itemRects.count(kGainTip) == 1);
}
