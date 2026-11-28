// The math nodes (Sum, Difference, Multiply, Divide, Log, Absolute; ticket C8): model/nodes/MathNode.cpp,
// app/nodes/MathNode.cpp. One node type, Math, with its operation in MathParams::op.

#include "nodes/NodeTestKit.hpp"

#include "model/nodes/MathNode.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>

using namespace gv;
using Catch::Matchers::WithinAbs;
using model::MathOp;
using model::MathParams;
using model::NodeType;

namespace {

auto Of(MathOp op, std::vector<float> values) -> MathParams
{
    MathParams params = model::MathDefaults(op);
    params.values = std::move(values);
    return params;
}

auto Answer(MathOp op, std::vector<float> values) -> std::optional<float>
{
    return model::MathAnswerOf(Of(op, std::move(values))).value;
}

auto AddMath(test::NodeScene& scene, MathOp op) -> std::string
{
    return scene.Add(NodeType::Math, [op](model::Node& node) { node.params = model::MathDefaults(op); });
}

auto Values(test::NodeScene& scene, const std::string& id) -> const std::vector<float>&
{
    return model::ParamsOf<MathParams>(scene.Node(id)).values;
}

} // namespace

TEST_CASE("math: each operation starts with its inputs at its identity, and is named after it", "[node:math]")
{
    CHECK(model::MathDefaults(MathOp::Sum).values == std::vector<float>{0.f, 0.f});
    CHECK(model::MathDefaults(MathOp::Difference).values == std::vector<float>{0.f, 0.f});
    CHECK(model::MathDefaults(MathOp::Multiply).values == std::vector<float>{1.f, 1.f});
    CHECK(model::MathDefaults(MathOp::Divide).values == std::vector<float>{1.f, 1.f});
    CHECK(model::MathDefaults(MathOp::Log).values == std::vector<float>{0.f});
    CHECK(model::MathDefaults(MathOp::Absolute).values == std::vector<float>{0.f});
    // The identity changes nothing: a new Multiply passes its wired input straight through.
    CHECK(Answer(MathOp::Multiply, {0.4f, 1.f}) == 0.4f);
    CHECK(Answer(MathOp::Sum, {0.4f, 0.f}) == 0.4f);

    test::NodeScene scene;
    const auto& node = scene.Node(AddMath(scene, MathOp::Divide));
    CHECK(node.id == "math-1");
    CHECK(model::Label(node) == "Divide");
    CHECK(model::OutputKind(node) == model::PortKind::Number);
    CHECK(model::InputCount(node) == 2);
    CHECK(model::InputKind(node, 1) == model::PortKind::Number);
}

TEST_CASE("math: the maths, unclamped", "[node:math]")
{
    CHECK(Answer(MathOp::Sum, {0.75f, 0.75f, 0.5f}) == 2.f); // past 1: the setting it moves clamps it
    CHECK(Answer(MathOp::Difference, {0.25f, 1.f}) == -0.75f);
    CHECK_THAT(*Answer(MathOp::Multiply, {0.5f, 0.3f, 2.f}), WithinAbs(0.3, 1e-6));
    CHECK(Answer(MathOp::Divide, {0.5f, 0.25f}) == 2.f);
    CHECK(Answer(MathOp::Absolute, {-0.6f}) == 0.6f);
    CHECK(Answer(MathOp::Absolute, {0.6f}) == 0.6f);
}

TEST_CASE("math: Log bends 0..1 onto 0..1, softer at the top; 0 curve is a straight line", "[node:math]")
{
    for (float x : {0.f, 0.1f, 0.5f, 0.9f, 1.f})
        CHECK_THAT(model::LogCurve(x, 0.f), WithinAbs(x, 1e-6));
    for (float curve : {0.25f, 0.5f, 1.f})
    {
        INFO("curve " << curve);
        CHECK(model::LogCurve(0.f, curve) == 0.f);
        CHECK_THAT(model::LogCurve(1.f, curve), WithinAbs(1.0, 1e-6));
        CHECK(model::LogCurve(0.5f, curve) > 0.5f);
        float last = 0.f;
        for (int i = 1; i <= 20; ++i) // rising all the way, and less near the top than near the bottom
        {
            const float y = model::LogCurve(static_cast<float>(i) / 20.f, curve);
            CHECK(y > last);
            last = y;
        }
        CHECK(model::LogCurve(1.f, curve) - model::LogCurve(0.9f, curve) < model::LogCurve(0.1f, curve) - model::LogCurve(0.f, curve));
    }
    CHECK(model::LogCurve(0.5f, 1.f) > model::LogCurve(0.5f, 0.5f)); // a stronger curve bends more
    MathParams log = Of(MathOp::Log, {3.f});
    CHECK(model::MathAnswerOf(log).value == 1.f); // above 1 counts as 1
    log.values = {0.f};
    CHECK(model::MathAnswerOf(log).value == 0.f); // 0 is on the curve: a square LFO into a Log doesn't stick
}

TEST_CASE("math: dividing by 0, Log below 0 and inputs that aren't numbers hold the last value, never NaN or inf", "[node:math]")
{
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float big = std::numeric_limits<float>::max();
    struct Case
    {
        MathOp op;
        std::vector<float> values;
        const char* note;
    };
    const Case cases[] = {
        {MathOp::Divide, {0.5f, 0.f}, "Dividing by zero: holding the last value."},
        {MathOp::Divide, {0.5f, -0.f}, "Dividing by zero: holding the last value."},
        {MathOp::Log, {-0.1f}, "Below zero: holding the last value."},
        {MathOp::Sum, {inf, 0.f}, "An input isn't a number: holding the last value."},
        {MathOp::Absolute, {nan}, "An input isn't a number: holding the last value."},
        {MathOp::Log, {nan}, "An input isn't a number: holding the last value."},
        {MathOp::Multiply, {big, big}, "The answer is too big: holding the last value."},
        {MathOp::Sum, {big, big}, "The answer is too big: holding the last value."},
        {MathOp::Divide, {big, 1e-30f}, "The answer is too big: holding the last value."},
    };
    for (const auto& c : cases)
    {
        INFO(model::MathOpName(c.op) << " of " << c.values[0]);
        const auto answer = model::MathAnswerOf(Of(c.op, c.values));
        CHECK_FALSE(answer.value);
        CHECK(std::string(answer.holding) == c.note);

        // The node's value: last frame's, or 0 before it had one.
        test::NodeScene scene;
        const std::string id = scene.Add(NodeType::Math, [&](model::Node& node) { node.params = Of(c.op, c.values); });
        model::SignalInputs inputs;
        CHECK(model::EvaluateSignals(scene.Graph(), inputs).at(id) == 0.f);
        inputs.previous[id] = 0.42f;
        CHECK(model::EvaluateSignals(scene.Graph(), inputs).at(id) == 0.42f);
        inputs.previous[id] = nan;
        CHECK(model::EvaluateSignals(scene.Graph(), inputs).at(id) == 0.f);
    }
    CHECK(model::MathAnswerOf(Of(MathOp::Divide, {0.5f, 0.25f})).holding == nullptr);
}

TEST_CASE("math: Sum and Multiply grow an input as the last is wired, and shrink back", "[node:math]")
{
    test::NodeScene scene;
    const std::string sum = AddMath(scene, MathOp::Sum);
    const std::string product = AddMath(scene, MathOp::Multiply);
    const std::string lfo = scene.Add(NodeType::Lfo);
    model::Connect(scene.Graph(), lfo, sum, 0);
    CHECK(Values(scene, sum).size() == 2); // a is wired, b is the empty one
    model::Connect(scene.Graph(), lfo, sum, 1);
    CHECK(Values(scene, sum) == std::vector<float>{0.f, 0.f, 0.f});
    model::Connect(scene.Graph(), lfo, sum, 2);
    CHECK(model::InputCount(scene.Node(sum)) == 4);
    CHECK(scene.Graph().WireInto(sum, 2)->param == "c");
    model::Disconnect(scene.Graph(), sum, 1);
    CHECK(Values(scene, sum).size() == 4); // c is still wired, and keeps its place
    model::Disconnect(scene.Graph(), sum, 2);
    CHECK(Values(scene, sum).size() == 2);
    CHECK(scene.Graph().WireInto(sum, 0) != nullptr);

    model::Connect(scene.Graph(), lfo, product, 1);
    CHECK(Values(scene, product) == std::vector<float>{1.f, 1.f, 1.f}); // the new input is at Multiply's identity

    // Never more than kMaxMathInputs; the fixed ones never grow.
    for (int i = 0; i < model::kMaxMathInputs; ++i)
        model::Connect(scene.Graph(), lfo, sum, i);
    CHECK(Values(scene, sum).size() == static_cast<size_t>(model::kMaxMathInputs));
    const std::string divide = AddMath(scene, MathOp::Divide);
    model::Connect(scene.Graph(), lfo, divide, 1);
    CHECK(Values(scene, divide).size() == 2);
    CHECK(model::ValidateGraph(scene.Graph()).empty());
}

TEST_CASE("math: chains run in wire order, LFO -> Multiply (0.3) -> Sum (+0.5) -> Contrast", "[node:math]")
{
    test::NodeScene scene;
    // Added last-first, so the order of the nodes isn't the order they're worked out in.
    const std::string contrast = scene.Add(NodeType::Contrast, [](model::Node& n) { n.depths["contrast"] = 0.1f; });
    const std::string sum = AddMath(scene, MathOp::Sum);
    const std::string multiply = AddMath(scene, MathOp::Multiply);
    const std::string lfo = scene.Add(NodeType::Lfo);
    model::Connect(scene.Graph(), lfo, multiply, 0);
    scene.Node(multiply).params = Of(MathOp::Multiply, {1.f, 0.3f});
    model::Connect(scene.Graph(), multiply, sum, 0);
    scene.Node(sum).params = Of(MathOp::Sum, {0.f, 0.5f});
    model::Connect(scene.Graph(), sum, contrast, model::ParamInput(scene.Node(contrast), 0));
    REQUIRE(model::ValidateGraph(scene.Graph()).empty());

    const auto values = model::EvaluateSignals(scene.Graph(), {std::nullopt, 0.5}); // a 2 s sine at its top: 1
    CHECK_THAT(values.at(lfo), WithinAbs(1.0, 1e-5));
    CHECK_THAT(values.at(multiply), WithinAbs(0.3, 1e-5)); // the wire's value, not moved around a slider
    CHECK_THAT(values.at(sum), WithinAbs(0.8, 1e-5));
    const auto& node = scene.Node(contrast);
    const float moved = model::ParamsOf<model::ContrastParams>(model::ModulatedNode(scene.Graph(), node, values)).contrast;
    CHECK_THAT(moved, WithinAbs(model::Modulate(node, 0, values.at(sum)), 1e-5));
    CHECK(moved > model::ParamsOf<model::ContrastParams>(node).contrast);
}

TEST_CASE("math: saved and read back, wires on grown inputs included; bad files are reported", "[node:math]")
{
    test::NodeScene scene;
    const std::string sum = AddMath(scene, MathOp::Sum);
    const std::string log = AddMath(scene, MathOp::Log);
    const std::string lfo = scene.Add(NodeType::Lfo);
    model::Connect(scene.Graph(), lfo, sum, 0);
    model::Connect(scene.Graph(), lfo, sum, 1);
    model::Connect(scene.Graph(), sum, log, 0);
    scene.Node(sum).params = Of(MathOp::Sum, {0.f, 0.f, 0.25f});
    scene.Node(log).params = Of(MathOp::Log, {0.f});
    model::ParamsOf<MathParams>(scene.Node(log)).curve = 0.8f;
    model::Connect(scene.Graph(), scene.Add(NodeType::Lfo), log, 1); // the curve's own modulation input
    for (const auto& id : {sum, log})
        CHECK(test::RoundTrip(scene.Node(id)) == scene.Node(id));
    const auto loaded = model::ParsePerformance(model::SerializePerformance(scene.performance));
    REQUIRE(loaded.ok);
    CHECK(loaded.performance.scenes.at(0).graph == scene.Graph());

    const auto [read, issues] = test::ReadNode(NodeType::Math, {{"op", "modulo"}});
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "/node/op");
    CHECK(test::ReadNode(NodeType::Math, {{"op", "multiply"}}).first.params == model::NodeParams{model::MathDefaults(MathOp::Multiply)});
    CHECK(test::ParamErrors(NodeType::Math, [](model::Node& n) { n.params = Of(MathOp::Divide, {1.f, 2.f, 3.f}); }) ==
          std::vector<std::string>{"Divide: it has the wrong number of inputs."});
    CHECK(test::ParamErrors(NodeType::Math, [](model::Node& n) { n.params = Of(MathOp::Sum, {11.f, 0.f}); }) ==
          std::vector<std::string>{"Sum: an input's number must be between -10 and 10."});
}

TEST_CASE("math: on the canvas, a number on each input, the value, and a note while holding", "[node:math]")
{
    test::NodeScene scene;
    const std::string divide = AddMath(scene, MathOp::Divide);
    const std::string lfo = scene.Add(NodeType::Lfo);
    model::Connect(scene.Graph(), lfo, divide, 1);
    test::FakeNodeContext context(scene);
    context.signals = {{lfo, 0.f}, {divide, 0.5f}};
    CHECK(app::NodeTitle(scene.Node(divide), context) == "Divide");

    test::HeadlessUi ui;
    ui.Frame(context, divide);
    CHECK(context.paramPins == std::vector<app::InputSpec>{{0, {}, model::PortKind::Number, "a"}, {1, {}, model::PortKind::Number, "b"}});
    CHECK(context.edits.empty());
    // b is wired: no depth, its number comes from the wire.
    CHECK_FALSE(context.itemRects.contains("How far the wire moves the setting either side of the slider (negative: the other way)."));
    auto note = [&] {
        for (const auto& control : app::NodeControls(scene.Node(divide), context))
        {
            if (control.kind == app::Control::Kind::Text)
                return control.text(scene.Node(divide), context);
        }
        return std::string("(none)");
    };
    CHECK(note() == "Dividing by zero: holding the last value.");
    context.signals[lfo] = 0.5f;
    CHECK(note().empty());

    // An unwired input's slider sets its number, from -10 to 10.
    const auto sliders = app::NodeControls(scene.Node(divide), context);
    REQUIRE(sliders.at(0).param == 0);
    CHECK(sliders[0].min == -10.0);
    CHECK(sliders[0].max == 10.0);

    // Every operation is in the Add menu, as a valid node of its own.
    std::vector<std::string> labels;
    for (const auto& item : app::Ui(NodeType::Math).addItems)
    {
        labels.emplace_back(item.label);
        model::Node node;
        node.type = NodeType::Math;
        item.configure(node);
        CHECK(model::Label(node) == item.label);
    }
    CHECK(labels == std::vector<std::string>{"Sum", "Difference", "Multiply", "Divide", "Log", "Absolute"});
}
