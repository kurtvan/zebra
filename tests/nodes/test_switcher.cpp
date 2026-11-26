// The Switcher node (switches videos or Milkdrop presets, whichever is wired into it): model/nodes/SwitcherNode.cpp, app/nodes/SwitcherNode.cpp.

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

} // namespace

TEST_CASE("switcher: every 30 seconds, in order, with one free input", "[node:switcher]")
{
    test::NodeScene scene;
    const auto& node = scene.Node(scene.Add(NodeType::Switcher));
    CHECK(node.id == "switcher-1");
    CHECK(Params(node).unit == IntervalUnit::Seconds);
    CHECK(Params(node).intervalSeconds == 30.0);
    CHECK_FALSE(Params(node).shuffle);
    CHECK(model::InputCount(node) == 1);
    CHECK(model::Label(node) == "Switcher");
}

TEST_CASE("switcher: it takes and gives pictures", "[node:switcher]")
{
    test::NodeScene scene;
    auto& node = scene.Node(scene.Add(NodeType::Switcher));
    CHECK(model::OutputKind(node) == model::PortKind::Picture);
    CHECK(model::InputKind(node, 0) == model::PortKind::Picture);
}

TEST_CASE("switcher: what's wired into it decides where it can go, not a setting", "[node:switcher]")
{
    test::NodeScene scene;
    const std::string switcher = scene.Add(NodeType::Switcher);
    const std::string video = scene.Add(NodeType::VideoSource);
    const std::string preset = scene.Add(NodeType::MilkdropPreset);
    const std::string opacity = scene.Add(NodeType::Opacity);

    // Empty, a video or a preset can go in, and its output can go anywhere.
    CHECK(model::ConnectErrors(scene.Graph(), video, switcher, 0).empty());
    CHECK(model::ConnectErrors(scene.Graph(), preset, switcher, 0).empty());
    CHECK(model::ConnectErrors(scene.Graph(), switcher, "viewport", 0).empty());
    CHECK(model::ConnectErrors(scene.Graph(), switcher, opacity, 0).empty());

    // Switching videos: no presets in; on the viewport, or into an effect.
    model::Connect(scene.Graph(), video, switcher, 0);
    CHECK(model::SwitchesVideos(scene.Graph(), scene.Node(switcher)));
    CHECK(test::Messages(model::ConnectErrors(scene.Graph(), preset, switcher, 1)) ==
          std::vector<std::string>{"Switcher: it switches videos or presets, not both (not supported yet)."});
    CHECK(model::ConnectErrors(scene.Graph(), switcher, "viewport", 0).empty());
    CHECK(model::ConnectErrors(scene.Graph(), switcher, opacity, 0).empty());

    // Switching presets instead: on the viewport, not into an effect.
    model::Disconnect(scene.Graph(), switcher, 0);
    model::Connect(scene.Graph(), preset, switcher, 0);
    CHECK_FALSE(model::SwitchesVideos(scene.Graph(), scene.Node(switcher)));
    CHECK(model::ConnectErrors(scene.Graph(), switcher, "viewport", 0).empty());
    CHECK(test::Messages(model::ConnectErrors(scene.Graph(), switcher, opacity, 0)) ==
          std::vector<std::string>{"Opacity: an effect works on a video, not on a preset's picture (not supported yet)."});
}

TEST_CASE("switcher: with nothing wired in, it switches videos if it feeds a preset", "[node:switcher]")
{
    test::NodeScene scene;
    const std::string frames = scene.Add(NodeType::Switcher);
    model::Connect(scene.Graph(), frames, "viewport", 0);
    CHECK_FALSE(model::SwitchesVideos(scene.Graph(), scene.Node(frames)));
    const std::string videos = scene.Add(NodeType::Switcher);
    model::Connect(scene.Graph(), videos, scene.Add(NodeType::MilkdropPreset), 0);
    CHECK(model::SwitchesVideos(scene.Graph(), scene.Node(videos)));
    CHECK(model::ValidateGraph(scene.Graph()).empty());
}

TEST_CASE("switcher: a show file's old kind is ignored", "[node:switcher]")
{
    // Saved as a video switcher, but wired between presets and the viewport: it opens as its wires say.
    const auto loaded = model::ParsePerformance(R"({"version": 3, "scenes": [{"id": "a", "midi": {"cc20Value": 0}, "graph": {
        "nodes": [{"id": "s", "type": "switcher", "kind": "video"}, {"id": "p", "type": "milkdropPreset", "path": "/x.milk"},
                  {"id": "v", "type": "viewport"}],
        "wires": [{"from": "s", "to": "v", "input": 0}, {"from": "p", "to": "s", "input": 0}]}}]})");
    REQUIRE(loaded.ok);
    CHECK(loaded.issues.empty());
    const auto& graph = loaded.performance.scenes[0].graph;
    CHECK(graph.wires.size() == 2);
    CHECK(model::ValidateGraph(graph).empty());
}

TEST_CASE("switcher: always one free input after the last wired one, never renumbered", "[node:switcher]")
{
    test::NodeScene scene;
    const std::string switcher = scene.Add(NodeType::Switcher);
    const std::string a = scene.Add(NodeType::MilkdropPreset);
    const std::string b = scene.Add(NodeType::MilkdropPreset);
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
          std::vector<std::string>{"Switcher: the interval must be between 1 and 3600 seconds."});
    // Counting beats, the seconds don't matter, the count does.
    CHECK(test::ParamErrors(NodeType::Switcher, with([](auto& p) {
              p.unit = IntervalUnit::Beats;
              p.intervalSeconds = 0.0;
          })).empty());
    CHECK(test::ParamErrors(NodeType::Switcher, with([](auto& p) {
              p.unit = IntervalUnit::Bars;
              p.intervalCount = 0;
          })) == std::vector<std::string>{"Switcher: the interval must be between 1 and 256."});
    CHECK(test::ParamErrors(NodeType::Switcher, with([](auto& p) { p.inputCount = model::kMaxPlaylistCount + 2; })) ==
          std::vector<std::string>{"Switcher: it can have at most 256 inputs."});
}

TEST_CASE("switcher: can't be fed by another switcher, even through an effect", "[node:switcher]")
{
    test::NodeScene scene;
    const std::string top = scene.Add(NodeType::Switcher);
    const std::string inner = scene.Add(NodeType::Switcher);
    CHECK(test::Messages(model::ConnectErrors(scene.Graph(), inner, top, 0)) ==
          std::vector<std::string>{"A switcher can't be fed by another switcher."});
    const std::string contrast = scene.Add(NodeType::Contrast);
    model::Connect(scene.Graph(), inner, contrast, 0);
    CHECK(test::Messages(model::ConnectErrors(scene.Graph(), contrast, top, 0)) ==
          std::vector<std::string>{"A switcher can't be fed by another switcher."});

    // A video switcher further up, through a preset, is fine.
    const std::string preset = scene.Add(NodeType::MilkdropPreset);
    const std::string videos = scene.Add(NodeType::Switcher);
    model::Connect(scene.Graph(), scene.Add(NodeType::VideoSource), videos, 0);
    model::Connect(scene.Graph(), videos, preset, 0);
    CHECK(model::ConnectErrors(scene.Graph(), preset, top, 0).empty());
}

TEST_CASE("switcher: saved with its timing and no kind, and read back; unknown units are errors", "[node:switcher]")
{
    test::NodeScene scene;
    auto& node = scene.Node(scene.Add(NodeType::Switcher, [](model::Node& n) {
        auto& p = model::ParamsOf<SwitcherParams>(n);
        p.unit = IntervalUnit::Bars;
        p.intervalCount = 8;
        p.shuffle = true;
    }));
    nlohmann::json object = nlohmann::json::object();
    model::Def(NodeType::Switcher).write(node, object);
    CHECK_FALSE(object.contains("kind"));
    CHECK(Params(test::RoundTrip(node)) == Params(node));

    // An old file's kind, whatever it says, is ignored.
    auto [bad, issues] = test::ReadNode(NodeType::Switcher, {{"kind", "audio"}, {"intervalUnit", "minutes"}});
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "/node/intervalUnit");
}

TEST_CASE("switcher: the plan follows the selected input, else the first wired one", "[node:switcher]")
{
    test::NodeScene scene;
    const std::string switcher = scene.Add(NodeType::Switcher);
    const std::string a = scene.Add(NodeType::MilkdropPreset, [](model::Node& n) { model::ParamsOf<model::MilkdropPresetParams>(n).path = "/a.milk"; });
    const std::string b = scene.Add(NodeType::MilkdropPreset, [](model::Node& n) { model::ParamsOf<model::MilkdropPresetParams>(n).path = "/b.milk"; });
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
    model::Connect(scene.Graph(), scene.Add(NodeType::MilkdropPreset), switcher, 0);
    test::FakeNodeContext context(scene);
    CHECK(app::NodeTitle(scene.Node(switcher), context) == "Switcher");
    const auto inputs = app::NodeInputs(scene.Node(switcher), context);
    REQUIRE(inputs.size() == 2);
    CHECK(inputs[0] == app::InputSpec{0, {}, model::PortKind::Picture, "1", false, "Double-click to go live on this preset now."});
    CHECK(inputs[1] == app::InputSpec{1, {}, model::PortKind::Picture, "2  (free)"});
}

TEST_CASE("switcher: live, the input passing shows how long it has left, and the next one is marked", "[node:switcher]")
{
    test::NodeScene scene;
    const std::string switcher = scene.Add(NodeType::Switcher);
    for (int i = 0; i < 3; ++i)
        model::Connect(scene.Graph(), scene.Add(NodeType::MilkdropPreset), switcher, i);
    test::FakeNodeContext context(scene);
    context.live = true;
    auto labels = [&] {
        std::vector<std::string> result;
        for (const auto& spec : app::NodeInputs(scene.Node(switcher), context))
            result.push_back(spec.label);
        return result;
    };
    context.switchers[switcher] = {1, 2, 11.2, -1.0, false};
    CHECK(labels() == std::vector<std::string>{"1", "2  passing, 12 s", "3  next", "4  (free)"});
    CHECK(app::NodeInputs(scene.Node(switcher), context)[1].passing);
    CHECK_FALSE(app::NodeInputs(scene.Node(switcher), context)[0].passing);
    context.switchers[switcher] = {0, 1, -1.0, 1.0, false};
    CHECK(labels()[0] == "1  passing, 1 beat");
    context.switchers[switcher] = {0, 1, -1.0, 6.5, false};
    CHECK(labels()[0] == "1  passing, 7 beats");
    context.switchers[switcher] = {2, -1, -1.0, -1.0, true};
    CHECK(labels()[2] == "3  passing, waiting for the MIDI clock");
}

TEST_CASE("switcher: double-clicking a wired input switches to it; a free one does nothing", "[node:switcher]")
{
    test::NodeScene scene;
    const std::string switcher = scene.Add(NodeType::Switcher);
    model::Connect(scene.Graph(), scene.Add(NodeType::VideoSource), switcher, 0);
    model::Connect(scene.Graph(), scene.Add(NodeType::VideoSource), switcher, 1);
    test::FakeNodeContext context(scene);

    // A video switcher only offers it while its scene is live.
    CHECK(app::NodeInputs(scene.Node(switcher), context)[1].tooltip.empty());
    context.live = true;
    CHECK(app::NodeInputs(scene.Node(switcher), context)[1].tooltip == "Double-click to switch to this video now.");

    context.doubleClickedInputs = {1, 2}; // input 2 is the free one
    test::HeadlessUi ui;
    ui.Frame(context, switcher);
    CHECK(context.jumps == std::vector<std::pair<std::string, int>>{{switcher, 1}});
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

TEST_CASE("switcher: the Add menu has one Switcher, for videos or presets", "[node:switcher]")
{
    const auto& items = app::Ui(NodeType::Switcher).addItems;
    REQUIRE(items.size() == 1);
    CHECK(std::string(items[0].label) == "Switcher");
    CHECK(items[0].configure == nullptr);
    CHECK(std::string(items[0].tooltip) == "Switches videos or Milkdrop presets: whatever you wire into it.");
}
