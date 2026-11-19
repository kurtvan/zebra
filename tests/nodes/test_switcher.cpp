// The Switcher node (Preset and Video switchers): model/nodes/SwitcherNode.cpp, app/nodes/SwitcherNode.cpp.

#include "nodes/NodeTestKit.hpp"

#include "engine/RenderPlan.hpp"

using namespace gv;
using model::IntervalUnit;
using model::NodeType;
using model::SwitcherParams;

namespace {

auto Params(const model::Node& node) -> const SwitcherParams&
{
    return model::ParamsOf<SwitcherParams>(node);
}

auto VideoKind(model::Node& node)
{
    model::ParamsOf<SwitcherParams>(node).kind = model::PortKind::Video;
}

} // namespace

TEST_CASE("switcher: a Preset switcher by default, every 30 seconds, in order, with one free input", "[node:switcher]")
{
    test::NodeScene scene;
    const auto& node = scene.Node(scene.Add(NodeType::Switcher));
    CHECK(node.id == "switcher-1");
    CHECK(Params(node).kind == model::PortKind::Frame);
    CHECK(Params(node).unit == IntervalUnit::Seconds);
    CHECK(Params(node).intervalSeconds == 30.0);
    CHECK_FALSE(Params(node).shuffle);
    CHECK(model::InputCount(node) == 1);
    CHECK(model::Label(node) == "Preset switcher");
}

TEST_CASE("switcher: its kind sets what it takes and gives", "[node:switcher]")
{
    test::NodeScene scene;
    auto& node = scene.Node(scene.Add(NodeType::Switcher, VideoKind));
    CHECK(model::Label(node) == "Video switcher");
    CHECK(model::OutputKind(node) == model::PortKind::Video);
    CHECK(model::InputKind(node, 0) == model::PortKind::Video);
}

TEST_CASE("switcher: always one free input after the last wired one, never renumbered", "[node:switcher]")
{
    test::NodeScene scene;
    const std::string switcher = scene.Add(NodeType::Switcher);
    const std::string a = scene.Add(NodeType::Preset);
    const std::string b = scene.Add(NodeType::Preset);
    auto count = [&] { return model::InputCount(scene.Node(switcher)); };
    model::Connect(scene.Graph(), a, switcher, 0);
    CHECK(count() == 2);
    model::Connect(scene.Graph(), b, switcher, 1);
    CHECK(count() == 3);
    model::Disconnect(scene.Graph(), switcher, 0);
    CHECK(count() == 3); // input 2 keeps its number
    CHECK(scene.Graph().WireInto(switcher, 1) != nullptr);
    model::Disconnect(scene.Graph(), switcher, 1);
    CHECK(count() == 1);
}

TEST_CASE("switcher: interval and input limits", "[node:switcher]")
{
    auto with = [](const std::function<void(SwitcherParams&)>& change) {
        return [change](model::Node& node) { change(model::ParamsOf<SwitcherParams>(node)); };
    };
    CHECK(test::ParamErrors(NodeType::Switcher, with([](auto& p) { p.intervalSeconds = 1.0; })).empty());
    CHECK(test::ParamErrors(NodeType::Switcher, with([](auto& p) { p.intervalSeconds = 0.5; })) ==
          std::vector<std::string>{"Preset switcher: the interval must be between 1 and 3600 seconds."});
    // Counting beats, the seconds don't matter, the count does.
    CHECK(test::ParamErrors(NodeType::Switcher, with([](auto& p) {
              p.unit = IntervalUnit::Beats;
              p.intervalSeconds = 0.0;
          })).empty());
    CHECK(test::ParamErrors(NodeType::Switcher, with([](auto& p) {
              p.unit = IntervalUnit::Bars;
              p.intervalCount = 0;
          })) == std::vector<std::string>{"Preset switcher: the interval must be between 1 and 256."});
    CHECK(test::ParamErrors(NodeType::Switcher, with([](auto& p) { p.inputCount = model::kMaxPlaylistCount + 2; })) ==
          std::vector<std::string>{"Preset switcher: it can have at most 256 inputs."});
}

TEST_CASE("switcher: can't be fed by a switcher of its own kind, even further up", "[node:switcher]")
{
    test::NodeScene scene;
    const std::string top = scene.Add(NodeType::Switcher);
    const std::string inner = scene.Add(NodeType::Switcher);
    CHECK(test::Messages(model::ConnectErrors(scene.Graph(), inner, top, 0)) ==
          std::vector<std::string>{"A Preset switcher can't be fed by another Preset switcher."});

    // A Video switcher further up, through a preset, is fine.
    const std::string preset = scene.Add(NodeType::Preset);
    const std::string videos = scene.Add(NodeType::Switcher, VideoKind);
    model::Connect(scene.Graph(), scene.Add(NodeType::VideoSource), videos, 0);
    model::Connect(scene.Graph(), videos, preset, 0);
    CHECK(model::ConnectErrors(scene.Graph(), preset, top, 0).empty());
}

TEST_CASE("switcher: saved with kind and timing, and read back; unknown kinds and units are errors", "[node:switcher]")
{
    test::NodeScene scene;
    auto& node = scene.Node(scene.Add(NodeType::Switcher, [](model::Node& n) {
        auto& p = model::ParamsOf<SwitcherParams>(n);
        p.kind = model::PortKind::Video;
        p.unit = IntervalUnit::Bars;
        p.intervalCount = 8;
        p.shuffle = true;
    }));
    auto loaded = test::RoundTrip(node);
    CHECK(Params(loaded).kind == model::PortKind::Video);
    CHECK(Params(loaded).unit == IntervalUnit::Bars);
    CHECK(Params(loaded).intervalCount == 8);
    CHECK(Params(loaded).shuffle);

    auto [bad, issues] = test::ReadNode(NodeType::Switcher, {{"kind", "audio"}, {"intervalUnit", "minutes"}});
    REQUIRE(issues.size() == 2);
    CHECK(issues[0].path == "/node/kind");
    CHECK(issues[1].path == "/node/intervalUnit");
}

TEST_CASE("switcher: the plan follows the selected input, else the first wired one", "[node:switcher]")
{
    test::NodeScene scene;
    const std::string switcher = scene.Add(NodeType::Switcher);
    const std::string a = scene.Add(NodeType::Preset, [](model::Node& n) { model::ParamsOf<model::PresetParams>(n).path = "/a.milk"; });
    const std::string b = scene.Add(NodeType::Preset, [](model::Node& n) { model::ParamsOf<model::PresetParams>(n).path = "/b.milk"; });
    model::Connect(scene.Graph(), switcher, "viewport", 0);
    model::Connect(scene.Graph(), a, switcher, 1);
    model::Connect(scene.Graph(), b, switcher, 2);
    CHECK(engine::CompilePlan(scene.Graph()).presetNode == a);
    CHECK(engine::CompilePlan(scene.Graph(), {{switcher, 2}}).presetNode == b);
    CHECK(engine::CompilePlan(scene.Graph(), {{switcher, 0}}).presetNode == a); // input 1 is free
    CHECK(engine::SelectionForItem(scene.Graph(), 1) == engine::SwitcherSelection{{switcher, 2}});
}

TEST_CASE("switcher: on the canvas, numbered inputs with the free one marked", "[node:switcher]")
{
    test::NodeScene scene;
    const std::string switcher = scene.Add(NodeType::Switcher);
    model::Connect(scene.Graph(), scene.Add(NodeType::Preset), switcher, 0);
    test::FakeNodeContext context(scene);
    CHECK(app::NodeTitle(scene.Node(switcher), context) == "Preset switcher");
    CHECK(app::NodeInputs(scene.Node(switcher), context) ==
          std::vector<app::InputSpec>{{0, {}, model::PortKind::Frame, "1"}, {1, {}, model::PortKind::Frame, "2  (free)"}});
}

TEST_CASE("switcher: seconds get a seconds slider, beats and bars a count; both stay in range", "[node:switcher]")
{
    const auto& seconds = test::ControlById(NodeType::Switcher, "##seconds");
    const auto& count = test::ControlById(NodeType::Switcher, "##count");
    CHECK(seconds.min == model::kMinPlaylistInterval);
    CHECK(seconds.max == model::kMaxPlaylistInterval);
    CHECK(count.integer);
    CHECK(count.min == 1);
    CHECK(count.max == model::kMaxPlaylistCount);
    CHECK(std::string(seconds.field) == "interval");
    model::Node node;
    node.type = NodeType::Switcher;
    node.params = SwitcherParams{};
    CHECK(seconds.visible(node));
    CHECK_FALSE(count.visible(node));
    model::ParamsOf<SwitcherParams>(node).unit = IntervalUnit::Beats;
    CHECK_FALSE(seconds.visible(node));
    CHECK(count.visible(node));
}

TEST_CASE("switcher: the unit button lists seconds, beats and bars", "[node:switcher]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::Switcher);
    test::FakeNodeContext context(scene);
    test::HeadlessUi ui;
    ui.Frame(context, id);
    ui.Click(context, id, test::HeadlessUi::Centre(context, "Beats and bars follow the MIDI clock."));
    const int list = test::IndexOf(NodeType::Switcher, test::ControlById(NodeType::Switcher, "##unit"));
    REQUIRE(context.popups == std::vector<int>{list});

    ui.Frame(context, id, list);
    ui.Click(context, id, test::HeadlessUi::PopupRow(1), list);
    CHECK(Params(scene.Node(id)).unit == IntervalUnit::Beats);
    CHECK(context.LastEdit() == "Change Interval Unit");
    const size_t edits = context.edits.size();
    ui.Click(context, id, test::HeadlessUi::PopupRow(1), list); // the unit it already has: no edit
    CHECK(context.edits.size() == edits);
}

TEST_CASE("switcher: Shuffle on and off", "[node:switcher]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::Switcher);
    test::FakeNodeContext context(scene);
    test::HeadlessUi ui;
    ui.Frame(context, id);
    const ImVec2 shuffle = test::HeadlessUi::Centre(context, "Off: top to bottom. On: random order, never the same input twice in a row.");
    ui.Click(context, id, shuffle);
    CHECK(Params(scene.Node(id)).shuffle);
    CHECK(context.LastEdit() == "Shuffle");
    ui.Click(context, id, shuffle);
    CHECK(context.LastEdit() == "Play in Order");
}

TEST_CASE("switcher: the Add menu has a Preset and a Video switcher", "[node:switcher]")
{
    const auto& items = app::Ui(NodeType::Switcher).addItems;
    REQUIRE(items.size() == 2);
    CHECK(std::string(items[0].label) == "Preset switcher");
    CHECK(std::string(items[1].label) == "Video switcher");
    model::Node node;
    node.type = NodeType::Switcher;
    node.params = SwitcherParams{};
    items[1].configure(node);
    CHECK(Params(node).kind == model::PortKind::Video);
}
