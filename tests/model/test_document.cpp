#include "model/Document.hpp"

#include "TestScenes.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace gv::model;
using gv::test::MakeScene;

namespace {

auto ThreeScenes() -> Performance
{
    Performance performance;
    for (int i = 0; i < 3; ++i)
    {
        ClassicScene classic;
        classic.presetPath = "/p" + std::to_string(i) + ".milk";
        performance.scenes.push_back(MakeScene("s" + std::to_string(i), i, classic));
    }
    return performance;
}

} // namespace

TEST_CASE("edits are undoable and redoable, and track unsaved changes")
{
    Document document(ThreeScenes(), "/show.json");
    CHECK_FALSE(document.IsDirty());
    CHECK_FALSE(document.CanUndo());

    auto edited = document.Get();
    edited.scenes[0].name = "Intro";
    document.Edit(edited, "Rename scene");
    CHECK(document.IsDirty());
    CHECK(document.UndoLabel() == "Rename scene");

    document.Undo();
    CHECK(document.Get().scenes[0].name.empty());
    CHECK_FALSE(document.IsDirty()); // back to the saved state
    CHECK(document.CanRedo());

    document.Redo();
    CHECK(document.Get().scenes[0].name == "Intro");
    CHECK(document.IsDirty());

    document.MarkSaved("/show.json");
    CHECK_FALSE(document.IsDirty());
    document.Undo();
    CHECK(document.IsDirty()); // the saved state is now the edited one
}

TEST_CASE("a new edit clears redo")
{
    Document document(ThreeScenes(), "");
    auto edited = document.Get();
    edited.name = "A";
    document.Edit(edited, "Rename");
    document.Undo();
    edited.name = "B";
    document.Edit(edited, "Rename");
    CHECK_FALSE(document.CanRedo());
}

TEST_CASE("edits with the same coalesce key form one undo step until coalescing ends")
{
    Document document(ThreeScenes(), "");
    for (float strength : {0.1f, 0.2f, 0.3f})
    {
        auto edited = document.Get();
        edited.scenes[1].beatHit.strength = strength;
        document.Edit(edited, "Change strength", "strength:s1");
    }
    document.EndCoalescing();
    auto edited = document.Get();
    edited.scenes[1].beatHit.strength = 0.9f;
    document.Edit(edited, "Change strength", "strength:s1");

    document.Undo();
    CHECK(document.Get().scenes[1].beatHit.strength == 0.3f);
    document.Undo();
    CHECK(document.Get().scenes[1].beatHit.strength == 0.5f); // the original default
    CHECK_FALSE(document.CanUndo());
}

TEST_CASE("references into the performance survive coalesced edits until released")
{
    // The editor keeps references to a scene while a slider edits it, frame after frame.
    Document document(ThreeScenes(), "");
    auto edited = document.Get();
    edited.scenes[2].beatHit.strength = 0.1f;
    document.Edit(edited, "Change strength", "strength:s2"); // the old state goes to the undo stack
    const auto& scene = document.Get().scenes[2];
    edited.scenes[2].beatHit.strength = 0.2f;
    document.Edit(edited, "Change strength", "strength:s2"); // coalesced: replaces the state `scene` points into
    CHECK(scene.id == "s2"); // still readable, not freed memory
    CHECK(scene.beatHit.strength == 0.1f);
    document.ReleaseRetired();
    CHECK(document.Get().scenes[2].beatHit.strength == 0.2f);
}

TEST_CASE("revision increases on every change")
{
    Document document(ThreeScenes(), "");
    const auto start = document.Revision();
    auto edited = document.Get();
    edited.name = "x";
    document.Edit(edited, "Rename");
    document.Undo();
    document.Redo();
    CHECK(document.Revision() == start + 3);
}

TEST_CASE("title falls back from name to file to Untitled")
{
    CHECK(Document().Title() == "Untitled");
    CHECK(Document(Performance{}, "/a/my-show.json").Title() == "my-show");
    Performance named;
    named.name = "Govtech";
    CHECK(Document(named, "/a/my-show.json").Title() == "Govtech");
}

TEST_CASE("adding scenes uses unique ids and free CC values")
{
    auto performance = ThreeScenes();
    performance.scenes[1].cc20Value = 5; // free: 1
    const int index = AddScene(performance, 0, "/new.milk");
    REQUIRE(index == 1);
    CHECK(performance.scenes[1].FirstPreset() == "/new.milk");
    CHECK(ValidateGraph(performance.scenes[1].graph).empty());
    CHECK(performance.scenes[1].cc20Value == 1);
    CHECK(performance.scenes[1].id == "scene-4");
    CHECK(AddScene(performance, 99, "/end.milk") == 4);
}

TEST_CASE("replacing a scene's preset, or the playlist entry on screen")
{
    auto performance = ThreeScenes();
    CHECK(ReplacePreset(performance, 1, 5, "/new.milk") == 0);
    CHECK(performance.scenes[1].FirstPreset() == "/new.milk");
    CHECK(ReplacePreset(performance, 3, 0, "/new.milk") == -1);

    ClassicScene classic;
    classic.playlist.enabled = true;
    classic.playlist.presets = {"/a.milk", "/b.milk", "/c.milk"};
    classic.playlist.looks = {VideoLook{0.1f}, VideoLook{0.2f}, VideoLook{0.3f}};
    classic.video.enabled = true;
    classic.video.source.videoId = "v";
    performance.scenes[2] = MakeScene("s2", 2, classic);
    CHECK(ReplacePreset(performance, 2, 1, "/new.milk") == 1);
    const auto& scene = performance.scenes[2];
    CHECK(scene.classic.playlist.presets == std::vector<std::filesystem::path>{"/a.milk", "/new.milk", "/c.milk"});
    CHECK(scene.LookFor(1).mix == 0.2f); // looks stay with their entries
    CHECK(ReplacePreset(performance, 2, 9, "/last.milk") == 2);
    CHECK(scene.classic.playlist.presets[2] == "/last.milk");
}

TEST_CASE("assigning a video: joins a video playlist, replaces the video, or adds one")
{
    auto performance = ThreeScenes();
    ClassicScene playlist = gv::test::WithVideo("/p.milk", "a");
    playlist.video.playlist = {true, {"a"}, IntervalUnit::Bars, 30.0, 4, false};
    performance.scenes[1] = MakeScene("s1", 1, playlist);
    performance.scenes[2] = MakeScene("s2", 2, gv::test::WithVideo("/p.milk", "a", VideoLook{0.4f}));

    CHECK(AssignVideo(performance, 0, "x")); // no video yet: a source and a default look chain
    CHECK(performance.scenes[0].classic.video.enabled);
    CHECK(performance.scenes[0].classic.video.source.videoId == "x");
    CHECK(performance.scenes[0].LookFor(0) == VideoLook{});
    CHECK(ValidateGraph(performance.scenes[0].graph).empty());

    CHECK(AssignVideo(performance, 1, "y"));
    CHECK(performance.scenes[1].classic.video.playlist.videos == std::vector<std::string>{"a", "y"});

    CHECK(AssignVideo(performance, 2, "z"));
    CHECK(performance.scenes[2].classic.video.source.videoId == "z");
    CHECK(performance.scenes[2].LookFor(0).mix == 0.4f); // the look stays
    CHECK_FALSE(AssignVideo(performance, 3, "x"));
}

TEST_CASE("graph edits go through one gate that refuses invalid graphs with every error")
{
    Document document(ThreeScenes(), "");
    const auto viewport = document.Get().scenes[0].graph.Viewport()->id;

    // A valid edit: one undo step.
    auto errors = TryEditGraph(document, 0, [](Graph& graph) { AddNode(graph, NodeType::Contrast, 0, 0); }, "Add Contrast");
    CHECK(errors.empty());
    CHECK(document.UndoLabel() == "Add Contrast");
    CHECK(document.Get().scenes[0].graph.nodes.size() == 3);

    // An invalid edit changes nothing and reports every problem.
    const auto before = document.Get().scenes[0].graph;
    errors = TryEditGraph(document, 0, [&](Graph& graph) {
        Connect(graph, viewport, "contrast-1", 0);                // the viewport has no output
        ParamsOf<ContrastParams>(*graph.Find("contrast-1")).contrast = -1.0f; // out of range
    }, "Bad");
    CHECK(errors.size() == 2);
    CHECK(document.Get().scenes[0].graph == before);
    CHECK(document.UndoLabel() == "Add Contrast");

    CHECK_FALSE(TryEditGraph(document, 7, [](Graph&) {}, "Nothing").empty());
}

TEST_CASE("adding stops at 128 scenes")
{
    Performance performance;
    for (int i = 0; i < kMaxScenes; ++i)
        AddScene(performance, -1, "/p.milk");
    CHECK(performance.scenes.size() == static_cast<size_t>(kMaxScenes));
    CHECK(AddScene(performance, -1, "/p.milk") == -1);
}

TEST_CASE("renumbering gives CC values in list order")
{
    auto performance = ThreeScenes();
    CHECK(RenumberCc20(performance) == 0); // already in order
    CHECK(MoveScene(performance, 2, 0));
    performance.scenes[1].cc20Value = 2; // a duplicate value
    CHECK(RenumberCc20(performance) == 3); // s2, s0, s1 had 2, 2, 1
    CHECK(performance.scenes[0].id == "s2"); // the order doesn't change
    for (int i = 0; i < 3; ++i)
        CHECK(performance.scenes[static_cast<size_t>(i)].cc20Value == i);
}

TEST_CASE("duplicate, delete and move")
{
    auto performance = ThreeScenes();
    performance.scenes[0].name = "Intro";
    const int copy = DuplicateScene(performance, 0);
    REQUIRE(copy == 1);
    CHECK(performance.scenes[1].name == "Intro copy");
    CHECK(performance.scenes[1].id != performance.scenes[0].id);
    CHECK(performance.scenes[1].cc20Value == 3);

    CHECK(MoveScene(performance, 3, 0));
    CHECK(performance.scenes[0].id == "s2");
    CHECK(performance.scenes[0].cc20Value == 2); // CC values move with their scenes

    CHECK(DeleteScene(performance, 0));
    CHECK(performance.scenes.size() == 3);
    CHECK_FALSE(DeleteScene(performance, 3));
    CHECK_FALSE(MoveScene(performance, 0, 3));
}
