// The LFO node: model/nodes/LfoNode.cpp, app/nodes/LfoNode.cpp. Driving Opacity: tests/nodes/test_opacity.cpp and
// tests/gl/test_effect_nodes.cpp.

#include "nodes/NodeTestKit.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace gv;
using Catch::Matchers::WithinAbs;
using model::LfoShape;
using model::NodeType;

namespace {

auto Lfo(LfoShape shape, float phase = 0.f) -> model::Node
{
    test::NodeScene scene;
    return scene.Node(scene.Add(NodeType::Lfo, [&](model::Node& node) {
        model::ParamsOf<model::LfoParams>(node).shape = shape;
        model::ParamsOf<model::LfoParams>(node).phase = phase;
    }));
}

/// The LFO's value at `beats` into a running clock's song.
auto AtBeat(const model::Node& node, double beats) -> float
{
    return model::Def(NodeType::Lfo).signal(node, {beats, 999.0});
}

} // namespace

TEST_CASE("lfo: a Number output, no inputs; a sine every beat, or every 2 s with no clock", "[node:lfo]")
{
    const auto node = Lfo(LfoShape::Sine);
    CHECK(node.id == "lfo-1");
    CHECK(model::OutputKind(node) == model::PortKind::Number);
    CHECK(model::InputCount(node) == 0);
    CHECK(model::ParamsOf<model::LfoParams>(node) == model::LfoParams{LfoShape::Sine, 1.f, 0.f, 2.0});
}

TEST_CASE("lfo: each shape at known points of its cycle", "[node:lfo]")
{
    CHECK_THAT(AtBeat(Lfo(LfoShape::Sine), 0.0), WithinAbs(0.5, 1e-5));
    CHECK_THAT(AtBeat(Lfo(LfoShape::Sine), 0.25), WithinAbs(1.0, 1e-5));
    CHECK_THAT(AtBeat(Lfo(LfoShape::Sine), 0.75), WithinAbs(0.0, 1e-5));
    CHECK_THAT(AtBeat(Lfo(LfoShape::Triangle), 0.0), WithinAbs(0.0, 1e-5));
    CHECK_THAT(AtBeat(Lfo(LfoShape::Triangle), 0.25), WithinAbs(0.5, 1e-5));
    CHECK_THAT(AtBeat(Lfo(LfoShape::Triangle), 0.5), WithinAbs(1.0, 1e-5));
    CHECK_THAT(AtBeat(Lfo(LfoShape::Saw), 0.75), WithinAbs(0.75, 1e-5));
    CHECK(AtBeat(Lfo(LfoShape::Square), 0.25) == 1.f);
    CHECK(AtBeat(Lfo(LfoShape::Square), 0.75) == 0.f);
    // Phase shifts the cycle; whole cycles later it's the same.
    CHECK(AtBeat(Lfo(LfoShape::Square, 0.5f), 0.25) == 0.f);
    CHECK_THAT(AtBeat(Lfo(LfoShape::Saw, 0.25f), 3.25), WithinAbs(0.5, 1e-5));
}

TEST_CASE("lfo: follows the clock's song position in beats, and its fallback period without one", "[node:lfo]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::Lfo, [](model::Node& node) {
        auto& params = model::ParamsOf<model::LfoParams>(node);
        params.shape = LfoShape::Saw;
        params.beats = 4.f;
        params.periodSeconds = 10.0;
    });
    // With a clock: one cycle every 4 beats, whatever the time.
    CHECK_THAT(model::EvaluateSignals(scene.Graph(), {1.0, 3.0}).at(id), WithinAbs(0.25, 1e-5));
    CHECK_THAT(model::EvaluateSignals(scene.Graph(), {6.0, 3.0}).at(id), WithinAbs(0.5, 1e-5));
    // Without: one cycle every 10 seconds.
    CHECK_THAT(model::EvaluateSignals(scene.Graph(), {std::nullopt, 3.0}).at(id), WithinAbs(0.3, 1e-5));
    CHECK_THAT(model::EvaluateSignals(scene.Graph(), {std::nullopt, 17.5}).at(id), WithinAbs(0.75, 1e-5));
    // Only Number outputs have a value.
    CHECK(model::EvaluateSignals(scene.Graph(), {}).size() == 1);
}

TEST_CASE("lfo: its settings' ranges", "[node:lfo]")
{
    auto set = [](float beats, float phase, double period) {
        return [=](model::Node& node) {
            auto& params = model::ParamsOf<model::LfoParams>(node);
            params.beats = beats;
            params.phase = phase;
            params.periodSeconds = period;
        };
    };
    CHECK(test::ParamErrors(NodeType::Lfo, set(0.25f, 1.f, 60.0)).empty());
    CHECK(test::ParamErrors(NodeType::Lfo, set(3.f, 0.f, 2.0)) == std::vector<std::string>{"LFO: a cycle is 1/4, 1/2, 1, 2, 4 or 8 beats."});
    CHECK(test::ParamErrors(NodeType::Lfo, set(1.f, 1.5f, 0.0)) ==
          std::vector<std::string>{"LFO: phase must be between 0 and 1.", "LFO: the cycle without a clock must be between 0.1 and 60 seconds."});
}

TEST_CASE("lfo: saved and read back", "[node:lfo]")
{
    test::NodeScene scene;
    auto& node = scene.Node(scene.Add(NodeType::Lfo, [](model::Node& n) {
        model::ParamsOf<model::LfoParams>(n) = {LfoShape::Square, 0.5f, 0.25f, 7.5};
    }));
    CHECK(test::RoundTrip(node) == node);
    const auto [read, issues] = test::ReadNode(NodeType::Lfo, {{"shape", "triangle"}, {"beats", 8}});
    CHECK(model::ParamsOf<model::LfoParams>(read).shape == LfoShape::Triangle);
    CHECK(model::ParamsOf<model::LfoParams>(read).beats == 8.f);
    CHECK(issues.empty());
    CHECK(test::ReadNode(NodeType::Lfo, {{"shape", "wobble"}}).second.size() == 1);
}

TEST_CASE("lfo: on the canvas, its shape, rate, phase and period, and its value as a bar", "[node:lfo]")
{
    CHECK(test::ControlById(NodeType::Lfo, "##beats").options.size() == std::size(model::kLfoBeats));
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::Lfo);
    test::FakeNodeContext context(scene);
    context.signals[id] = 0.75f;
    test::HeadlessUi ui;
    ui.Frame(context, id);
    CHECK(context.edits.empty());
    CHECK(context.itemRects.count("The LFO's value now, from 0 to 1. Wire it into a setting's diamond to move that setting.") == 1);
}
