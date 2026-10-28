#include "model/Document.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace gv::model;

namespace {

auto ThreeScenes() -> Performance
{
    Performance performance;
    for (int i = 0; i < 3; ++i)
    {
        Scene scene;
        scene.id = "s" + std::to_string(i);
        scene.presetPath = "/p" + std::to_string(i) + ".milk";
        scene.cc20Value = i;
        performance.scenes.push_back(scene);
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
    for (float mix : {0.1f, 0.2f, 0.3f})
    {
        auto edited = document.Get();
        edited.scenes[1].video.look.mix = mix;
        document.Edit(edited, "Change mix", "mix:s1");
    }
    document.EndCoalescing();
    auto edited = document.Get();
    edited.scenes[1].video.look.mix = 0.9f;
    document.Edit(edited, "Change mix", "mix:s1");

    document.Undo();
    CHECK(document.Get().scenes[1].video.look.mix == 0.3f);
    document.Undo();
    CHECK(document.Get().scenes[1].video.look.mix == 1.0f); // the original default
    CHECK_FALSE(document.CanUndo());
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
    CHECK(performance.scenes[1].presetPath == "/new.milk");
    CHECK(performance.scenes[1].cc20Value == 1);
    CHECK(performance.scenes[1].id == "scene-4");
    CHECK(AddScene(performance, 99, "/end.milk") == 4);
}

TEST_CASE("replacing a scene's preset, or the playlist entry on screen")
{
    auto performance = ThreeScenes();
    CHECK(ReplacePreset(performance, 1, 5, "/new.milk") == 0);
    CHECK(performance.scenes[1].presetPath == "/new.milk");
    CHECK(ReplacePreset(performance, 3, 0, "/new.milk") == -1);

    auto& scene = performance.scenes[2];
    scene.playlist.enabled = true;
    scene.playlist.Add("/a.milk", {});
    scene.playlist.Add("/b.milk", {});
    scene.playlist.Add("/c.milk", {});
    CHECK(ReplacePreset(performance, 2, 1, "/new.milk") == 1);
    CHECK(scene.playlist.presets[1] == "/new.milk");
    CHECK(scene.playlist.looks.size() == 3); // looks stay with their entries
    CHECK(ReplacePreset(performance, 2, 9, "/last.milk") == 2);
    CHECK(scene.playlist.presets[2] == "/last.milk");
    CHECK(scene.presetPath == "/p2.milk"); // a playlist scene keeps its own preset field
}

TEST_CASE("adding stops at 128 scenes")
{
    Performance performance;
    for (int i = 0; i < kMaxScenes; ++i)
        AddScene(performance, -1, "/p.milk");
    CHECK(performance.scenes.size() == static_cast<size_t>(kMaxScenes));
    CHECK(AddScene(performance, -1, "/p.milk") == -1);
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
