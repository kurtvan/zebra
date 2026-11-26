#include "model/LegacyScene.hpp"
#include "model/Graph.hpp"
#include "model/Performance.hpp"

#include "model/ClassicView.hpp"
#include "model/nodes/NodeDef.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace gv::model;

namespace {

/// preset-1 -> viewport.
auto Basic() -> Graph
{
    Graph graph;
    const auto preset = AddNode(graph, NodeType::MilkdropPreset, 0, 0);
    ParamsOf<MilkdropPresetParams>(*graph.Find(preset)).path = "/a.milk";
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
    CHECK(graph.Find("milkdrop-1") != nullptr);
    CHECK(graph.Find("viewport") != nullptr);
    CHECK(AddNode(graph, NodeType::MilkdropPreset, 0, 0) == "milkdrop-2");
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
    const auto preset = AddNode(unfinished, NodeType::MilkdropPreset, 0, 0);
    Connect(unfinished, gate, preset, Feedback()); // an effect chain with no source carries no video
    CHECK(ValidateGraph(unfinished).empty());

    // Fan-out: one source into two inputs of one preset.
    Graph fan = Basic();
    const auto source = AddNode(fan, NodeType::VideoSource, 0, 0);
    const auto mix = AddNode(fan, NodeType::Opacity, 0, 0);
    Connect(fan, source, mix, 0);
    Connect(fan, mix, "milkdrop-1", Feedback());
    Connect(fan, source, "milkdrop-1", static_cast<int>(PresetLayer::OnTop));
    Connect(fan, source, "milkdrop-1", static_cast<int>(PresetLayer::Texture), "clouds");
    CHECK(ValidateGraph(fan).empty());
}

TEST_CASE("validation reports every broken rule")
{
    SECTION("viewports")
    {
        Graph none;
        AddNode(none, NodeType::MilkdropPreset, 0, 0);
        CHECK(Has(ValidateGraph(none), "no viewport"));
        Graph two = Basic();
        AddNode(two, NodeType::Viewport, 0, 0);
        CHECK(Has(ValidateGraph(two), "only be one viewport"));
    }
    SECTION("pictures, inputs and missing nodes")
    {
        Graph graph = Basic();
        const auto source = AddNode(graph, NodeType::VideoSource, 0, 0);
        graph.wires.push_back({source, "viewport", 0, {}});     // a video on the viewport (and a second wire there)
        graph.wires.push_back({"milkdrop-1", "nowhere", 0, {}});
        graph.wires.push_back({source, "milkdrop-1", 7, {}});     // no input 8
        graph.wires.push_back({"viewport", source, 0, {}});     // the viewport has no output
        graph.wires.push_back({source, "milkdrop-1", static_cast<int>(PresetLayer::Texture), {}}); // texture input without a name
        const auto errors = ValidateGraph(graph);
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
    SECTION("a switcher fed by a switcher")
    {
        Graph graph = Basic();
        const auto outer = AddNode(graph, NodeType::Switcher, 0, 0);
        const auto inner = AddNode(graph, NodeType::Switcher, 0, 0);
        Connect(graph, inner, outer, 0);
        Connect(graph, "milkdrop-1", inner, 0);
        Connect(graph, outer, "viewport", 0);
        CHECK(Has(ValidateGraph(graph), "can't be fed by another switcher"));

        // A video switcher upstream of a preset switcher, through the preset, is fine.
        Graph mixed = Basic();
        const auto frames = AddNode(mixed, NodeType::Switcher, 0, 0);
        const auto videos = AddNode(mixed, NodeType::Switcher, 0, 0);
        Connect(mixed, AddNode(mixed, NodeType::VideoSource, 0, 0), videos, 0);
        Connect(mixed, videos, "milkdrop-1", Feedback());
        Connect(mixed, "milkdrop-1", frames, 0);
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
        Connect(graph, gate, "milkdrop-1", Feedback());
        Connect(graph, b, "milkdrop-1", static_cast<int>(PresetLayer::OnTop));
        CHECK(Has(ValidateGraph(graph), "only one video at a time"));
    }
    SECTION("parameter ranges")
    {
        Graph graph = Basic();
        ParamsOf<ContrastParams>(*graph.Find(AddNode(graph, NodeType::Contrast, 0, 0))).contrast = 101.f;
        ParamsOf<OpacityParams>(*graph.Find(AddNode(graph, NodeType::Opacity, 0, 0))).opacity = -0.5f;
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
    const auto other = AddNode(graph, NodeType::MilkdropPreset, 0, 0);
    CHECK(Connect(graph, other, "viewport", 0)); // replaces preset-1's wire
    REQUIRE(graph.wires.size() == 1);
    CHECK(graph.wires[0].fromNode == other);
    CHECK_FALSE(RemoveNode(graph, "viewport"));
    CHECK(RemoveNode(graph, other));
    CHECK(graph.wires.empty());
    CHECK_FALSE(Disconnect(graph, "viewport", 0));

    const auto switcher = AddNode(graph, NodeType::Switcher, 0, 0);
    CHECK(InputCount(*graph.Find(switcher)) == 1);
    Connect(graph, "milkdrop-1", switcher, 0);
    CHECK(InputCount(*graph.Find(switcher)) == 2);
    Connect(graph, AddNode(graph, NodeType::MilkdropPreset, 0, 0), switcher, 1);
    CHECK(InputCount(*graph.Find(switcher)) == 3);
    Disconnect(graph, switcher, 0); // input numbers never change: input 1 stays wired
    CHECK(InputCount(*graph.Find(switcher)) == 3);
    CHECK(graph.Upstream(switcher, 1) != nullptr);
}

TEST_CASE("ConnectErrors checks a wire on a copy, as the canvas does while one is dragged")
{
    Graph graph = Basic();
    const auto source = AddNode(graph, NodeType::VideoSource, 0, 0);
    const auto gate = AddNode(graph, NodeType::LumaGate, 0, 0);
    const Graph before = graph;

    // Fine: video into an effect, and the effect into a preset input (replacing nothing).
    CHECK(ConnectErrors(graph, source, gate, 0).empty());
    CHECK(ConnectErrors(graph, gate, "milkdrop-1", Feedback()).empty());
    CHECK(graph == before); // the graph itself is never changed

    // Refused, with the reason: pictures not supported yet, loops, missing texture names, missing nodes.
    CHECK(Has(ConnectErrors(graph, "milkdrop-1", gate, 0), "works on a video"));
    CHECK(Has(ConnectErrors(graph, source, "milkdrop-1", static_cast<int>(PresetLayer::Texture)), "needs the name"));
    CHECK(Has(ConnectErrors(graph, "gone", gate, 0), "isn't there"));
    Connect(graph, source, gate, 0);
    CHECK(Has(ConnectErrors(graph, gate, gate, 0), "loop"));

    // An occupied input: the new wire replaces the old one.
    const auto other = AddNode(graph, NodeType::VideoSource, 0, 0);
    CHECK(ConnectErrors(graph, other, gate, 0).empty());
}

TEST_CASE("one picture kind: what the engine can't render yet is refused, each with its own message")
{
    Graph graph = Basic();
    const auto source = AddNode(graph, NodeType::VideoSource, 0, 0);
    const auto other = AddNode(graph, NodeType::MilkdropPreset, 0, 0);
    const auto gate = AddNode(graph, NodeType::LumaGate, 0, 0);
    const auto shader = AddNode(graph, NodeType::IsfShader, 0, 0);
    const auto switcher = AddNode(graph, NodeType::Switcher, 0, 0);
    Connect(graph, source, switcher, 0);
    auto messages = [](const std::vector<GraphError>& errors) {
        std::vector<std::string> result;
        for (const auto& error : errors)
            result.push_back(error.message);
        return result;
    };
    using Messages = std::vector<std::string>;

    CHECK(OutputKind(*graph.Find(source)) == PortKind::Picture);
    CHECK(InputKind(*graph.Find("viewport"), 0) == PortKind::Picture);
    CHECK(ConnectErrors(graph, source, "viewport", 0).empty()); // a video straight on the viewport
    CHECK(messages(ConnectErrors(graph, "milkdrop-1", other, Feedback())) ==
          Messages{"Milkdrop preset: its inputs take a video, not another preset (not supported yet)."});
    CHECK(messages(ConnectErrors(graph, "milkdrop-1", gate, 0)) ==
          Messages{"Luma gate: an effect works on a video, not on a preset's picture (not supported yet)."});
    CHECK(messages(ConnectErrors(graph, "milkdrop-1", shader, 0)) ==
          Messages{"ISF shader: an effect works on a video, not on a preset's picture (not supported yet)."});
    CHECK(messages(ConnectErrors(graph, "milkdrop-1", switcher, 1)) ==
          Messages{"Switcher: it switches videos or presets, not both (not supported yet)."});
    CHECK(messages(ConnectErrors(graph, switcher, AddNode(graph, NodeType::Switcher, 0, 0), 0)) ==
          Messages{"A switcher can't be fed by another switcher."});
    // A video switcher on the viewport too.
    CHECK(ConnectErrors(graph, switcher, "viewport", 0).empty());
}

TEST_CASE("one picture kind: every wire that was legal with two kinds still is")
{
    // What each end took or gave with two kinds: Video, Frame, or either (a switcher with nothing wired yet).
    enum class Old
    {
        Video,
        Frame,
        Either
    };
    struct End
    {
        const char* name;
        Old kind;
        bool switcher;
        std::string id; //!< In the graph below.
        int input{0};
    };
    Graph graph = Basic();
    auto add = [&](NodeType type, const std::string& from = {}) {
        const auto id = AddNode(graph, type, 0, 0);
        if (!from.empty())
            Connect(graph, from, id, 0);
        return id;
    };
    const auto source = add(NodeType::VideoSource);
    const auto preset = add(NodeType::MilkdropPreset);
    const std::vector<End> outputs{
        {"video source", Old::Video, false, source},
        {"effect", Old::Video, false, add(NodeType::Contrast, source)},
        {"shader", Old::Video, false, add(NodeType::IsfShader)},
        {"preset", Old::Frame, false, preset},
        {"video switcher", Old::Video, true, add(NodeType::Switcher, source)},
        {"preset switcher", Old::Frame, true, add(NodeType::Switcher, preset)},
        {"empty switcher", Old::Either, true, add(NodeType::Switcher)},
    };
    const auto shader = add(NodeType::IsfShader);
    ParamsOf<IsfShaderParams>(*graph.Find(shader)).wiredInputs = 1;
    const std::vector<End> inputs{
        {"viewport", Old::Frame, false, "viewport"},
        {"preset feedback", Old::Video, false, add(NodeType::MilkdropPreset), Feedback()},
        {"preset on top", Old::Video, false, add(NodeType::MilkdropPreset), static_cast<int>(PresetLayer::OnTop)},
        {"effect", Old::Video, false, add(NodeType::Saturation)},
        {"shader", Old::Video, false, shader},
        {"empty switcher", Old::Either, true, add(NodeType::Switcher)},
    };
    REQUIRE(ValidateGraph(graph).empty());
    for (const auto& out : outputs)
    {
        for (const auto& in : inputs)
        {
            const bool kindsFit = out.kind == in.kind || out.kind == Old::Either || in.kind == Old::Either;
            const bool wasLegal = kindsFit && !(out.switcher && in.switcher);
            const bool videoOnViewport = out.kind == Old::Video && in.id == "viewport"; // legal since one kind
            INFO(out.name << " -> " << in.name);
            CHECK(ConnectErrors(graph, out.id, in.id, in.input).empty() == (wasLegal || videoOnViewport));
        }
    }
}

TEST_CASE("number wires: only a Number output into a Number input; never part of a video's chain")
{
    Graph graph = Basic();
    const auto preset = graph.nodes[0].id;
    const auto video = AddNode(graph, NodeType::VideoSource, 0, 0);
    const auto opacity = AddNode(graph, NodeType::Opacity, 0, 0);
    const auto lfo = AddNode(graph, NodeType::Lfo, 0, 0);
    Connect(graph, video, opacity, 0);
    Connect(graph, opacity, preset, Feedback());
    REQUIRE(ValidateGraph(graph).empty());
    const int modulation = ParamInput(*graph.Find(opacity), 0);
    CHECK(modulation == 1);
    CHECK(InputKind(*graph.Find(opacity), modulation) == PortKind::Number);

    // An LFO into Opacity's modulation input.
    CHECK(ConnectErrors(graph, lfo, opacity, modulation).empty());
    // A number into a picture input, or a picture into a number input.
    CHECK(Has(ConnectErrors(graph, lfo, opacity, 0), "Opacity: this input takes a picture, not a number."));
    CHECK(Has(ConnectErrors(graph, lfo, preset, Feedback()), "takes a picture, not a number"));
    CHECK(Has(ConnectErrors(graph, lfo, "viewport", 0), "takes a picture, not a number"));
    CHECK(Has(ConnectErrors(graph, video, opacity, modulation), "Opacity: this input takes a number (from an LFO or an Envelope), not a picture."));

    // A wired number leaves the chain's start (the video) as it was.
    Connect(graph, lfo, opacity, modulation);
    CHECK(ChainStart(graph, graph.Find(opacity))->id == video);
    // Its depth is -1..1.
    graph.Find(opacity)->depths["opacity"] = 1.5f;
    CHECK(Has(ValidateGraph(graph), "Opacity: opacity depth must be between -1 and 1."));
}

TEST_CASE("audio wires: Audio only into Audio; an Envelope's number moves Opacity")
{
    Graph graph = Basic();
    const auto preset = graph.nodes[0].id;
    const auto video = AddNode(graph, NodeType::VideoSource, 0, 0);
    const auto opacity = AddNode(graph, NodeType::Opacity, 0, 0);
    const auto audio = AddNode(graph, NodeType::AudioIn, 0, 0);
    const auto envelope = AddNode(graph, NodeType::Envelope, 0, 0);
    const auto lfo = AddNode(graph, NodeType::Lfo, 0, 0);
    Connect(graph, video, opacity, 0);
    Connect(graph, opacity, preset, Feedback());
    REQUIRE(ValidateGraph(graph).empty());
    const int modulation = ParamInput(*graph.Find(opacity), 0);

    // Audio In -> Envelope -> Opacity's modulation input.
    CHECK(ConnectErrors(graph, audio, envelope, 0).empty());
    Connect(graph, audio, envelope, 0);
    CHECK(ConnectErrors(graph, envelope, opacity, modulation).empty());

    // Audio into a number or a picture input.
    CHECK(Has(ConnectErrors(graph, audio, opacity, modulation), "Opacity: this input takes a number (from an LFO or an Envelope), not audio."));
    CHECK(Has(ConnectErrors(graph, audio, opacity, 0), "Opacity: this input takes a picture, not audio."));
    CHECK(Has(ConnectErrors(graph, audio, preset, Feedback()), "takes a picture, not audio"));
    CHECK(Has(ConnectErrors(graph, audio, "viewport", 0), "takes a picture, not audio"));
    // A picture or a number into the Envelope's audio input.
    CHECK(Has(ConnectErrors(graph, video, envelope, 0), "Envelope: this input takes audio (from Audio In), not a picture."));
    CHECK(Has(ConnectErrors(graph, lfo, envelope, 0), "Envelope: this input takes audio (from Audio In), not a number."));
    // An Envelope's number isn't a picture either.
    CHECK(Has(ConnectErrors(graph, envelope, opacity, 0), "Opacity: this input takes a picture, not a number."));
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
        const auto back = gv::test::ClassicFromGraph(graph);
        REQUIRE(back.has_value());
        CHECK(*back == NormalizeClassic(classic));
        CHECK(GraphFromClassic(*back) == graph); // deterministic ids and layout
    }
}

TEST_CASE("the conversion follows the product doc's table")
{
    auto cases = Cases();
    SECTION("a look becomes Contrast -> Saturation -> Luma gate (softness 0.1) -> Opacity into Feedback")
    {
        const Graph graph = GraphFromClassic(cases[1]);
        const Node* mix = graph.Upstream("milkdrop-1", Feedback());
        REQUIRE(mix != nullptr);
        CHECK(mix->type == NodeType::Opacity);
        const Node* gate = graph.Upstream(mix->id, 0);
        REQUIRE(gate->type == NodeType::LumaGate);
        CHECK(ParamsOf<LumaGateParams>(*gate).softness == kFeedbackGateSoftness);
        const Node* saturation = graph.Upstream(gate->id, 0);
        CHECK(saturation->type == NodeType::Saturation);
        const Node* contrast = graph.Upstream(saturation->id, 0);
        CHECK(contrast->type == NodeType::Contrast);
        CHECK(graph.Upstream(contrast->id, 0)->type == NodeType::VideoSource);
        CHECK(graph.Upstream("milkdrop-1", static_cast<int>(PresetLayer::OnTop)) == nullptr);
    }
    SECTION("the overlay is a second chain from the same source into On top, with its own softness")
    {
        const Graph graph = GraphFromClassic(cases[2]);
        const Node* mix = graph.Upstream("milkdrop-1", static_cast<int>(PresetLayer::OnTop));
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
        CHECK_FALSE(SwitchesVideos(graph, *frames));
        CHECK(ParamsOf<SwitcherParams>(*frames).inputCount == 4); // three presets and the empty input
        CHECK(ParamsOf<SwitcherParams>(*frames).unit == IntervalUnit::Bars);
        int videoSwitchers = 0;
        for (const auto& node : graph.nodes)
        {
            if (SwitchesVideos(graph, node))
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
    CHECK(gv::test::ClassicFromGraph(graph).has_value());
    // An extra effect in the chain.
    const auto extra = AddNode(graph, NodeType::Contrast, 0, 0);
    const Node* mix = graph.Upstream("milkdrop-1", Feedback());
    const std::string mixId = mix->id;
    Connect(graph, mixId, extra, 0);
    Connect(graph, extra, "milkdrop-1", Feedback());
    CHECK_FALSE(gv::test::ClassicFromGraph(graph).has_value());

    // Nothing wired into the viewport: an empty classic scene.
    Graph empty;
    AddNode(empty, NodeType::Viewport, 0, 0);
    CHECK(gv::test::ClassicFromGraph(empty) == ClassicScene{});
}

TEST_CASE("a scene's preset entries come from its graph, whatever its shape")
{
    ClassicScene classic;
    classic.playlist = {true, {"/a.milk", "/b.milk"}, {VideoLook{}, VideoLook{}}, IntervalUnit::Seconds, 10, 4, true};
    classic.video.enabled = true;
    classic.video.playlist = {true, {"x", "y"}, IntervalUnit::Seconds, 12, 4, false};
    Graph graph = GraphFromClassic(classic);
    REQUIRE(gv::test::ClassicFromGraph(graph).has_value());

    // Every chain moves from the feedback input into a texture input: no longer a classic scene.
    for (auto& wire : graph.wires)
    {
        const Node* to = graph.Find(wire.toNode);
        if (to != nullptr && to->type == NodeType::MilkdropPreset && wire.toInput == static_cast<int>(PresetLayer::Feedback))
        {
            wire.toInput = static_cast<int>(PresetLayer::Texture);
            wire.textureName = "worms";
        }
    }
    CHECK(ValidateGraph(graph).empty());
    CHECK_FALSE(gv::test::ClassicFromGraph(graph).has_value());

    Scene scene;
    scene.graph = graph;
    scene.Refresh();
    CHECK(scene.SwitchesPresets());
    CHECK(scene.Presets() == std::vector<std::filesystem::path>{"/a.milk", "/b.milk"});
    CHECK(scene.presetEntries[1].input == 1);
    CHECK(scene.EntryOfInput(1) == 1);
    CHECK(scene.EntryOfInput(7) == -1);

    // A preset straight into the viewport: one entry, no switcher.
    scene.graph = Basic();
    scene.Refresh();
    CHECK_FALSE(scene.SwitchesPresets());
    CHECK(scene.FirstPreset() == "/a.milk");
    CHECK(scene.presetEntries[0].input == -1);

    // Nothing reaches the viewport: one empty entry.
    Graph empty;
    AddNode(empty, NodeType::Viewport, 0, 0);
    scene.graph = empty;
    scene.Refresh();
    REQUIRE(scene.presetEntries.size() == 1);
    CHECK(scene.FirstPreset().empty());
    CHECK(scene.presetEntries[0].node.empty());
}
