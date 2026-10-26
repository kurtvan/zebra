#pragma once

#include "model/Performance.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gv::model {

/// An open performance with its file, unsaved-changes state and undo history.
///
/// Every edit replaces the whole performance and records the previous one: performances are small (at most
/// 128 scenes), so snapshots keep undo simple and exact.
class Document
{
public:
    Document() = default;
    Document(Performance performance, std::filesystem::path path);

    auto Get() const -> const Performance&
    {
        return m_current;
    }
    auto Path() const -> const std::filesystem::path&
    {
        return m_path;
    }
    auto IsDirty() const -> bool
    {
        return m_currentId != m_savedId;
    }

    /// Display name: the performance name, else the file name, else "Untitled".
    auto Title() const -> std::string;

    /// Increases on every change (edit, undo, redo); used to sync the engine.
    auto Revision() const -> uint64_t
    {
        return m_revision;
    }

    /// Replaces the performance with `edited`, recording an undo step called `label`.
    /// Consecutive edits with the same non-empty `coalesceKey` merge into one step (e.g. a slider drag).
    void Edit(Performance edited, std::string label, std::string coalesceKey = {});

    /// Ends the current coalescing run, so the next edit starts a new undo step.
    void EndCoalescing()
    {
        m_coalesceKey.clear();
    }

    auto CanUndo() const -> bool
    {
        return !m_undo.empty();
    }
    auto CanRedo() const -> bool
    {
        return !m_redo.empty();
    }
    auto UndoLabel() const -> std::string;
    auto RedoLabel() const -> std::string;
    void Undo();
    void Redo();

    /// Records that the current state is what's on disk at `path`.
    void MarkSaved(const std::filesystem::path& path);

private:
    struct Step
    {
        Performance state; //!< The performance before (undo) or after (redo) the step.
        uint64_t id{0};    //!< Identity of that state, for dirty tracking.
        std::string label;
    };

    void Changed();

    Performance m_current;
    std::filesystem::path m_path;
    std::vector<Step> m_undo;
    std::vector<Step> m_redo;
    std::string m_coalesceKey;
    uint64_t m_nextId{1};
    uint64_t m_currentId{0};
    uint64_t m_savedId{0};
    uint64_t m_revision{0};
};

// --- Edits: pure functions on a performance ------------------------------------------------------

/// A scene id not used in `performance`, like "scene-7".
auto UniqueSceneId(const Performance& performance, const std::string& base = "scene") -> std::string;

/// Inserts a new scene after `after` (or at the end when out of range). Returns its index, or -1 at the
/// 128-scene limit. The new scene gets a unique id and the lowest free CC 20 value.
auto AddScene(Performance& performance, int after, const std::filesystem::path& presetPath) -> int;

/// Inserts a copy of scene `index` right after it (new id and CC value). Returns its index, or -1.
auto DuplicateScene(Performance& performance, int index) -> int;

/// Removes scene `index`. Returns false if out of range.
auto DeleteScene(Performance& performance, int index) -> bool;

/// Moves scene `from` to position `to`. CC values move with their scenes. Returns false if out of range.
auto MoveScene(Performance& performance, int from, int to) -> bool;

} // namespace gv::model
