// What every node type must have, checked for each one. A new node type (a NodeType, its NodeDef in
// model/nodes/, its NodeUi in app/nodes/) fails here until it's complete, has its own tests in
// tests/nodes/test_<type>.cpp and, if it's an effect, a shader test in tests/gl/test_effect_nodes.cpp.

#include "nodes/NodeTestKit.hpp"

#include "model/PerformanceJson.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

using namespace gv;
using model::NodeType;

namespace {

auto AllTypes() -> std::vector<NodeType>
{
    std::vector<NodeType> types;
    for (int i = 0; i < model::kNodeTypeCount; ++i)
        types.push_back(static_cast<NodeType>(i));
    return types;
}

/// "lumaGate" -> "luma_gate"
auto Snake(const std::string& camel) -> std::string
{
    std::string snake;
    for (char c : camel)
    {
        if (std::isupper(static_cast<unsigned char>(c)))
        {
            snake += '_';
            snake += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        else
        {
            snake += c;
        }
    }
    return snake;
}

/// A graph with a node of `type` (default settings) and, unless it's the viewport, the viewport.
struct Lone
{
    explicit Lone(NodeType type)
    {
        if (type == NodeType::Viewport)
            id = "viewport";
        else
            id = scene.Add(type);
    }
    test::NodeScene scene;
    std::string id;
};

} // namespace

TEST_CASE("every node type has a definition and a canvas definition, at its own index", "[node-types]")
{
    CHECK(model::NodeDefs().size() == static_cast<size_t>(model::kNodeTypeCount));
    CHECK(app::NodeUis().size() == static_cast<size_t>(model::kNodeTypeCount));
    for (NodeType type : AllTypes())
    {
        CHECK(model::Def(type).type == type);
        CHECK(app::Ui(type).type == type);
    }
}

TEST_CASE("every node type has its own names, a unique show-file name, and its own kind of settings", "[node-types]")
{
    std::set<std::string> jsonNames;
    std::set<std::string> prefixes;
    std::set<size_t> settings;
    for (NodeType type : AllTypes())
    {
        const auto& def = model::Def(type);
        INFO(def.displayName);
        CHECK(std::string(def.displayName).size() > 0);
        CHECK(jsonNames.insert(def.jsonName).second);
        CHECK(prefixes.insert(def.idPrefix).second);
        CHECK(settings.insert(def.defaults.index()).second);
        CHECK(model::NodeTypeFromJson(def.jsonName) == type);
    }
}

TEST_CASE("every node type has a test file", "[node-types]")
{
    const std::string files = GV_NODE_TEST_FILES;
    for (NodeType type : AllTypes())
    {
        const std::string expected = "nodes/test_" + Snake(model::Def(type).jsonName) + ".cpp";
        INFO("expected " << expected << " in GV_NODE_TESTS (tests/CMakeLists.txt)");
        CHECK(files.find(expected) != std::string::npos);
    }
}

TEST_CASE("every node type's ports are consistent", "[node-types]")
{
    for (NodeType type : AllTypes())
    {
        const auto& def = model::Def(type);
        INFO(def.displayName);
        Lone lone(type);
        const model::Node& node = lone.scene.Node(lone.id);
        const int inputs = model::InputCount(node);
        CHECK(inputs >= 0);
        if (inputs > 0)
            CHECK(def.inputKind != nullptr);
        for (int i = 0; i < inputs; ++i)
            CHECK(model::InputKind(node, i).has_value());
        CHECK_FALSE(model::InputKind(node, inputs).has_value());
        CHECK_FALSE(model::InputKind(node, -1).has_value());
        CHECK((def.inputNames.empty() || def.inputNames.size() == static_cast<size_t>(inputs)));
        CHECK(def.textureInput < inputs);
        CHECK((def.output != nullptr || inputs > 0)); // a node with neither does nothing
    }
}

TEST_CASE("every effect is one video in, one video out, with a pass and a shader; nothing else has those", "[node-types]")
{
    for (NodeType type : AllTypes())
    {
        const auto& def = model::Def(type);
        INFO(def.displayName);
        Lone lone(type);
        const model::Node& node = lone.scene.Node(lone.id);
        if (def.effect)
        {
            CHECK(model::OutputKind(node) == model::PortKind::Video);
            CHECK(model::InputCount(node) == 1);
            CHECK(model::InputKind(node, 0) == model::PortKind::Video);
            REQUIRE(def.pass != nullptr);
            CHECK(def.pass(node).type == type);
            CHECK(def.shader != nullptr);
        }
        else
        {
            CHECK(def.pass == nullptr);
            CHECK(def.shader == nullptr);
        }
    }
}

TEST_CASE("every node type is valid with its default settings", "[node-types]")
{
    for (NodeType type : AllTypes())
    {
        INFO(model::DisplayName(type));
        Lone lone(type);
        CHECK(test::Messages(model::ValidateGraph(lone.scene.Graph())).empty());
    }
}

TEST_CASE("every node type is saved and read back unchanged, alone and in a show file", "[node-types]")
{
    for (NodeType type : AllTypes())
    {
        INFO(model::DisplayName(type));
        Lone lone(type);
        auto& node = lone.scene.Node(lone.id);
        node.x = 120.5f;
        node.y = -40.f;
        node.expanded = false;
        CHECK(test::RoundTrip(node) == node);

        // A show file reads it back exactly; the only problems are the ones the node itself reports (a
        // preset node with no file, say). Only an error stops the show from loading.
        lone.scene.performance.scenes[0].cc20Value = 1;
        std::vector<model::ShowProblem> problems;
        if (const auto& def = model::Def(type); def.checkInShow != nullptr)
            def.checkInShow(lone.scene.performance, node, problems);
        const auto loaded = model::ParsePerformance(model::SerializePerformance(lone.scene.performance));
        CHECK(loaded.issues.size() == problems.size());
        CHECK(loaded.ok == std::ranges::none_of(problems, &model::ShowProblem::error));
        REQUIRE(loaded.performance.scenes.size() == 1);
        CHECK(loaded.performance.scenes[0].graph == lone.scene.Graph());
    }
}

TEST_CASE("every node type's controls are complete and stay within its valid settings", "[node-types]")
{
    for (NodeType type : AllTypes())
    {
        const auto& ui = app::Ui(type);
        INFO(model::DisplayName(type));
        std::set<std::string> ids;
        bool showsInputs = false;
        for (const auto& control : ui.controls)
        {
            INFO("control " << control.id);
            using Kind = app::Control::Kind;
            if (control.kind != Kind::Inputs && control.kind != Kind::Text)
                CHECK(ids.insert(control.id).second);
            showsInputs |= control.kind == Kind::Inputs;
            switch (control.kind)
            {
                case Kind::Slider:
                case Kind::Checkbox:
                case Kind::Choice:
                {
                    REQUIRE(control.get != nullptr);
                    REQUIRE(control.set != nullptr);
                    CHECK(std::string(control.undo).size() > 0);
                    // Every value the control can set is a valid setting.
                    std::vector<double> values;
                    if (control.kind == Kind::Slider)
                    {
                        CHECK(control.min < control.max);
                        CHECK(control.field != nullptr);
                        const double middle = (control.min + control.max) * 0.5;
                        values = {control.min, control.max, control.integer ? std::floor(middle) : middle};
                    }
                    else if (control.kind == Kind::Checkbox)
                    {
                        CHECK(control.label != nullptr);
                        values = {0.0, 1.0};
                    }
                    else
                    {
                        CHECK_FALSE(control.options.empty());
                        for (size_t i = 0; i < control.options.size(); ++i)
                            values.push_back(static_cast<double>(i));
                    }
                    for (double value : values)
                    {
                        Lone lone(type);
                        model::Node& node = lone.scene.Node(lone.id);
                        control.set(node, value);
                        INFO("set to " << value);
                        CHECK(control.get(node) == value);
                        CHECK(test::Messages(model::ValidateGraph(lone.scene.Graph())).empty());
                    }
                    break;
                }
                case Kind::Text:
                    CHECK(control.text != nullptr);
                    break;
                case Kind::Custom:
                    CHECK(control.draw != nullptr);
                    break;
                case Kind::Inputs:
                    break;
            }
        }
        // A node with inputs shows them, so they can be wired.
        Lone lone(type);
        if (model::InputCount(lone.scene.Node(lone.id)) > 0)
            CHECK(showsInputs);
    }
}

TEST_CASE("every node type draws headless in its default state, with its input rows", "[node-types]")
{
    for (NodeType type : AllTypes())
    {
        INFO(model::DisplayName(type));
        Lone lone(type);
        test::FakeNodeContext context(lone.scene);
        test::HeadlessUi ui;
        ui.Frame(context, lone.id);
        ui.Frame(context, lone.id); // and a second frame, as the canvas draws every frame
        const auto inputs = app::NodeInputs(lone.scene.Node(lone.id), context);
        CHECK(context.inputRows.size() == inputs.size() * 2);
        CHECK(context.edits.empty());
        CHECK(context.popups.empty());
        CHECK_FALSE(app::NodeTitle(lone.scene.Node(lone.id), context).empty());
        for (const auto& input : inputs)
            CHECK(model::InputKind(lone.scene.Node(lone.id), input.input) == input.kind);
    }
}

TEST_CASE("every node type can be added from the Add menu, as a valid node", "[node-types]")
{
    for (NodeType type : AllTypes())
    {
        const auto& ui = app::Ui(type);
        INFO(model::DisplayName(type));
        CHECK_FALSE(ui.addItems.empty());
        CHECK(ui.width > 0.f);
        for (const auto& item : ui.addItems)
        {
            INFO(item.label);
            test::NodeScene scene;
            model::Graph& graph = scene.Graph();
            if (type == NodeType::Viewport)
                graph = model::Graph{};
            if (item.available != nullptr && !item.available(graph))
                continue;
            const std::string id = model::AddNode(graph, type, 0.f, 0.f);
            if (item.configure != nullptr)
                item.configure(*graph.Find(id));
            CHECK(test::Messages(model::ValidateGraph(graph)).empty());
        }
    }
}
