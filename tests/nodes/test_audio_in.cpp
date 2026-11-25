// The Audio In node: model/nodes/AudioInNode.cpp, app/nodes/AudioInNode.cpp. What follows it: test_envelope.cpp.

#include "nodes/NodeTestKit.hpp"

using namespace gv;
using model::NodeType;

TEST_CASE("audio in: one Audio output, no inputs, no settings", "[node:audio_in]")
{
    test::NodeScene scene;
    const auto& node = scene.Node(scene.Add(NodeType::AudioIn));
    CHECK(node.id == "audio-1");
    CHECK(model::Label(node) == "Audio In");
    CHECK(model::OutputKind(node) == model::PortKind::Audio);
    CHECK(model::InputCount(node) == 0);
    CHECK(model::Def(NodeType::AudioIn).signal == nullptr); // its audio goes to the engine, not a number
    CHECK(model::ValidateGraph(scene.Graph()).empty());
}

TEST_CASE("audio in: saved and read back", "[node:audio_in]")
{
    test::NodeScene scene;
    auto& node = scene.Node(scene.Add(NodeType::AudioIn));
    node.x = 40.f;
    CHECK(test::RoundTrip(node) == node);
    CHECK(model::NodeTypeFromJson("audioIn") == NodeType::AudioIn);
    CHECK(test::ReadNode(NodeType::AudioIn, {{"unused", 1}}).second.empty());
}

TEST_CASE("audio in: on the canvas, a line saying what it is, and in the Add menu", "[node:audio_in]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::AudioIn);
    test::FakeNodeContext context(scene);
    test::HeadlessUi ui;
    ui.Frame(context, id);
    CHECK(context.edits.empty());
    CHECK(context.inputRows.empty());
    REQUIRE(app::Ui(NodeType::AudioIn).addItems.size() == 1);
    CHECK(std::string(app::Ui(NodeType::AudioIn).addItems[0].label) == "Audio In");
}
