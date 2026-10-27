#pragma once

#include "app/Settings.hpp"
#include "app/VideoManager.hpp"
#include "engine/Engine.hpp"
#include "io/AudioInput.hpp"
#include "io/MidiInput.hpp"
#include "library/Library.hpp"
#include "model/Document.hpp"
#include "model/PerformanceJson.hpp"
#include "thumbnails/ThumbnailService.hpp"

#include <OpenGL/gl3.h>

#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gv::app {

/// What the editor UI can ask the app to do.
class AppActions
{
public:
    virtual ~AppActions() = default;

    // Document
    virtual void NewPerformance() = 0;
    virtual void OpenPerformance(const std::filesystem::path& file) = 0;
    virtual void SavePerformance() = 0;
    virtual void SavePerformanceAs() = 0;
    virtual void RevertPerformance() = 0;
    /// Replaces the performance with `edited` as one undoable step (see Document::Edit for `coalesceKey`).
    virtual void Edit(model::Performance edited, const std::string& label, const std::string& coalesceKey = {}) = 0;
    virtual void EndEdit() = 0; //!< Ends a coalesced edit (e.g. slider released).
    virtual void Undo() = 0;
    virtual void Redo() = 0;

    // Playback and setup
    virtual void Trigger(int scene) = 0;
    virtual void TriggerItem(int scene, int item) = 0; //!< Go live on a playlist entry.
    virtual void ToggleFullscreen() = 0;
    virtual void SetAudioDevice(const std::string& deviceName) = 0; //!< Empty: system default.
    virtual void SetMidiChannel(int channel) = 0;
    virtual void SetLibraryRoot(const std::filesystem::path& folder) = 0;
    virtual void SetTextureFolder(const std::filesystem::path& folder) = 0;
    virtual void ToggleFavourite(const std::string& presetId) = 0;
    virtual void RescanLibrary() = 0;
    virtual void SettingsChanged() = 0;
};

/// Everything the editor shows. Owned by the app; the shell only reads it, except `selectedScene`,
/// `droppedFiles` (consumed) and settings fields it edits.
struct ShellState
{
    const model::Document* document{nullptr};
    int* selectedScene{nullptr};
    std::vector<std::filesystem::path>* droppedFiles{nullptr};
    const std::vector<model::Issue>* issues{nullptr};
    std::deque<std::string>* messages{nullptr};
    Settings* settings{nullptr};
    const engine::Engine* engine{nullptr};
    const io::MidiInput* midi{nullptr};
    const io::AudioInput* audio{nullptr};
    const VideoManager* video{nullptr};
    const library::Library* library{nullptr};
    const library::Favourites* favourites{nullptr};
    const std::set<std::string>* availableTextures{nullptr};
    uint64_t libraryVersion{0}; //!< Changes whenever the library or favourites change.
    bool indexing{false};
    thumbnails::ThumbnailService* thumbnails{nullptr};
    int indexProgress{0};
    GLuint preview{0};
    const std::deque<float>* frameIntervalsMs{nullptr};
    long long slowFrames{0};
};

/// The editor: preview, scene list, scene properties, setup, preset browser (M5), status and messages.
class Shell
{
public:
    /// Fonts and colours. Call once before the first frame.
    static void ApplyTheme();

    void Draw(ShellState& state, AppActions& actions);

private:
    void BuildDefaultLayout(unsigned int dockspace);
    void DrawPreview(const ShellState& state, AppActions& actions);
    void DrawScenes(ShellState& state, AppActions& actions);
    void DrawSceneProperties(ShellState& state, AppActions& actions);
    void DrawPlaylist(ShellState& state, AppActions& actions, int index, const engine::Engine::SceneInfo& info);
    void DrawSetup(ShellState& state, AppActions& actions);
    void DrawBrowser(ShellState& state, AppActions& actions);
    void DrawPresetGrid(ShellState& state, AppActions& actions);
    void AssignPreset(ShellState& state, AppActions& actions, const std::filesystem::path& preset, int scene);
    void DrawVideos(ShellState& state, AppActions& actions);
    void DrawStatus(const ShellState& state);
    void DrawMessages(const ShellState& state);
    void DrawStatusBar(const ShellState& state);
    void HandleDrops(ShellState& state, AppActions& actions, int hoveredScene);
    auto SelectedEntry(const model::Scene& scene) const -> int;
    auto NewEntryLook(const model::Scene& scene) const -> model::VideoLook;

    bool m_layoutBuilt{false};
    int m_showBrowserTab{0}; //!< Frames until the browser tab is brought to front.
    std::vector<std::string> m_audioDevices;
    bool m_listsLoaded{false};
    int m_scrollToScene{-1};
    bool m_videosHovered{false};
    std::string m_entryScene; //!< Scene id of the selected playlist entry (whose look the Video sliders edit).
    int m_entry{0}; //!< Last frame: Finder drops add to the pool instead of a scene.

    // Preset browser.
    library::Query m_query;
    library::Query m_lastQuery;
    uint64_t m_lastLibraryVersion{~0ull};
    std::vector<int> m_results;
    std::string m_selectedPreset;
};

} // namespace gv::app
