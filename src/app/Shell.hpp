#pragma once

#include "app/GraphEditor.hpp"
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
#include <functional>
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
    /// Edits scene `scene`'s graph through model::TryEditGraph: commits it as one undoable step if the graph
    /// stays valid, else changes nothing and returns every error.
    virtual auto EditGraph(int scene, const std::function<void(model::Graph&)>& edit, const std::string& label,
                           const std::string& coalesceKey = {}) -> std::vector<model::GraphError> = 0;
    virtual void Undo() = 0;
    virtual void Redo() = 0;

    // Playback and setup
    virtual void Trigger(int scene) = 0;
    virtual void TriggerItem(int scene, int item) = 0; //!< Go live on a preset entry (Scene::presetEntries).
    /// Switches switcher `switcherId` of scene `scene` to `input` now (see NodeContext::JumpSwitcher).
    virtual void JumpSwitcher(int scene, const std::string& switcherId, int input) = 0;
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
    float pinch{1.f}; //!< Trackpad pinch since the last frame, as a zoom factor (1: none).
    const std::deque<float>* frameIntervalsMs{nullptr};
    long long slowFrames{0};
    double liveGpuMs{-1.0}; //!< The live lane's GPU time over the last second; -1 if not known.
};

/// The editor: preview, scene list, scene properties, the node canvas, setup, preset browser, status and messages.
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
    void DrawGraph(ShellState& state, AppActions& actions);
    void DrawSetup(ShellState& state, AppActions& actions);
    void DrawCanvas(const ShellState& state, AppActions& actions);
    void DrawBrowser(ShellState& state, AppActions& actions);
    void DrawPresetGrid(ShellState& state, AppActions& actions);
    void AssignPreset(ShellState& state, AppActions& actions, const std::filesystem::path& preset, int scene);
    /// Puts the preset in the live scene, in place of the preset entry on screen if it switches presets.
    /// Returns false when nothing is live.
    auto ReplaceLivePreset(const ShellState& state, AppActions& actions, const std::filesystem::path& preset) -> bool;
    void DoubleClickPreset(const ShellState& state, AppActions& actions, const std::filesystem::path& preset);
    void DrawVideos(ShellState& state, AppActions& actions);
    /// The selected scene's nodes; clicking one selects it on the canvas and centres the view on it.
    void DrawNodes(ShellState& state, AppActions& actions);
    void DrawStatus(const ShellState& state);
    void DrawMessages(const ShellState& state);
    void DrawStatusBar(ShellState& state, AppActions& actions);
    /// The edge strips of collapsed areas, each with a button that brings its area back.
    void DrawCollapsedStrips(ShellState& state, AppActions& actions);
    /// A hide button in the tab bar of each open area.
    void DrawHideButtons(ShellState& state, AppActions& actions);
    /// Left/Right (and Page Up/Down) step through the scenes, whether or not the scene list is shown.
    void HandleSceneStepKeys(const ShellState& state, AppActions& actions);
    void HandleDrops(ShellState& state, AppActions& actions, int hoveredScene);
    /// Puts `preset` in scene `index` of `edited`: in the preset node selected on its canvas, else its first.
    void PutPreset(model::Performance& edited, int index, const std::filesystem::path& preset) const;

    bool m_layoutBuilt{false};
    int m_showBrowserTab{0}; //!< Frames until the browser tab is brought to front.
    std::vector<std::string> m_audioDevices;
    bool m_listsLoaded{false};
    int m_scrollToScene{-1};
    int m_canvasWidthField{-1};  //!< Canvas size being typed (-1: not editing).
    int m_canvasHeightField{-1};
    bool m_videosHovered{false}; //!< Last frame: Finder drops add to the pool instead of a scene.
    std::string m_nodeFilter;    //!< The Nodes pane's filter.
    GraphEditor m_graph;
    std::vector<thumbnails::Request> m_gridVisible; //!< The browser's thumbnails this frame.
    std::vector<thumbnails::Request> m_gridPrefetch;

    // Preset browser.
    library::Query m_query;
    library::Query m_lastQuery;
    uint64_t m_lastLibraryVersion{~0ull};
    std::vector<int> m_results;
    std::string m_selectedPreset;
};

} // namespace gv::app
