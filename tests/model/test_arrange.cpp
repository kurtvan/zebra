#include "model/Arrange.hpp"
#include "model/PerformanceJson.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

using namespace gv::model;

namespace {

auto Fixture(const char* name) -> std::string
{
    std::ifstream in(std::string(GV_TEST_FIXTURES_DIR) + "/" + name);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

/// Made-up sizes that differ by type, like the editor's.
auto SizesOf(const Graph& graph) -> std::map<std::string, ArrangeSize>
{
    std::map<std::string, ArrangeSize> sizes;
    for (const auto& node : graph.nodes)
    {
        switch (node.type)
        {
        case NodeType::MilkdropPreset: sizes[node.id] = {260.f, 230.f}; break;
        case NodeType::Switcher: sizes[node.id] = {200.f, 160.f}; break;
        case NodeType::Viewport: sizes[node.id] = {140.f, 60.f}; break;
        default: sizes[node.id] = {180.f, 90.f + static_cast<float>(node.id.size())}; break;
        }
    }
    return sizes;
}

auto Size(const std::map<std::string, ArrangeSize>& sizes, const std::string& id) -> ArrangeSize
{
    auto it = sizes.find(id);
    return it != sizes.end() ? it->second : ArrangeSize{};
}

/// Every node is placed, no two overlap, and every wire runs from left to right.
void CheckLayout(const Graph& graph, const std::map<std::string, ArrangeSize>& sizes, const std::map<std::string, Vec2>& at)
{
    REQUIRE(at.size() == graph.nodes.size());
    for (const auto& node : graph.nodes)
        REQUIRE(at.count(node.id) == 1);
    for (size_t i = 0; i < graph.nodes.size(); ++i)
    {
        for (size_t j = i + 1; j < graph.nodes.size(); ++j)
        {
            const auto& a = graph.nodes[i].id;
            const auto& b = graph.nodes[j].id;
            const Vec2 pa = at.at(a), pb = at.at(b);
            const ArrangeSize sa = Size(sizes, a), sb = Size(sizes, b);
            const bool apart = pa.x + sa.width <= pb.x || pb.x + sb.width <= pa.x || pa.y + sa.height <= pb.y || pb.y + sb.height <= pa.y;
            INFO(a << " and " << b);
            CHECK(apart);
        }
    }
    for (const auto& wire : graph.wires)
    {
        INFO(wire.fromNode << " -> " << wire.toNode);
        CHECK(at.at(wire.fromNode).x + Size(sizes, wire.fromNode).width < at.at(wire.toNode).x);
    }
}

/// A preset with a video through a gate on top, and a second preset, through a switcher into the viewport;
/// plus a loose contrast and a loose preset.
struct TestGraph
{
    Graph graph;
    std::string viewport, switcher, presetA, presetB, video, gate, looseContrast, loosePreset;

    TestGraph()
    {
        viewport = AddNode(graph, NodeType::Viewport, 900, 0);
        looseContrast = AddNode(graph, NodeType::Contrast, 50, 400);
        switcher = AddNode(graph, NodeType::Switcher, 0, 0);
        presetB = AddNode(graph, NodeType::MilkdropPreset, 300, 300);
        presetA = AddNode(graph, NodeType::MilkdropPreset, 300, -200);
        video = AddNode(graph, NodeType::VideoSource, 700, 700);
        gate = AddNode(graph, NodeType::LumaGate, -100, 20);
        loosePreset = AddNode(graph, NodeType::MilkdropPreset, 10, 10);
        Connect(graph, switcher, viewport, 0);
        Connect(graph, presetB, switcher, 1);
        Connect(graph, presetA, switcher, 0);
        Connect(graph, video, gate, 0);
        Connect(graph, gate, presetA, static_cast<int>(PresetLayer::OnTop));
    }
};

auto ColumnOf(const ArrangeColumns& columns, const std::string& id) -> int
{
    for (size_t c = 0; c < columns.size(); ++c)
    {
        if (std::find(columns[c].begin(), columns[c].end(), id) != columns[c].end())
            return static_cast<int>(c);
    }
    return -1;
}

} // namespace

TEST_CASE("Arrange: depth is the longest path to an end")
{
    TestGraph s;
    const auto depths = ArrangeDepths(s.graph);
    CHECK(depths.at(s.viewport) == 0);
    CHECK(depths.at(s.switcher) == 1);
    CHECK(depths.at(s.presetA) == 2);
    CHECK(depths.at(s.presetB) == 2);
    CHECK(depths.at(s.gate) == 3);
    CHECK(depths.at(s.video) == 4);
    CHECK(depths.at(s.looseContrast) == 0);
}

TEST_CASE("Arrange: sources on the left, the viewport on the right, loose nodes far left")
{
    TestGraph s;
    const auto columns = ArrangeIntoColumns(s.graph);
    REQUIRE(columns.size() == 6);
    CHECK(columns[0] == std::vector<std::string>{s.looseContrast, s.loosePreset}); // by id
    CHECK(columns[1] == std::vector<std::string>{s.video});
    CHECK(columns[2] == std::vector<std::string>{s.gate});
    CHECK(columns[3] == std::vector<std::string>{s.presetA, s.presetB}); // in switcher input order
    CHECK(columns[4] == std::vector<std::string>{s.switcher});
    CHECK(columns[5] == std::vector<std::string>{s.viewport});

    const auto sizes = SizesOf(s.graph);
    const auto at = ArrangeLayered(s.graph, sizes);
    CheckLayout(s.graph, sizes, at);
    CHECK(at.at(s.presetA).y < at.at(s.presetB).y);
    // The layout starts where the graph's top left was.
    float left = 1e9f, top = 1e9f;
    for (const auto& [id, p] : at)
    {
        left = std::min(left, p.x);
        top = std::min(top, p.y);
    }
    CHECK(left == -100.f);
    CHECK(top == -200.f);
}

TEST_CASE("Arrange: the barycentre pass keeps chains from crossing")
{
    // Two effect chains into a switcher's inputs 1 and 0: each chain's source lines up with its effect.
    Graph graph;
    const auto viewport = AddNode(graph, NodeType::Viewport, 0, 0);
    const auto switcher = AddNode(graph, NodeType::Switcher, 0, 0);
    const auto presetLow = AddNode(graph, NodeType::MilkdropPreset, 0, 0);
    const auto presetHigh = AddNode(graph, NodeType::MilkdropPreset, 0, 0);
    const auto gateLow = AddNode(graph, NodeType::LumaGate, 0, 0);
    const auto gateHigh = AddNode(graph, NodeType::LumaGate, 0, 0);
    const auto videoLow = AddNode(graph, NodeType::VideoSource, 0, 0);
    const auto videoHigh = AddNode(graph, NodeType::VideoSource, 0, 0);
    Connect(graph, switcher, viewport, 0);
    Connect(graph, presetHigh, switcher, 0);
    Connect(graph, presetLow, switcher, 1);
    Connect(graph, gateHigh, presetHigh, 0);
    Connect(graph, gateLow, presetLow, 0);
    Connect(graph, videoHigh, gateHigh, 0);
    Connect(graph, videoLow, gateLow, 0);
    const auto columns = ArrangeIntoColumns(graph);
    REQUIRE(columns.size() == 5);
    CHECK(columns[0] == std::vector<std::string>{videoHigh, videoLow});
    CHECK(columns[1] == std::vector<std::string>{gateHigh, gateLow});
    CHECK(columns[2] == std::vector<std::string>{presetHigh, presetLow});
}

TEST_CASE("Arrange: a lone viewport and loose nodes")
{
    Graph graph;
    const auto viewport = AddNode(graph, NodeType::Viewport, 0, 0);
    CHECK(ArrangeIntoColumns(graph) == ArrangeColumns{{viewport}});
    const auto contrast = AddNode(graph, NodeType::Contrast, 0, 0);
    const auto opacity = AddNode(graph, NodeType::Opacity, 0, 0);
    CHECK(ArrangeIntoColumns(graph) == ArrangeColumns{{contrast, opacity}, {viewport}});
    const auto sizes = SizesOf(graph);
    CheckLayout(graph, sizes, ArrangeLayered(graph, sizes));
    CHECK(ArrangeLayered(Graph{}, {}).empty());
}

TEST_CASE("Arrange: a loose chain ends in the viewport's column")
{
    Graph graph;
    const auto viewport = AddNode(graph, NodeType::Viewport, 0, 0);
    const auto video = AddNode(graph, NodeType::VideoSource, 0, 0);
    const auto gate = AddNode(graph, NodeType::LumaGate, 0, 0);
    Connect(graph, video, gate, 0);
    CHECK(ArrangeIntoColumns(graph) == ArrangeColumns{{video}, {viewport, gate}});
    const auto sizes = SizesOf(graph);
    CheckLayout(graph, sizes, ArrangeLayered(graph, sizes));
}

TEST_CASE("Arrange: nodes without a measured size get the default")
{
    TestGraph s;
    const auto at = ArrangeLayered(s.graph, {});
    CheckLayout(s.graph, {}, at);
}

TEST_CASE("Arrange: a loop doesn't hang it")
{
    Graph graph;
    const auto a = AddNode(graph, NodeType::Contrast, 0, 0);
    const auto b = AddNode(graph, NodeType::Opacity, 0, 0);
    graph.wires.push_back({a, b, 0, {}});
    graph.wires.push_back({b, a, 0, {}});
    const auto at = ArrangeLayered(graph, {});
    CHECK(at.size() == 2);
    CHECK(at.at(a).x != at.at(b).x);
}

TEST_CASE("Arrange: the same graph always gives the same layout")
{
    TestGraph s;
    const auto sizes = SizesOf(s.graph);
    const auto first = ArrangeLayered(s.graph, sizes);
    CHECK(ArrangeLayered(s.graph, sizes) == first);
    // Wire order (saving sorts wires) doesn't matter, and old positions only move the whole layout.
    Graph moved = s.graph;
    std::reverse(moved.wires.begin(), moved.wires.end());
    for (auto& node : moved.nodes)
    {
        node.x += 30.f;
        node.y += 500.f;
    }
    for (const auto& [id, p] : ArrangeLayered(moved, sizes))
    {
        INFO(id);
        CHECK(p.x - 30.f == first.at(id).x);
        CHECK(p.y - 500.f == first.at(id).y);
    }
}

TEST_CASE("Arrange: every scene of the real show")
{
    const auto result = ParsePerformance(Fixture("govtech-nov-6th.json"));
    REQUIRE(result.ok);
    REQUIRE_FALSE(result.performance.scenes.empty());
    for (const auto& scene : result.performance.scenes)
    {
        INFO("scene " << scene.id);
        const auto& graph = scene.graph;
        const auto sizes = SizesOf(graph);
        const auto at = ArrangeLayered(graph, sizes);
        CheckLayout(graph, sizes, at);
        CHECK(ArrangeLayered(graph, sizes) == at);
        // The viewport is in the rightmost column.
        const auto columns = ArrangeIntoColumns(graph);
        if (const Node* viewport = graph.Viewport())
            CHECK(ColumnOf(columns, viewport->id) == static_cast<int>(columns.size()) - 1);
    }
}
