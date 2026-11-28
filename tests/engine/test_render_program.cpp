// E3a (#161): the render program compiles a scene's presets into ordered steps, each input naming its feed.
#include "engine/RenderPlan.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace gv::engine;
using namespace gv::model;

namespace {

constexpr int kFeedback = static_cast<int>(PresetLayer::Feedback);
constexpr int kOnTop = static_cast<int>(PresetLayer::OnTop);
constexpr int kTexture = static_cast<int>(PresetLayer::Texture);

/// A graph with a viewport and the preset on it.
auto WithTop(Graph& graph) -> std::string
{
    AddNode(graph, NodeType::Viewport, 0, 0);
    const auto top = AddNode(graph, NodeType::MilkdropPreset, 0, 0);
    REQUIRE(Connect(graph, top, "viewport", 0));
    return top;
}

/// The order of the program's presets, by node id.
auto Order(const RenderProgram& program) -> std::vector<std::string>
{
    std::vector<std::string> order;
    for (const auto& step : program.steps)
        order.push_back(step.presetNode);
    return order;
}

} // namespace

TEST_CASE("render program: A into B's feedback puts A first, and B's feedback names A's step")
{
    Graph graph;
    const auto b = WithTop(graph);
    const auto a = AddNode(graph, NodeType::MilkdropPreset, 0, 0);
    REQUIRE(Connect(graph, a, b, kFeedback));

    const auto program = CompileProgram(graph);
    CHECK(program.errors.empty());
    REQUIRE(Order(program) == std::vector<std::string>{a, b});
    CHECK(program.steps[0].inputs.empty());
    REQUIRE(program.steps[1].inputs.size() == 1);
    const auto& input = program.steps[1].inputs[0];
    CHECK(input.layer == PresetLayer::Feedback);
    CHECK(input.feed == Feed::Step);
    CHECK(input.step == 0);
    CHECK(program.Top()->presetNode == b);

    // The lane still draws only the top preset, with no video into it.
    CHECK(LanePlanOf(program, "key") == LanePlan{{}, false, "key"});
}

TEST_CASE("render program: A into one of B's textures, by name")
{
    Graph graph;
    const auto b = WithTop(graph);
    const auto a = AddNode(graph, NodeType::MilkdropPreset, 0, 0);
    REQUIRE(Connect(graph, a, b, kTexture, "worms"));

    const auto program = CompileProgram(graph);
    REQUIRE(Order(program) == std::vector<std::string>{a, b});
    REQUIRE(program.steps[1].inputs.size() == 1);
    CHECK(program.steps[1].inputs[0] == StepInput{PresetLayer::Texture, "worms", Feed::Step, {}, 0});
}

TEST_CASE("render program: A and B both into C, upstream first and ties by node id")
{
    Graph graph;
    const auto c = WithTop(graph);                              // preset-1
    const auto b = AddNode(graph, NodeType::MilkdropPreset, 0, 0); // preset-2
    const auto a = AddNode(graph, NodeType::MilkdropPreset, 0, 0); // preset-3
    // In input order a comes first, but b's id sorts first.
    REQUIRE(Connect(graph, a, c, kFeedback));
    REQUIRE(Connect(graph, b, c, kOnTop));

    const auto program = CompileProgram(graph);
    REQUIRE(Order(program) == std::vector<std::string>{b, a, c});
    REQUIRE(program.steps[2].inputs.size() == 2);
    CHECK(program.steps[2].inputs[0].layer == PresetLayer::Feedback);
    CHECK(program.steps[2].inputs[0].step == 1);
    CHECK(program.steps[2].inputs[1].layer == PresetLayer::OnTop);
    CHECK(program.steps[2].inputs[1].step == 0);
    CHECK(CompileProgram(graph) == program); // the same every time

    // A chain: a into b, so b now waits for a.
    REQUIRE(Connect(graph, a, b, kFeedback));
    CHECK(Order(CompileProgram(graph)) == std::vector<std::string>{a, b, c});
}

TEST_CASE("render program: a video into A and into B, each with its effects and modulations")
{
    Graph graph;
    const auto b = WithTop(graph);
    const auto a = AddNode(graph, NodeType::MilkdropPreset, 0, 0);
    REQUIRE(Connect(graph, a, b, kFeedback));
    const auto source = AddNode(graph, NodeType::VideoSource, 0, 0);
    const auto opacity = AddNode(graph, NodeType::Opacity, 0, 0);
    REQUIRE(Connect(graph, source, opacity, 0));
    REQUIRE(Connect(graph, opacity, a, kFeedback));
    REQUIRE(Connect(graph, source, b, kOnTop));
    const auto lfo = AddNode(graph, NodeType::Lfo, 0, 0);
    REQUIRE(Connect(graph, lfo, opacity, ParamInput(*graph.Find(opacity), 0)));
    const auto audio = AddNode(graph, NodeType::AudioIn, 0, 0);
    REQUIRE(Connect(graph, audio, b, kPresetAudioInput));

    const auto program = CompileProgram(graph);
    REQUIRE(Order(program) == std::vector<std::string>{a, b});
    CHECK(program.sourceNode == source);

    // A hears nothing; its feedback is the video through the LFO-moved opacity.
    CHECK(program.steps[0].audio.empty());
    REQUIRE(program.steps[0].inputs.size() == 1);
    const auto& intoA = program.steps[0].inputs[0];
    CHECK(intoA.feed == Feed::Video);
    REQUIRE(intoA.video.size() == 1);
    CHECK(intoA.video[0].effects[0].type == NodeType::Opacity);
    CHECK(HasModulations(intoA.video));

    // B hears the Audio In; its feedback is A, its on-top the plain video.
    CHECK(program.steps[1].audio == audio);
    REQUIRE(program.steps[1].inputs.size() == 2);
    CHECK(program.steps[1].inputs[0].feed == Feed::Step);
    CHECK(program.steps[1].inputs[0].step == 0);
    CHECK(program.steps[1].inputs[1] == StepInput{PresetLayer::OnTop, {}, Feed::Video, {}, -1});

    // The lane draws the top preset's video inputs, as the flat plan has them.
    const auto lane = LanePlanOf(program, "key");
    CHECK(lane.branches == std::vector<VideoBranch>{{{}, PresetLayer::OnTop, {}}});
    CHECK(lane.branches == CompilePlan(graph).branches);
}

TEST_CASE("render program: an unfinished input is fed by nothing; a single preset matches the flat plan")
{
    Graph graph;
    const auto top = WithTop(graph);
    REQUIRE(Connect(graph, AddNode(graph, NodeType::Contrast, 0, 0), top, kFeedback));
    const auto source = AddNode(graph, NodeType::VideoSource, 0, 0);
    REQUIRE(Connect(graph, source, top, kOnTop));

    const auto program = CompileProgram(graph);
    REQUIRE(program.steps.size() == 1);
    REQUIRE(program.steps[0].inputs.size() == 2);
    CHECK(program.steps[0].inputs[0].feed == Feed::Nothing);
    CHECK(program.steps[0].inputs[1].feed == Feed::Video);
    const auto plan = CompilePlan(graph);
    CHECK(plan.presetNode == top);
    CHECK(plan.sourceNode == source);
    CHECK(LanePlanOf(program, {}).branches == plan.branches);
}

TEST_CASE("render program: a loop of presets is refused in plain words")
{
    Graph graph;
    const auto b = WithTop(graph);
    const auto a = AddNode(graph, NodeType::MilkdropPreset, 0, 0);
    REQUIRE(Connect(graph, a, b, kFeedback));
    REQUIRE(Connect(graph, b, a, kFeedback));

    const auto program = CompileProgram(graph);
    CHECK(program.steps.empty());
    REQUIRE(program.errors.size() == 1);
    CHECK(program.errors[0].nodeId == b);
    CHECK(program.errors[0].message.find("loop") != std::string::npos);
    CHECK(CompilePlan(graph) == RenderPlan{});

    // A preset into its own input is a loop too.
    Graph self;
    const auto only = WithTop(self);
    REQUIRE(Connect(self, only, only, kOnTop));
    CHECK(CompileProgram(self).errors.size() == 1);
}
