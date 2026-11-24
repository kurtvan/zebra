#include "model/NodeList.hpp"
#include "model/PerformanceJson.hpp"
#include "model/nodes/NodeDef.hpp"

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

auto Ids(const std::vector<NodeRow>& rows) -> std::vector<std::string>
{
    std::vector<std::string> ids;
    for (const auto& row : rows)
        ids.push_back(row.id);
    return ids;
}

auto Row(const std::vector<NodeRow>& rows, const std::string& id) -> const NodeRow&
{
    const auto it = std::find_if(rows.begin(), rows.end(), [&](const NodeRow& row) { return row.id == id; });
    REQUIRE(it != rows.end());
    return *it;
}

/// Two presets through a switcher into the viewport, a video into the first through a luma gate, and a loose
/// contrast and preset.
struct SwitcherGraph
{
    Graph graph;
    std::string a, b, loosePreset, source, gate, contrast, switcher, viewport;

    SwitcherGraph()
    {
        a = AddNode(graph, NodeType::MilkdropPreset, 0, 0);
        b = AddNode(graph, NodeType::MilkdropPreset, 0, 0);
        loosePreset = AddNode(graph, NodeType::MilkdropPreset, 0, 0);
        source = AddNode(graph, NodeType::VideoSource, 0, 0);
        gate = AddNode(graph, NodeType::LumaGate, 0, 0);
        contrast = AddNode(graph, NodeType::Contrast, 0, 0);
        switcher = AddNode(graph, NodeType::Switcher, 0, 0);
        viewport = AddNode(graph, NodeType::Viewport, 0, 0);
        Connect(graph, source, gate, 0);
        Connect(graph, gate, a, static_cast<int>(PresetLayer::Feedback));
        Connect(graph, a, switcher, 0);
        Connect(graph, b, switcher, 1);
        Connect(graph, switcher, viewport, 0);
        FollowWires(graph);
    }
};

/// Names presets by their id, so ordering by label is easy to check.
auto ById(const Node& node) -> std::string
{
    return node.type == NodeType::MilkdropPreset ? "Preset " + node.id : Label(node);
}

} // namespace

TEST_CASE("Node list: every input of a switcher reaches the viewport, loose nodes don't")
{
    const SwitcherGraph g;
    const auto rows = ListNodes(g.graph, "");
    REQUIRE(rows.size() == g.graph.nodes.size());
    for (const auto& id : {g.a, g.b, g.source, g.gate, g.switcher, g.viewport})
        CHECK(Row(rows, id).connected);
    CHECK_FALSE(Row(rows, g.loosePreset).connected);
    CHECK_FALSE(Row(rows, g.contrast).connected);
    CHECK(Row(rows, g.gate).type == "Luma gate");
    CHECK(Row(rows, g.gate).label == Label(*g.graph.Find(g.gate)));
}

TEST_CASE("Node list: connected first, then by label ignoring case, then id")
{
    const SwitcherGraph g;
    const auto rows = ListNodes(g.graph, "", ById);
    // Connected: "Luma gate", "Preset milkdrop-1", "Preset milkdrop-2", "Switcher", "Video source", "Viewport";
    // then loose: "Contrast", "Preset milkdrop-3".
    CHECK(Ids(rows) == std::vector<std::string>{g.gate, g.a, g.b, g.switcher, g.source, g.viewport, g.contrast, g.loosePreset});

    Graph graph;
    const auto upper = AddNode(graph, NodeType::Contrast, 0, 0);
    const auto lower = AddNode(graph, NodeType::Saturation, 0, 0);
    const auto same = AddNode(graph, NodeType::Opacity, 0, 0);
    const auto names = [&](const Node& node) -> std::string { return node.id == upper ? "Banana" : "apple"; };
    // "apple" before "Banana" (ignoring case); the two "apple"s by id.
    CHECK(Ids(ListNodes(graph, "", names)) == std::vector<std::string>{std::min(lower, same), std::max(lower, same), upper});
}

TEST_CASE("Node list: the filter matches label or type, ignoring case")
{
    const SwitcherGraph g;
    CHECK(Ids(ListNodes(g.graph, "GATE")) == std::vector<std::string>{g.gate});
    CHECK(Ids(ListNodes(g.graph, "gate", ById)) == std::vector<std::string>{g.gate});
    // By label: only the custom names contain the id.
    CHECK(Ids(ListNodes(g.graph, "preset milkdrop-2", ById)) == std::vector<std::string>{g.b});
    CHECK(ListNodes(g.graph, "milkdrop").size() == 3);
    CHECK(ListNodes(g.graph, "nothing like it").empty());
    CHECK(ListNodes(g.graph, "").size() == g.graph.nodes.size());
}

TEST_CASE("Node list: a graph without a viewport lists everything as not connected")
{
    Graph graph;
    const auto preset = AddNode(graph, NodeType::MilkdropPreset, 0, 0);
    CHECK(NodesReachingViewport(graph).empty());
    const auto rows = ListNodes(graph, "");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].id == preset);
    CHECK_FALSE(rows[0].connected);
}

TEST_CASE("Node list: the real show's scenes")
{
    const auto result = ParsePerformance(Fixture("govtech-nov-6th.json"));
    REQUIRE(result.ok);
    int switchers = 0;
    for (const auto& scene : result.performance.scenes)
    {
        INFO("scene " << scene.id);
        const auto rows = ListNodes(scene.graph, "");
        REQUIRE(rows.size() == scene.graph.nodes.size());
        // Connected rows come first.
        const auto firstLoose = std::find_if(rows.begin(), rows.end(), [](const NodeRow& row) { return !row.connected; });
        CHECK(std::none_of(firstLoose, rows.end(), [](const NodeRow& row) { return row.connected; }));
        // Every preset of a converted scene is played, so it reaches the viewport (through its switcher, if any).
        for (const auto& node : scene.graph.nodes)
        {
            if (node.type == NodeType::MilkdropPreset || node.type == NodeType::Viewport)
                CHECK(Row(rows, node.id).connected);
            if (node.type == NodeType::Switcher)
                switchers++;
        }
        const auto presets = ListNodes(scene.graph, "Milkdrop");
        CHECK(static_cast<size_t>(std::count_if(scene.graph.nodes.begin(), scene.graph.nodes.end(), [](const Node& node) {
                  return node.type == NodeType::MilkdropPreset;
              })) == presets.size());
    }
    CHECK(switchers > 0); // the switcher case is covered
}
