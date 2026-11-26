// The Video source node: model/nodes/VideoSourceNode.cpp, app/nodes/VideoSourceNode.cpp.

#include "nodes/NodeTestKit.hpp"

#include "engine/RenderPlan.hpp"

using namespace gv;
using model::NodeType;
using model::VideoSourceParams;
using model::VideoSourceType;

namespace {

auto Params(const model::Node& node) -> const VideoSourceParams&
{
    return model::ParamsOf<VideoSourceParams>(node);
}

/// A scene with a pool video "clip" (/v/clip.mov) and a file source node playing it.
struct VideoScene
{
    VideoScene()
    {
        scene.performance.videos.push_back({"clip", "", "/v/clip.mov"});
        id = scene.Add(NodeType::VideoSource, [](model::Node& n) { model::ParamsOf<VideoSourceParams>(n).source.videoId = "clip"; });
    }
    test::NodeScene scene;
    std::string id;
};

auto Syphon(std::string app, std::string server)
{
    return [=](model::Node& n) {
        auto& source = model::ParamsOf<VideoSourceParams>(n).source;
        source.type = VideoSourceType::Syphon;
        source.appName = app;
        source.serverName = server;
    };
}

} // namespace

TEST_CASE("video source: no inputs, a video output; a looping file that restarts by default", "[node:video-source]")
{
    test::NodeScene scene;
    const auto& node = scene.Node(scene.Add(NodeType::VideoSource));
    CHECK(node.id == "video-1");
    CHECK(model::OutputKind(node) == model::PortKind::Picture);
    CHECK(model::InputCount(node) == 0);
    CHECK_FALSE(model::InputKind(node, 0));
    CHECK(Params(node).source.type == VideoSourceType::File);
    CHECK(Params(node).restartOnEnter);
    CHECK(Params(node).loop);
    CHECK_FALSE(model::IsEffect(NodeType::VideoSource));
}

TEST_CASE("video source: feeds presets, effects, switchers and the viewport", "[node:video-source]")
{
    test::NodeScene scene;
    const std::string video = scene.Add(NodeType::VideoSource);
    const std::string preset = scene.Add(NodeType::MilkdropPreset);
    const std::string switcher = scene.Add(NodeType::Switcher);
    CHECK(model::ConnectErrors(scene.Graph(), video, preset, static_cast<int>(model::PresetLayer::BeforeComposite)).empty());
    CHECK(model::ConnectErrors(scene.Graph(), video, scene.Add(NodeType::Contrast), 0).empty());
    CHECK(model::ConnectErrors(scene.Graph(), video, switcher, 0).empty());
    CHECK(model::ConnectErrors(scene.Graph(), video, "viewport", 0).empty());
    CHECK(test::Messages(model::ConnectErrors(scene.Graph(), preset, video, 0)) == std::vector<std::string>{"Video source has no input 1."});
}

TEST_CASE("video source: saved with its source, restart and loop, and read back", "[node:video-source]")
{
    VideoScene file;
    auto& node = file.scene.Node(file.id);
    model::ParamsOf<VideoSourceParams>(node).loop = false;
    CHECK(test::RoundTrip(node) == node);

    test::NodeScene scene;
    auto& syphon = scene.Node(scene.Add(NodeType::VideoSource, Syphon("Resolume", "Out")));
    model::ParamsOf<VideoSourceParams>(syphon).restartOnEnter = false;
    CHECK(test::RoundTrip(syphon) == syphon);

    auto [unknown, issues] = test::ReadNode(NodeType::VideoSource, {{"source", {{"type", "ndi"}}}});
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "/node/source/type");
}

TEST_CASE("video source: a show file needs a file source's video in the pool; Syphon needs nothing", "[node:video-source]")
{
    VideoScene file;
    auto problems = [&](const std::string& id) {
        std::vector<model::ShowProblem> found;
        model::Def(NodeType::VideoSource).checkInShow(file.scene.performance, file.scene.Node(id), found);
        return found;
    };
    CHECK(problems(file.id).empty());
    CHECK(problems(file.scene.Add(NodeType::VideoSource)) == std::vector<model::ShowProblem>{{false, "no video chosen"}});
    const std::string gone = file.scene.Add(NodeType::VideoSource, [](model::Node& n) { model::ParamsOf<VideoSourceParams>(n).source.videoId = "gone"; });
    CHECK(problems(gone) == std::vector<model::ShowProblem>{{true, "video \"gone\" isn't in the video pool"}});
    CHECK(problems(file.scene.Add(NodeType::VideoSource, Syphon("", ""))).empty());
}

TEST_CASE("video source: the plan plays its source", "[node:video-source]")
{
    test::NodeScene scene;
    const std::string preset = scene.Add(NodeType::MilkdropPreset);
    const std::string video = scene.Add(NodeType::VideoSource, Syphon("VDMX", ""));
    model::Connect(scene.Graph(), preset, "viewport", 0);
    model::Connect(scene.Graph(), video, preset, 0);
    const auto plan = engine::CompilePlan(scene.Graph());
    CHECK(plan.sourceNode == video);
    CHECK(plan.source.type == VideoSourceType::Syphon);
    CHECK(plan.source.appName == "VDMX");
    REQUIRE(plan.branches.size() == 1);
    CHECK(plan.branches[0].steps.empty());
}

TEST_CASE("video source: titled by its video or Syphon sender", "[node:video-source]")
{
    VideoScene file;
    test::FakeNodeContext context(file.scene);
    CHECK(app::NodeTitle(file.scene.Node(file.id), context) == "clip");
    file.scene.performance.videos[0].name = "Opening loop";
    CHECK(app::NodeTitle(file.scene.Node(file.id), context) == "Opening loop");
    CHECK(app::NodeTitle(file.scene.Node(file.scene.Add(NodeType::VideoSource)), context) == "Video (none chosen)");
    CHECK(app::NodeTitle(file.scene.Node(file.scene.Add(NodeType::VideoSource, Syphon("", ""))), context) == "Syphon: Any sender");
    CHECK(app::NodeTitle(file.scene.Node(file.scene.Add(NodeType::VideoSource, Syphon("Resolume", "Out"))), context) == "Syphon: Resolume / Out");
}

TEST_CASE("video source: its dot says whether a video is chosen, found and playing", "[node:video-source]")
{
    VideoScene file;
    test::FakeNodeContext context(file.scene);
    auto state = [&](const std::string& id) { return app::NodeStatusOf(file.scene.Node(id), context).state; };

    CHECK(state(file.scene.Add(NodeType::VideoSource)) == app::NodeState::Empty);
    CHECK(state(file.id) == app::NodeState::Ready);
    context.live = true;
    // Live, but its video switcher passes another input: not on screen.
    CHECK(app::NodeStatusOf(file.scene.Node(file.id), context) ==
          app::NodeStatus{app::NodeState::Ready, "Not on screen now."});
    context.liveVideo = file.id;
    CHECK(state(file.id) == app::NodeState::Live);
    context.videoState = std::pair<bool, std::string>{false, "Video: opening"};
    CHECK(app::NodeStatusOf(file.scene.Node(file.id), context) == app::NodeStatus{app::NodeState::Waiting, "Video: opening"});
    context.videoState = std::pair<bool, std::string>{true, "Video: live"};
    CHECK(state(file.id) == app::NodeState::Live);
    file.scene.performance.videos[0].missing = true;
    CHECK(app::NodeStatusOf(file.scene.Node(file.id), context) == app::NodeStatus{app::NodeState::Missing, "File not found: /v/clip.mov"});
}

TEST_CASE("video source: File shows the video list and Loop; Syphon shows the senders", "[node:video-source]")
{
    const auto& file = test::ControlById(NodeType::VideoSource, "##file");
    const auto& syphon = test::ControlById(NodeType::VideoSource, "##syphon");
    const auto& loop = test::ControlById(NodeType::VideoSource, "##loop");
    const auto& restart = test::ControlById(NodeType::VideoSource, "##restart");
    VideoScene scene;
    model::Node node = scene.scene.Node(scene.id);
    CHECK(file.visible(node));
    CHECK_FALSE(syphon.visible(node));
    CHECK(loop.visible(node));
    CHECK(restart.visible == nullptr);
    Syphon("", "")(node);
    CHECK_FALSE(file.visible(node));
    CHECK(syphon.visible(node));
    CHECK_FALSE(loop.visible(node)); // a stream doesn't end
}

TEST_CASE("video source: choosing File or Syphon, and Loop, are one undo step each", "[node:video-source]")
{
    VideoScene scene;
    test::FakeNodeContext context(scene.scene);
    test::HeadlessUi ui;
    ui.Frame(context, scene.id);
    ui.Click(context, scene.id, test::HeadlessUi::Centre(context, "On: the video loops back to its start point at the end.\n"
                                                                  "Off: it plays to the end once and holds its last frame."));
    CHECK_FALSE(Params(scene.scene.Node(scene.id)).loop);
    CHECK(context.LastEdit() == "Play Video Once");

    const auto& type = test::ControlById(NodeType::VideoSource, "##type");
    CHECK(type.radio);
    model::Node node = scene.scene.Node(scene.id);
    type.set(node, 1);
    CHECK(Params(node).source.type == VideoSourceType::Syphon);
    CHECK(Params(node).source.videoId == "clip"); // kept, for switching back
    CHECK(std::string(type.undo) == "Change Video Source");
}

TEST_CASE("video source: the video list picks a pool video, or adds one with the video in one step", "[node:video-source]")
{
    VideoScene scene;
    scene.scene.performance.videos.push_back({"other", "", "/v/other.mov"});
    test::FakeNodeContext context(scene.scene);
    test::HeadlessUi ui;
    ui.Frame(context, scene.id);
    ui.Click(context, scene.id, test::HeadlessUi::Centre(context, "The video from the pool. Its start point and speed are in the Videos panel."));
    const int list = test::IndexOf(NodeType::VideoSource, test::ControlById(NodeType::VideoSource, "##file"));
    REQUIRE(context.popups == std::vector<int>{list});

    ui.Frame(context, scene.id, list);
    ui.Click(context, scene.id, test::HeadlessUi::PopupRow(1), list);
    CHECK(Params(scene.scene.Node(scene.id)).source.videoId == "other");
    CHECK(context.LastEdit() == "Change Video");

    context.chosenFile = std::filesystem::path("/v/new.mov");
    ui.Frame(context, scene.id, list);
    ui.Click(context, scene.id, test::HeadlessUi::PopupLastRow(), list); // "Add Video..."
    CHECK(context.fileDialogs == std::vector<std::string>{"Add a video"});
    REQUIRE(scene.scene.performance.videos.size() == 3);
    CHECK(Params(scene.scene.Node(scene.id)).source.videoId == scene.scene.performance.videos[2].id);
    CHECK(context.LastEdit() == "Change Video");
}

TEST_CASE("video source: the Syphon list picks a sender, or any", "[node:video-source]")
{
    test::NodeScene scene;
    const std::string id = scene.Add(NodeType::VideoSource, Syphon("Old", "Gone"));
    test::FakeNodeContext context(scene);
    context.servers = {{"Resolume", "Out"}, {"VDMX", ""}};
    const int list = test::IndexOf(NodeType::VideoSource, test::ControlById(NodeType::VideoSource, "##syphon"));
    test::HeadlessUi ui;
    ui.Frame(context, id, list);
    ui.Click(context, id, test::HeadlessUi::PopupRow(1), list);
    CHECK(Params(scene.Node(id)).source.appName == "Resolume");
    CHECK(Params(scene.Node(id)).source.serverName == "Out");
    ui.Click(context, id, test::HeadlessUi::PopupRow(0), list);
    CHECK(Params(scene.Node(id)).source.appName.empty());
    CHECK(Params(scene.Node(id)).source.serverName.empty());
    CHECK(context.LastEdit() == "Change Syphon Server");
}

TEST_CASE("video source: the Add menu has a file and a Syphon entry", "[node:video-source]")
{
    const auto& items = app::Ui(NodeType::VideoSource).addItems;
    REQUIRE(items.size() == 2);
    CHECK(std::string(items[0].label) == "Video file");
    CHECK(items[0].configure == nullptr);
    CHECK(std::string(items[1].label) == "Syphon video");
    model::Node node;
    node.type = NodeType::VideoSource;
    node.params = model::DefaultParams(NodeType::VideoSource);
    items[1].configure(node);
    CHECK(Params(node).source.type == VideoSourceType::Syphon);
}
