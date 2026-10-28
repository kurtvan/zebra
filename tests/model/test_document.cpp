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

TEST_CASE("references into the performance survive coalesced edits until released")
{
    // The editor keeps references to a scene while a slider edits it, frame after frame.
    Document document(ThreeScenes(), "");
    auto edited = document.Get();
    edited.scenes[2].video.look.mix = 0.1f;
    document.Edit(edited, "Change mix", "mix:s2"); // the old state goes to the undo stack
    const auto& scene = document.Get().scenes[2];
    edited.scenes[2].video.look.mix = 0.2f;
    document.Edit(edited, "Change mix", "mix:s2"); // coalesced: replaces the state `scene` points into
    CHECK(scene.id == "s2"); // still readable, not freed memory
    CHECK(scene.video.look.mix == 0.1f);
    document.ReleaseRetired();
    CHECK(document.Get().scenes[2].video.look.mix == 0.2f);
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
