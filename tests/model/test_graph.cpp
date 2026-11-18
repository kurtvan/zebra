#include "model/Classic.hpp"
#include "model/Graph.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace gv::model;

namespace {

/// preset-1 -> viewport.
auto Basic() -> Graph
{
    Graph graph;
    const auto preset = AddNode(graph, NodeType::Preset, 0, 0);
    ParamsOf<PresetParams>(*graph.Find(preset)).path = "/a.milk";
    const auto viewport = AddNode(graph, NodeType::Viewport, 0, 0);
    Connect(graph, preset, viewport, 0);
    return graph;
}

auto Has(const std::vector<GraphError>& errors, const std::string& text) -> bool
{
    return std::any_of(errors.begin(), errors.end(), [&](const GraphError& error) { return error.message.find(text) != std::string::npos; });
}

auto Feedback() -> int
{
    return static_cast<int>(PresetLayer::Feedback);
}

} // namespace

TEST_CASE("node ids are unique per type and the viewport is 'viewport'")
{
    Graph graph = Basic();
    CHECK(graph.Find("preset-1") != nullptr);
    CHECK(graph.Find("viewport") != nullptr);
    CHECK(AddNode(graph, NodeType::Preset, 0, 0) == "preset-2");
    CHECK(AddNode(graph, NodeType::LumaGate, 0, 0) == "gate-1");
    CHECK(AddNode(graph, NodeType::Viewport, 0, 0) == "viewport-1");
}

TEST_CASE("valid and unfinished graphs pass validation")
{
    CHECK(ValidateGraph(Basic()).empty());

    Graph unfinished;
    AddNode(unfinished, NodeType::Viewport, 0, 0);
    AddNode(unfinished, NodeType::Contrast, 0, 0); // loose
    const auto gate = AddNode(unfinished, NodeType::LumaGate, 0, 0);
    const auto preset = AddNode(unfinished, NodeType::Preset, 0, 0);
    Connect(unfinished, gate, preset, Feedback()); // an effect chain with no source carries no video
    CHECK(ValidateGraph(unfinished).empty());

    // Fan-out: one source into two inputs of one preset.
    Graph fan = Basic();
    const auto source = AddNode(fan, NodeType::VideoSource, 0, 0);
    const auto mix = AddNode(fan, NodeType::Mix, 0, 0);
    Connect(fan, source, mix, 0);
    Connect(fan, mix, "preset-1", Feedback());
    Connect(fan, source, "preset-1", static_cast<int>(PresetLayer::OnTop));
    Connect(fan, source, "preset-1", static_cast<int>(PresetLayer::Texture), "clouds");
    CHECK(ValidateGraph(fan).empty());
}

TEST_CASE("validation reports every broken rule")
{
    SECTION("viewports")
    {
        Graph none;
        AddNode(none, NodeType::Preset, 0, 0);
        CHECK(Has(ValidateGraph(none), "no viewport"));
        Graph two = Basic();
        AddNode(two, NodeType::Viewport, 0, 0);
        CHECK(Has(ValidateGraph(two), "only be one viewport"));
    }
    SECTION("kinds, inputs and missing nodes")
    {
        Graph graph = Basic();
        const auto source = AddNode(graph, NodeType::VideoSource, 0, 0);
        graph.wires.push_back({source, "viewport", 0, {}});     // video into a Frame input (and a second wire there)
        graph.wires.push_back({"preset-1", "nowhere", 0, {}});
        graph.wires.push_back({source, "preset-1", 7, {}});     // no input 8
        graph.wires.push_back({"viewport", source, 0, {}});     // the viewport has no output
        graph.wires.push_back({source, "preset-1", static_cast<int>(PresetLayer::Texture), {}}); // texture input without a name
        const auto errors = ValidateGraph(graph);
        CHECK(Has(errors, "takes a preset's picture, not video"));
        CHECK(Has(errors, "two wires into one input"));
        CHECK(Has(errors, "isn't there"));
        CHECK(Has(errors, "has no input 8"));
        CHECK(Has(errors, "has no output"));
        CHECK(Has(errors, "needs the name of the texture"));
    }
    SECTION("loops")
    {
        Graph graph = Basic();
        const auto a = AddNode(graph, NodeType::Contrast, 0, 0);
        const auto b = AddNode(graph, NodeType::Saturation, 0, 0);
        Connect(graph, a, b, 0);
        Connect(graph, b, a, 0);
        CHECK(Has(ValidateGraph(graph), "loop"));
    }
    SECTION("a switcher fed by a switcher of the same kind")
    {
        Graph graph = Basic();
        const auto outer = AddNode(graph, NodeType::Switcher, 0, 0);
        const auto inner = AddNode(graph, NodeType::Switcher, 0, 0);
        Connect(graph, inner, outer, 0);
        Connect(graph, "preset-1", inner, 0);
        Connect(graph, outer, "viewport", 0);
        CHECK(Has(ValidateGraph(graph), "can't be fed by another Preset switcher"));

        // A Video switcher upstream of a Frame switcher is fine.
        Graph mixed = Basic();
        const auto frames = AddNode(mixed, NodeType::Switcher, 0, 0);
        const auto videos = AddNode(mixed, NodeType::Switcher, 0, 0);
        ParamsOf<SwitcherParams>(*mixed.Find(videos)).kind = PortKind::Video;
        Connect(mixed, AddNode(mixed, NodeType::VideoSource, 0, 0), videos, 0);
        Connect(mixed, videos, "preset-1", Feedback());
        Connect(mixed, "preset-1", frames, 0);
        Connect(mixed, frames, "viewport", 0);
        CHECK(ValidateGraph(mixed).empty());
    }
    SECTION("one video source per preset")
    {
        Graph graph = Basic();
        const auto a = AddNode(graph, NodeType::VideoSource, 0, 0);
        const auto b = AddNode(graph, NodeType::VideoSource, 0, 0);
        const auto gate = AddNode(graph, NodeType::LumaGate, 0, 0);
        Connect(graph, a, gate, 0);
        Connect(graph, gate, "preset-1", Feedback());
        Connect(graph, b, "preset-1", static_cast<int>(PresetLayer::OnTop));
        CHECK(Has(ValidateGraph(graph), "only one video at a time"));
    }
    SECTION("parameter ranges")
    {
        Graph graph = Basic();
        ParamsOf<ContrastParams>(*graph.Find(AddNode(graph, NodeType::Contrast, 0, 0))).contrast = 101.f;
        ParamsOf<MixParams>(*graph.Find(AddNode(graph, NodeType::Mix, 0, 0))).opacity = -0.5f;
        auto& switcher = ParamsOf<SwitcherParams>(*graph.Find(AddNode(graph, NodeType::Switcher, 0, 0)));
        switcher.unit = IntervalUnit::Beats;
        switcher.intervalCount = 0;
        const auto errors = ValidateGraph(graph);
        CHECK(errors.size() == 3);
    }
    SECTION("each error once")
    {
        Graph graph = Basic();
        AddNode(graph, NodeType::Viewport, 0, 0);
        AddNode(graph, NodeType::Viewport, 0, 0);
        CHECK(ValidateGraph(graph).size() == 2); // one per extra viewport, nothing repeated
    }
}

TEST_CASE("edits: connect replaces, remove drops wires, the viewport stays, switchers keep one empty input")
{
    Graph graph = Basic();
    const auto other = AddNode(graph, NodeType::Preset, 0, 0);
    CHECK(Connect(graph, other, "viewport", 0)); // replaces preset-1's wire
    REQUIRE(graph.wires.size() == 1);
    CHECK(graph.wires[0].fromNode == other);
    CHECK_FALSE(RemoveNode(graph, "viewport"));
    CHECK(RemoveNode(graph, other));
    CHECK(graph.wires.empty());
    CHECK_FALSE(Disconnect(graph, "viewport", 0));

    const auto switcher = AddNode(graph, NodeType::Switcher, 0, 0);
    CHECK(InputCount(*graph.Find(switcher)) == 1);
    Connect(graph, "preset-1", switcher, 0);
    CHECK(InputCount(*graph.Find(switcher)) == 2);
    Connect(graph, AddNode(graph, NodeType::Preset, 0, 0), switcher, 1);
    CHECK(InputCount(*graph.Find(switcher)) == 3);
    Disconnect(graph, switcher, 0); // input numbers never change: input 1 stays wired
    CHECK(InputCount(*graph.Find(switcher)) == 3);
    CHECK(graph.Upstream(switcher, 1) != nullptr);
}

TEST_CASE("queries: presets and pool videos of a graph")
{
    ClassicScene classic;
    classic.playlist.enabled = true;
    classic.playlist.presets = {"/a.milk", "/b.milk"};
    classic.playlist.looks = {VideoLook{}, VideoLook{}};
    classic.video.enabled = true;
    classic.video.playlist = {true, {"x", "y"}, IntervalUnit::Seconds, 10.0, 4, false};
    const Graph graph = GraphFromClassic(classic);
    CHECK(PresetsOf(graph) == std::vector<std::filesystem::path>{"/a.milk", "/b.milk"});
    CHECK(VideoIdsOf(graph) == std::vector<std::string>{"x", "y"});
}

// --- Classic scenes ------------------------------------------------------------------------------

namespace {

auto Cases() -> std::vector<ClassicScene>
{
    std::vector<ClassicScene> cases;
    ClassicScene plain;
    plain.presetPath = "/p.milk";
    cases.push_back(plain);

    ClassicScene file = plain;
    file.video.enabled = true;
    file.video.source.videoId = "v";
    file.video.look = {0.6f, 0.3f, 2.0f, 0.5f, false, 0.7f}; // softness without overlay: dropped
    file.video.restartOnEnter = false;
    file.video.loop = false;
    cases.push_back(file);

    ClassicScene overlay = file;
    overlay.video.look.overlay = true;
    overlay.video.invertLuma = true;
    cases.push_back(overlay);

    ClassicScene syphon = plain;
    syphon.video.enabled = true;
    syphon.video.source = {VideoSourceType::Syphon, {}, {}, "Resolume", ""};
    syphon.video.playlist = {true, {"v"}, IntervalUnit::Bars, 30.0, 4, false}; // ignored for Syphon
    cases.push_back(syphon);

    ClassicScene videoPlaylist = overlay;
    videoPlaylist.video.playlist = {true, {"a", "b", "c"}, IntervalUnit::Beats, 30.0, 16, true};
    cases.push_back(videoPlaylist);

    ClassicScene presets;
    presets.presetPath = "/kept-by-format-2.milk";
    presets.playlist = {true, {"/a.milk", "/b.milk", "/c.milk"}, {VideoLook{0.1f}, VideoLook{0.2f, 0.4f, 1.f, 1.f, true, 0.3f}, VideoLook{0.3f}},
                        IntervalUnit::Bars, 30.0, 8, true};
    presets.video = videoPlaylist.video;
    cases.push_back(presets);

    ClassicScene presetsNoVideo = presets;
    presetsNoVideo.video.enabled = false;
    cases.push_back(presetsNoVideo);

    ClassicScene disabledPlaylist = file;
    disabledPlaylist.playlist = presets.playlist;
    disabledPlaylist.playlist.enabled = false;
    disabledPlaylist.video.playlist = videoPlaylist.video.playlist;
    disabledPlaylist.video.playlist.enabled = false;
    cases.push_back(disabledPlaylist);
    return cases;
}

} // namespace

TEST_CASE("classic scenes convert to valid graphs and read back unchanged")
{
    for (const auto& classic : Cases())
    {
        const Graph graph = GraphFromClassic(classic);
        CHECK(ValidateGraph(graph).empty());
        const auto back = ClassicFromGraph(graph);
        REQUIRE(back.has_value());
        CHECK(*back == NormalizeClassic(classic));
        CHECK(GraphFromClassic(*back) == graph); // deterministic ids and layout
    }
}

TEST_CASE("the conversion follows the product doc's table")
{
    auto cases = Cases();
    SECTION("a look becomes Contrast -> Saturation -> Luma gate (softness 0.1) -> Mix into Feedback")
    {
        const Graph graph = GraphFromClassic(cases[1]);
        const Node* mix = graph.Upstream("preset-1", Feedback());
        REQUIRE(mix != nullptr);
        CHECK(mix->type == NodeType::Mix);
        const Node* gate = graph.Upstream(mix->id, 0);
        REQUIRE(gate->type == NodeType::LumaGate);
        CHECK(ParamsOf<LumaGateParams>(*gate).softness == kFeedbackGateSoftness);
        const Node* saturation = graph.Upstream(gate->id, 0);
        CHECK(saturation->type == NodeType::Saturation);
        const Node* contrast = graph.Upstream(saturation->id, 0);
        CHECK(contrast->type == NodeType::Contrast);
        CHECK(graph.Upstream(contrast->id, 0)->type == NodeType::VideoSource);
        CHECK(graph.Upstream("preset-1", static_cast<int>(PresetLayer::OnTop)) == nullptr);
    }
    SECTION("the overlay is a second chain from the same source into On top, with its own softness")
    {
        const Graph graph = GraphFromClassic(cases[2]);
        const Node* mix = graph.Upstream("preset-1", static_cast<int>(PresetLayer::OnTop));
        REQUIRE(mix != nullptr);
        const Node* gate = graph.Upstream(mix->id, 0);
        CHECK(ParamsOf<LumaGateParams>(*gate).softness == 0.7f);
        CHECK(ParamsOf<LumaGateParams>(*gate).invert);
        CHECK(std::count_if(graph.nodes.begin(), graph.nodes.end(), [](const Node& n) { return n.type == NodeType::VideoSource; }) == 1);
    }
    SECTION("playlists become switchers")
    {
        const Graph graph = GraphFromClassic(cases[5]);
        const Node* frames = graph.Upstream("viewport", 0);
        REQUIRE(frames->type == NodeType::Switcher);
        CHECK(ParamsOf<SwitcherParams>(*frames).kind == PortKind::Frame);
        CHECK(ParamsOf<SwitcherParams>(*frames).inputCount == 4); // three presets and the empty input
        CHECK(ParamsOf<SwitcherParams>(*frames).unit == IntervalUnit::Bars);
        int videoSwitchers = 0;
        for (const auto& node : graph.nodes)
        {
            if (node.type == NodeType::Switcher && ParamsOf<SwitcherParams>(node).kind == PortKind::Video)
            {
                videoSwitchers++;
                CHECK(ParamsOf<SwitcherParams>(node).inputCount == 4);
                CHECK(ParamsOf<SwitcherParams>(node).shuffle);
            }
        }
        CHECK(videoSwitchers == 1); // shared by every preset's chains
    }
}

TEST_CASE("graphs that aren't classic-shaped are reported as such")
{
    Graph graph = GraphFromClassic(Cases()[1]);
    CHECK(ClassicFromGraph(graph).has_value());
    // An extra effect in the chain.
    const auto extra = AddNode(graph, NodeType::Contrast, 0, 0);
    const Node* mix = graph.Upstream("preset-1", Feedback());
    const std::string mixId = mix->id;
    Connect(graph, mixId, extra, 0);
    Connect(graph, extra, "preset-1", Feedback());
    CHECK_FALSE(ClassicFromGraph(graph).has_value());

    // Nothing wired into the viewport: an empty classic scene.
    Graph empty;
    AddNode(empty, NodeType::Viewport, 0, 0);
    CHECK(ClassicFromGraph(empty) == ClassicScene{});
}

TEST_CASE("scenes the classic view can't read still find their presets, playlist and video")
{
    ClassicScene classic;
    classic.playlist = {true, {"/a.milk", "/b.milk"}, {VideoLook{}, VideoLook{}}, IntervalUnit::Seconds, 10, 4, true};
    classic.video.enabled = true;
    classic.video.playlist = {true, {"x", "y"}, IntervalUnit::Seconds, 12, 4, false};
    Graph graph = GraphFromClassic(classic);
    REQUIRE(ClassicFromGraph(graph).has_value());

    // Every chain moves from the feedback input into a texture input: no longer a classic scene.
    for (auto& wire : graph.wires)
    {
        const Node* to = graph.Find(wire.toNode);
        if (to != nullptr && to->type == NodeType::Preset && wire.toInput == static_cast<int>(PresetLayer::Feedback))
        {
            wire.toInput = static_cast<int>(PresetLayer::Texture);
            wire.textureName = "worms";
        }
    }
    CHECK(ValidateGraph(graph).empty());
    CHECK_FALSE(ClassicFromGraph(graph).has_value());

    const ClassicScene view = RuntimeViewFromGraph(graph);
    CHECK(view.presetPath == "/a.milk");
    CHECK(view.playlist.enabled);
    CHECK(view.playlist.presets == std::vector<std::filesystem::path>{"/a.milk", "/b.milk"});
    CHECK(view.playlist.intervalSeconds == 10);
    CHECK(view.playlist.shuffle);
    CHECK(view.video.enabled);
    CHECK(view.video.playlist.enabled);
    CHECK(view.video.playlist.videos == std::vector<std::string>{"x", "y"});
    CHECK(view.video.playlist.intervalSeconds == 12);
    CHECK(view.video.source.videoId == "x");

    // A preset alone, with nothing wired into it.
    CHECK(RuntimeViewFromGraph(Basic()).presetPath == "/a.milk");
    CHECK_FALSE(RuntimeViewFromGraph(Basic()).video.enabled);
}
