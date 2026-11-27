// The Band node: model/nodes/BandNode.cpp, app/nodes/BandNode.cpp. Its filter: tests/engine/test_band_filter.cpp;
// with Envelopes: tests/engine/test_scene_audio.cpp. Wiring rules: tests/model/test_graph.cpp.

#include "model/nodes/BandNode.hpp"
#include "nodes/NodeTestKit.hpp"

using namespace gv;
using model::BandParams;
using model::NodeType;

namespace {

const char* const kPresetTip = "Bass is below 250 Hz (the kick and bass line), mid is 250 to 4000 Hz (voices, most instruments), treble is above 4000 Hz (hi-hats, cymbals).";
const char* const kStripTip =
    "The range it lets through, out of everything you can hear (20 Hz to 20 kHz). Wire it into an Envelope to follow just this range.";

auto Control(const char* id) -> const app::Control&
{
    for (const auto& control : app::Ui(NodeType::Band).controls)
    {
        if (std::string(control.id) == id)
            return control;
    }
    FAIL("no control " << id);
    return app::Ui(NodeType::Band).controls.front();
}

} // namespace

TEST_CASE("band: one Audio input, an Audio output; the bass by default", "[node:band]")
{
    test::NodeScene scene;
    const auto& node = scene.Node(scene.Add(NodeType::Band));
    CHECK(node.id == "band-1");
    CHECK(model::OutputKind(node) == model::PortKind::Audio);
    CHECK(model::InputCount(node) == 3); // the audio, then the modulation inputs of low and high
    CHECK(model::InputKind(node, 0) == model::PortKind::Audio);
    CHECK(model::Def(NodeType::Band).signal == nullptr); // its audio goes on to an Envelope, not a number
    CHECK(model::ParamsOf<BandParams>(node) == BandParams{20.0, 250.0});
    CHECK(model::BandRangeText(model::ParamsOf<BandParams>(node)) == "20-250 Hz");
}

TEST_CASE("band: its settings' ranges", "[node:band]")
{
    auto set = [](BandParams params) { return [=](model::Node& node) { model::ParamsOf<BandParams>(node) = params; }; };
    CHECK(test::ParamErrors(NodeType::Band, set({20.0, 20000.0})).empty());
    CHECK(test::ParamErrors(NodeType::Band, set({500.0, 500.0})).empty());
    CHECK(test::ParamErrors(NodeType::Band, set({10.0, 30000.0})) ==
          std::vector<std::string>{"Band: the low end must be between 20 and 20000 Hz.", "Band: the high end must be between 20 and 20000 Hz."});
    CHECK(test::ParamErrors(NodeType::Band, set({4000.0, 250.0})) == std::vector<std::string>{"Band: the low end must be below the high end."});
}

TEST_CASE("band: saved and read back", "[node:band]")
{
    test::NodeScene scene;
    auto& node = scene.Node(scene.Add(NodeType::Band, [](model::Node& n) { model::ParamsOf<BandParams>(n) = {60.0, 180.0}; }));
    CHECK(test::RoundTrip(node) == node);
    CHECK(model::NodeTypeFromJson("band") == NodeType::Band);
    const auto [read, issues] = test::ReadNode(NodeType::Band, {{"high", 120.0}});
    CHECK(model::ParamsOf<BandParams>(read) == BandParams{20.0, 120.0});
    CHECK(issues.empty());
    CHECK(test::ReadNode(NodeType::Band, {{"low", "deep"}}).second.size() == 1);
}

TEST_CASE("band: a preset sets the range; the range can still be set by hand", "[node:band]")
{
    const auto& preset = Control("##preset");
    const auto sliders = test::ParamSliders(NodeType::Band); // low, then high
    REQUIRE(sliders.size() == 2);
    const auto& low = sliders[0];
    const auto& high = sliders[1];
    model::Node node;
    node.type = NodeType::Band;
    node.params = BandParams{};
    CHECK(preset.get(node) == 0.0); // bass

    preset.set(node, 1.0);
    CHECK(model::ParamsOf<BandParams>(node) == BandParams{250.0, 4000.0});
    CHECK(model::BandRangeText(model::ParamsOf<BandParams>(node)) == "250-4000 Hz");
    preset.set(node, 2.0);
    CHECK(model::ParamsOf<BandParams>(node) == BandParams{4000.0, 20000.0});
    CHECK(preset.get(node) == 2.0);

    // By hand: no preset is chosen any more.
    low.set(node, 6000.0);
    CHECK(preset.get(node) == -1.0);
    CHECK(model::BandRangeText(model::ParamsOf<BandParams>(node)) == "6000-20000 Hz");
    // An end moved past the other pushes it along.
    high.set(node, 100.0);
    CHECK(model::ParamsOf<BandParams>(node) == BandParams{100.0, 100.0});
    low.set(node, 300.0);
    CHECK(model::ParamsOf<BandParams>(node) == BandParams{300.0, 300.0});
}

TEST_CASE("band: on the canvas, the presets, a strip with the range, and low and high sliders", "[node:band]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::Band);
    test::FakeNodeContext context(scene);
    test::HeadlessUi ui;
    ui.Frame(context, id);
    CHECK(context.edits.empty());
    CHECK(context.inputRows == std::vector<app::InputSpec>{{0, {}, model::PortKind::Audio, "Audio"}});
    REQUIRE(context.itemRects.count(kStripTip) == 1);
    const auto [min, max] = context.itemRects.at(kStripTip);
    CHECK(max.y - min.y == 22.f);
    CHECK(context.itemRects.count("Sound below this is cut.") == 1);
    CHECK(context.itemRects.count("Sound above this is cut.") == 1);

    // Clicking the last preset (treble) sets the range, as one undo step.
    ui.Click(context, id, test::HeadlessUi::Centre(context, kPresetTip));
    CHECK(context.LastEdit() == "Choose Band Range");
    CHECK(model::ParamsOf<BandParams>(scene.Node(id)) == BandParams{4000.0, 20000.0});
    REQUIRE(app::Ui(NodeType::Band).addItems.size() == 1);
    CHECK(std::string(app::Ui(NodeType::Band).addItems[0].label) == "Band");
}
