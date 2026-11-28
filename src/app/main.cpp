// Zebra: editor window, performance output and the main loop.

#include "app/Settings.hpp"
#include "app/Shell.hpp"
#include "app/VideoManager.hpp"
#include "app/macos/FileDialog.hpp"
#include "app/macos/Menu.hpp"
#include "app/macos/Window.hpp"
#include "library/Library.hpp"
#include "library/PresetParsing.hpp"
#include "model/Document.hpp"
#include "thumbnails/ThumbnailService.hpp"
#include "app_info.hpp"
#include "common/FitRect.hpp"
#include "common/SpscRingBuffer.hpp"
#include "common/log.hpp"
#include "common/paths.hpp"
#include "common/memory.hpp"
#include "engine/Engine.hpp"
#include "io/AudioInput.hpp"
#include "io/MidiInput.hpp"
#include "model/PerformanceJson.hpp"

#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <OpenGL/gl3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kLogCategory = "app";
constexpr int kLanes = 3;
constexpr size_t kFrameHistory = 240;
constexpr size_t kGpuWindowFrames = 60; //!< One second at 60 fps.
constexpr int kGpuSettleFrames = 30;
constexpr size_t kMaxMessages = 200;
/// Smoke edits: sets every Saturation node feeding the `item`-th preset of a preset switcher (both of its
/// classic look chains, so the scene stays classic-shaped).
void SetEntrySaturation(gv::model::Graph& graph, int item, float saturation)
{
    const auto selection = gv::engine::SelectionForItem(graph, item);
    if (selection.empty())
        return;
    const auto* preset = graph.Upstream(selection.begin()->first, selection.begin()->second);
    if (preset == nullptr)
        return;
    for (const auto& wire : graph.wires)
    {
        if (wire.toNode != preset->id)
            continue;
        for (auto* node = graph.Find(wire.fromNode); node != nullptr && gv::model::IsEffect(node->type);)
        {
            if (node->type == gv::model::NodeType::Saturation)
                gv::model::ParamsOf<gv::model::SaturationParams>(*node).saturation = saturation;
            const auto* up = graph.Upstream(node->id, 0);
            node = up != nullptr ? graph.Find(up->id) : nullptr;
        }
    }
}

constexpr int kMaxRenderWidth = 1920; //!< Keeps Retina screens at a 1080p-class render (the M1 budgets).
constexpr size_t kMaxRecent = 10;
constexpr int kRecoveryIntervalFrames = 60 * 30; //!< Write a recovery copy of unsaved changes every ~30 s.

using Clock = std::chrono::steady_clock;

struct Options
{
    std::optional<std::filesystem::path> performance; //!< Opened instead of the last one.
    // Headless check: play `performance` cutting every `smokeCutSeconds`, report, and quit.
    bool smoke{false};
    double smokeSeconds{20.0};
    double smokeCutSeconds{2.0};
    bool smokeNoEditor{false}; //!< Skip drawing the editor (to measure its cost).
    bool smokeFocus{false};    //!< Start in focus mode: every panel around the Preview and Graph collapsed.
    bool smokeEdits{false};    //!< Script edits during playback (reorder, change the live preset, delete live, undo).
    bool smokeRandom{false};   //!< Trigger scenes in random order instead of sequentially.
    bool smokeCrash{false};    //!< Edit, write the recovery copy, then exit abruptly (crash-recovery drill).
    bool smokeRecover{false};  //!< Restore a recovery copy at launch, as a normal launch would.
    double smokeReportSeconds{0}; //!< If > 0, log memory and playback stats this often (soak tests).
    std::filesystem::path smokeSnapshots; //!< If set, saves the output just before each cut as a BMP here.
    std::filesystem::path smokeEditorSnapshot; //!< If set, saves the editor window once as a BMP...
    double smokeEditorSnapshotAt{3.0};         //!< ...this many seconds in.
    /// Moves every scene's feedback video into another preset input when the show opens: "before-composite",
    /// "on-top", "texture" (each preset's first texture) or "texture:<name>". With --smoke-edits in texture modes, the
    /// live scene's texture input is also detached and re-attached during each cut (live attach and detach).
    std::string smokeLayer;
};

auto ParseOptions(int argc, char* argv[]) -> Options
{
    Options options;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--performance" && hasValue)
            options.performance = argv[++i];
        else if (arg == "--smoke")
            options.smoke = true;
        else if (arg == "--smoke-seconds" && hasValue)
            options.smokeSeconds = std::atof(argv[++i]);
        else if (arg == "--smoke-snapshots" && hasValue)
            options.smokeSnapshots = argv[++i];
        else if (arg == "--smoke-editor-snapshot" && hasValue)
            options.smokeEditorSnapshot = argv[++i];
        else if (arg == "--smoke-editor-snapshot-at" && hasValue)
            options.smokeEditorSnapshotAt = std::atof(argv[++i]);
        else if (arg == "--smoke-crash")
            options.smokeCrash = true;
        else if (arg == "--smoke-recover")
            options.smokeRecover = true;
        else if (arg == "--smoke-random")
            options.smokeRandom = true;
        else if (arg == "--smoke-report-every" && hasValue)
            options.smokeReportSeconds = std::atof(argv[++i]);
        else if (arg == "--smoke-edits")
            options.smokeEdits = true;
        else if (arg == "--smoke-focus")
            options.smokeFocus = true;
        else if (arg == "--smoke-no-editor")
            options.smokeNoEditor = true;
        else if (arg == "--smoke-layer" && hasValue)
            options.smokeLayer = argv[++i];
        else if (arg == "--smoke-cut-every" && hasValue)
            options.smokeCutSeconds = std::atof(argv[++i]);
        else if (arg.size() > 5 && arg.substr(arg.size() - 5) == ".json")
            options.performance = arg; // also accept a bare performance path
    }
    return options;
}

class App final : public gv::app::AppActions
{
public:
    App(SDL_Window* editorWindow, SDL_GLContext editorContext, Options options)
        : m_editorWindow(editorWindow)
        , m_editorContext(editorContext)
        , m_options(std::move(options))
        , m_ring(48000 * 2)
        , m_audio(m_ring)
    {
    }

    auto Initialize() -> bool
    {
        m_settingsFile = gv::paths::ApplicationSupportDir() / "settings.json";
        m_settings = gv::app::Settings::Load(m_settingsFile);
        if (m_options.smoke)
            m_settings.panes = {}; // smoke runs show the default layout, whatever the user collapsed
        if (m_options.smokeFocus)
            m_settings.panes.ToggleFocus();

        if (!m_engine.Start(kLanes, gv::engine::PoolConfig{kLanes, 1, 3}))
        {
            return false;
        }
        m_engine.SetVideoProvider(&m_video);
        {
            std::vector<std::string> texturePaths;
            for (const auto& path : m_settings.texturePaths)
                texturePaths.push_back(path.string());
            if (!m_thumbnails.Start(gv::paths::ApplicationSupportDir() / "thumbnails", texturePaths))
                AddMessage("Preset previews are unavailable (see the log).");
        }
        ApplyTexturePaths();

        m_midi = gv::io::CreateMidiInput();
        m_midi->SetChannel(m_settings.midiChannel);

        if (!m_audio.Open(m_settings.audioDevice) && !m_settings.audioDevice.empty())
        {
            AddMessage("Audio input \"" + m_settings.audioDevice + "\" isn't available; using the system default.");
            m_audio.Open("");
        }

        UpdateRenderSize();
        gv::app::SetRecentFiles(m_settings.recentPerformances);
        InitializeLibrary();

        if (m_options.performance)
            OpenPerformance(*m_options.performance);
        else if (!m_settings.lastPerformance.empty())
            OpenPerformance(m_settings.lastPerformance);
        else
            LoadDocument(gv::model::Document());
        if (!m_options.smoke || m_options.smokeRecover)
            RestoreRecovery();
        return true;
    }

    void Shutdown()
    {
        SaveSettings();
        SaveLibrary();
        if (!m_document.IsDirty())
            DeleteRecovery();
        m_thumbnails.Stop();
        m_audio.Close();
        m_midi.reset();
        m_engine.Shutdown();
    }

    // --- AppActions -----------------------------------------------------------------------

    void NewPerformance() override
    {
        if (!ConfirmDiscardChanges())
            return;
        LoadDocument(gv::model::Document());
        AddMessage("New performance.");
    }

    void OpenPerformance(const std::filesystem::path& file) override
    {
        if (!ConfirmDiscardChanges())
            return;
        auto result = gv::model::LoadPerformance(file);
        if (!result.ok)
        {
            m_issues = result.issues;
            AddMessage("Could not open " + file.string() + " (see the errors above).");
            GV_LOG_ERROR(kLogCategory, "could not open performance %s", file.c_str());
            return;
        }
        if (m_options.smoke && !m_options.smokeLayer.empty())
            ApplySmokeLayer(result.performance);
        LoadDocument(gv::model::Document(std::move(result.performance), file));
        m_issues = result.issues;
        m_settings.lastPerformance = file;
        AddRecent(file);
        SaveSettings();
        AddMessage("Opened " + file.filename().string() + ": " + std::to_string(m_document.Get().scenes.size()) + " scenes.");
    }

    void SavePerformance() override
    {
        if (m_document.Path().empty())
        {
            SavePerformanceAs();
            return;
        }
        SaveTo(m_document.Path());
    }

    void SavePerformanceAs() override
    {
        if (auto file = gv::app::ChooseSaveFile("Save Performance", m_document.Title() + ".json", "json"))
            SaveTo(*file);
    }

    void RevertPerformance() override
    {
        if (m_document.Path().empty())
            return;
        const auto path = m_document.Path();
        if (m_document.IsDirty())
        {
            // Reverting is the explicit "discard": no save prompt, but mark clean so OpenPerformance doesn't ask.
            m_document.MarkSaved(path);
        }
        OpenPerformance(path);
    }

    void Edit(gv::model::Performance edited, const std::string& label, const std::string& coalesceKey) override
    {
        m_document.Edit(std::move(edited), label, coalesceKey);
        SyncEngine();
    }

    void EndEdit() override
    {
        m_document.EndCoalescing();
    }

    auto EditGraph(int scene, const std::function<void(gv::model::Graph&)>& edit, const std::string& label, const std::string& coalesceKey)
        -> std::vector<gv::model::GraphError> override
    {
        auto errors = gv::model::TryEditGraph(m_document, scene, edit, label, coalesceKey);
        if (errors.empty())
            SyncEngine();
        return errors;
    }

    void Undo() override
    {
        m_document.Undo();
        SyncEngine();
    }

    void Redo() override
    {
        m_document.Redo();
        SyncEngine();
    }

    /// Cmd+F, Esc and the Fullscreen button: only the visuals, on a full screen with no menu bar. macOS full
    /// screen (green button, Globe+F) is separate and keeps the editor; this enters it too if needed and
    /// leaves it again afterwards.
    void ToggleFullscreen() override
    {
        m_fullscreen = !m_fullscreen;
        const bool native = gv::app::IsNativeFullscreen(m_editorWindow);
        if (m_fullscreen)
        {
            m_nativeForFullscreen = !native;
            m_nativeSeen = native;
            if (!native)
                gv::app::ToggleNativeFullscreen(m_editorWindow);
        }
        else
        {
            gv::app::SetMenuBarHidden(false);
            m_menuBarHidden = false;
            if (m_nativeForFullscreen && native)
                gv::app::ToggleNativeFullscreen(m_editorWindow);
            m_nativeForFullscreen = false;
        }
        SDL_ShowCursor(m_fullscreen ? SDL_DISABLE : SDL_ENABLE);
        GV_LOG_INFO(kLogCategory, "fullscreen visuals %s", m_fullscreen ? "on" : "off");
    }

    /// Hides the menu bar once macOS full screen has started, and leaves the visuals view if the window
    /// leaves macOS full screen some other way (Globe+F, the green button).
    void SyncFullscreen()
    {
        if (!m_fullscreen)
            return;
        const bool native = gv::app::IsNativeFullscreen(m_editorWindow);
        if (native && !m_menuBarHidden)
        {
            gv::app::SetMenuBarHidden(true);
            m_menuBarHidden = true;
        }
        if (native)
            m_nativeSeen = true;
        else if (m_nativeSeen)
        {
            m_nativeForFullscreen = false;
            ToggleFullscreen();
        }
    }

    void SetAudioDevice(const std::string& deviceName) override
    {
        m_settings.audioDevice = deviceName;
        if (!m_audio.Open(deviceName))
            AddMessage("Could not open audio input \"" + deviceName + "\".");
        SaveSettings();
    }

    void SetMidiChannel(int channel) override
    {
        m_settings.midiChannel = channel;
        m_midi->SetChannel(channel);
        SaveSettings();
    }

    void SetLibraryRoot(const std::filesystem::path& folder) override
    {
        m_settings.libraryRoot = folder;
        SaveSettings();
        RescanLibrary();
    }

    void ToggleFavourite(const std::string& presetId) override
    {
        m_favourites.Toggle(presetId);
        if (auto error = m_favourites.Save(gv::paths::ApplicationSupportDir() / "favourites.json"); !error.empty())
            AddMessage("Could not save favourites: " + error);
        m_libraryVersion++;
    }

    void RescanLibrary() override
    {
        if (!m_settings.libraryRoot.empty())
            m_indexer.Start(m_settings.libraryRoot, m_library);
    }

    void SetTextureFolder(const std::filesystem::path& folder) override
    {
        m_settings.texturePaths = {folder};
        ApplyTexturePaths();
        m_availableTextures = gv::library::AvailableTextures(m_settings.texturePaths);
        m_thumbnails.SetTexturePaths({folder.string()});
        m_libraryVersion++;
        SaveSettings();
        AddMessage("Texture folder changed; it applies to scenes prepared from now on.");
    }

    void SettingsChanged() override
    {
        m_settingsDirty = true;
    }

    void Trigger(int scene) override
    {
        m_video.OnTrigger(scene, 0, m_engine.LiveScene());
        m_engine.Trigger(scene);
    }

    void JumpSwitcher(int scene, const std::string& switcherId, int input) override
    {
        const auto& scenes = m_document.Get().scenes;
        if (scene < 0 || scene >= static_cast<int>(scenes.size()))
            return;
        const auto& target = scenes[static_cast<size_t>(scene)];
        if (switcherId == target.presetSwitcher)
        {
            // A preset switcher's input is a preset entry: go live on it, as from the Scenes list.
            if (const int entry = target.EntryOfInput(input); entry >= 0)
                TriggerItem(scene, entry);
            return;
        }
        m_engine.JumpVideo(scene, switcherId, input);
    }

    void ResetShader(int scene, const std::string& nodeId) override
    {
        m_engine.ResetShader(scene, nodeId);
    }

    void TriggerItem(int scene, int item) override
    {
        m_video.OnTrigger(scene, item, m_engine.LiveScene()); // only restarts when entering another scene
        m_engine.TriggerItem(scene, item);
    }

    /// Renders at the show's canvas size, or with Match screen at the native resolution of the screen the window
    /// is on, capped at kMaxRenderWidth wide. Cheap when nothing changed.
    void UpdateRenderSize()
    {
        if (const auto& canvas = m_document.Get().canvas; !canvas.matchScreen)
        {
            m_engine.SetRenderSize(canvas.width, canvas.height);
            return;
        }
        const int display = SDL_GetWindowDisplayIndex(m_editorWindow);
        SDL_DisplayMode mode{};
        if (display < 0 || SDL_GetDesktopDisplayMode(display, &mode) != 0)
        {
            return;
        }
        // Desktop modes are in points; scale by the window's pixel density to get the screen's pixels.
        int windowWidth = 0;
        int windowHeight = 0;
        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_GetWindowSize(m_editorWindow, &windowWidth, &windowHeight);
        SDL_GL_GetDrawableSize(m_editorWindow, &drawableWidth, &drawableHeight);
        const double density = windowWidth > 0 ? static_cast<double>(drawableWidth) / windowWidth : 1.0;
        int width = static_cast<int>(mode.w * density);
        int height = static_cast<int>(mode.h * density);
        if (width > kMaxRenderWidth)
        {
            height = static_cast<int>(static_cast<double>(height) * kMaxRenderWidth / width);
            width = kMaxRenderWidth;
        }
        m_engine.SetRenderSize(width, height);
    }

    // --- Preset library -------------------------------------------------------------------------

    void InitializeLibrary()
    {
        const auto dir = gv::paths::ApplicationSupportDir();
        m_library.Load(dir / "library.json");
        if (m_library.Root() != m_settings.libraryRoot)
            m_library = gv::library::Library();
        m_favourites.Load(dir / "favourites.json");
        m_availableTextures = gv::library::AvailableTextures(m_settings.texturePaths);
        RescanLibrary(); // background; quick when nothing changed
    }

    /// Takes a finished scan, and records what the engine learned about presets (heavy, broken).
    void UpdateLibrary(int frame)
    {
        if (auto scanned = m_indexer.TakeResult())
        {
            m_library = std::move(*scanned);
            m_libraryVersion++;
            m_libraryDirty = true;
            SaveLibrary();
        }
        if (frame % 120 == 0)
        {
            bool changed = false;
            for (const auto& measurement : m_engine.PresetMeasurements())
                changed |= m_library.Record(measurement.path, measurement.frameMs, measurement.error);
            if (changed)
            {
                m_libraryDirty = true;
                m_libraryVersion++;
            }
        }
        for (const auto& finding : m_thumbnails.TakeFindings())
        {
            if (m_library.Record(finding.path, 0.0, finding.error))
            {
                m_libraryDirty = true;
                m_libraryVersion++;
            }
        }
        // Previews never compete with a fullscreen performance.
        m_thumbnails.SetPaused(m_fullscreen);
        if (m_libraryDirty && frame % 1800 == 0)
            SaveLibrary();
    }

    void SaveLibrary()
    {
        if (!m_libraryDirty || m_library.Root().empty())
            return;
        if (auto error = m_library.Save(gv::paths::ApplicationSupportDir() / "library.json"); !error.empty())
            GV_LOG_ERROR(kLogCategory, "%s", error.c_str());
        m_libraryDirty = false;
    }

    // --- Document -----------------------------------------------------------------------------

    /// Replaces the open document and restarts playback with it.
    void LoadDocument(gv::model::Document document)
    {
        m_document = std::move(document);
        m_selectedScene = m_document.Get().scenes.empty() ? -1 : 0;
        m_issues.clear();
        SDL_GL_MakeCurrent(m_editorWindow, m_editorContext); // video sources convert frames on this context
        UpdateRenderSize(); // before preparing, so the lanes don't prepare at the old size
        m_video.Load(m_document.Get());
        m_engine.LoadPerformance(m_document.Get());
        m_syncedRevision = m_document.Revision();
    }

    /// Hands edits to the engine and video sources without stopping playback, and re-validates.
    void SyncEngine()
    {
        if (m_document.Revision() == m_syncedRevision)
            return;
        m_syncedRevision = m_document.Revision();
        SDL_GL_MakeCurrent(m_editorWindow, m_editorContext);
        UpdateRenderSize(); // the canvas may have been edited (or undone)
        m_video.Load(m_document.Get());
        m_engine.UpdatePerformance(m_document.Get());
        m_issues = gv::model::Validate(m_document.Get());
    }

    void SaveTo(const std::filesystem::path& file)
    {
        const auto issues = gv::model::Validate(m_document.Get());
        bool hasErrors = false;
        for (const auto& issue : issues)
            hasErrors = hasErrors || issue.severity == gv::model::Issue::Severity::Error;
        if (hasErrors)
        {
            m_issues = issues;
            AddMessage("Not saved: fix the problems listed above first.");
            return;
        }
        const auto backup = gv::model::BackupOlderFormat(file);
        if (auto error = gv::model::SavePerformance(m_document.Get(), file); !error.empty())
        {
            AddMessage("Could not save: " + error);
            return;
        }
        if (!backup.empty())
            AddMessage("Saved in the new format. The old file was kept as " + backup.filename().string() + " for older versions of the app.");
        m_document.MarkSaved(file);
        m_settings.lastPerformance = file;
        AddRecent(file);
        SaveSettings();
        DeleteRecovery();
        AddMessage("Saved " + file.filename().string() + ".");
    }

    /// Asks to save unsaved changes. Returns false if the user cancelled.
    auto ConfirmDiscardChanges() -> bool
    {
        if (!m_document.IsDirty() || m_options.smoke)
            return true;
        switch (gv::app::AskToSaveChanges(m_document.Title()))
        {
            case gv::app::SaveChoice::Save:
                SavePerformance();
                return !m_document.IsDirty();
            case gv::app::SaveChoice::DontSave:
                DeleteRecovery();
                return true;
            case gv::app::SaveChoice::Cancel:
                return false;
        }
        return false;
    }

    void AddRecent(const std::filesystem::path& file)
    {
        auto& recent = m_settings.recentPerformances;
        recent.erase(std::remove(recent.begin(), recent.end(), file), recent.end());
        recent.insert(recent.begin(), file);
        if (recent.size() > kMaxRecent)
            recent.resize(kMaxRecent);
        gv::app::SetRecentFiles(recent);
    }

    auto RecoveryFile() const -> std::filesystem::path
    {
        return gv::paths::ApplicationSupportDir() / "recovery.json";
    }

    /// Writes unsaved changes so a crash doesn't lose them.
    void WriteRecovery()
    {
        if (!m_document.IsDirty() || (m_options.smoke && !m_options.smokeCrash))
            return;
        gv::model::SavePerformance(m_document.Get(), RecoveryFile());
        std::ofstream(RecoveryFile().string() + ".path") << m_document.Path().string();
    }

    void DeleteRecovery()
    {
        std::error_code ec;
        std::filesystem::remove(RecoveryFile(), ec);
        std::filesystem::remove(RecoveryFile().string() + ".path", ec);
    }

    /// After a crash: reopens the unsaved changes from the recovery copy.
    void RestoreRecovery()
    {
        std::error_code ec;
        if (!std::filesystem::exists(RecoveryFile(), ec))
            return;
        auto result = gv::model::LoadPerformance(RecoveryFile());
        std::string path;
        std::getline(std::ifstream(RecoveryFile().string() + ".path"), path);
        if (!result.ok)
        {
            DeleteRecovery();
            return;
        }
        auto recovered = result.performance;
        LoadDocument(gv::model::Document(std::move(result.performance), path));
        m_document.Edit(std::move(recovered), "Recover Unsaved Changes"); // marks it unsaved
        m_syncedRevision = m_document.Revision();
        AddMessage("Recovered unsaved changes from your last session. Save to keep them, or File > Revert to Saved to discard them.");
        GV_LOG_INFO(kLogCategory, "recovered \"%s\" (unsaved: %d) from %s", m_document.Title().c_str(), m_document.IsDirty(), RecoveryFile().c_str());
    }

    void HandleMenuCommands(bool& running)
    {
        using gv::app::MenuCommand;
        for (int command : gv::app::TakeMenuCommands())
        {
            if (command >= static_cast<int>(MenuCommand::OpenRecent))
            {
                const size_t index = static_cast<size_t>(command - static_cast<int>(MenuCommand::OpenRecent));
                if (index < m_settings.recentPerformances.size())
                    OpenPerformance(m_settings.recentPerformances[index]);
                continue;
            }
            auto edited = m_document.Get();
            switch (static_cast<MenuCommand>(command))
            {
                case MenuCommand::New:
                    NewPerformance();
                    break;
                case MenuCommand::Open:
                    if (auto file = gv::app::ChooseFile("Open Performance", {"json"}))
                        OpenPerformance(*file);
                    break;
                case MenuCommand::Save:
                    SavePerformance();
                    break;
                case MenuCommand::SaveAs:
                    SavePerformanceAs();
                    break;
                case MenuCommand::Reload:
                    RevertPerformance();
                    break;
                case MenuCommand::Undo:
                    Undo();
                    break;
                case MenuCommand::Redo:
                    Redo();
                    break;
                case MenuCommand::AddScene: {
                    const auto preset = m_selectedScene >= 0 ? edited.scenes[static_cast<size_t>(m_selectedScene)].FirstPreset() : std::filesystem::path();
                    if (const int index = gv::model::AddScene(edited, m_selectedScene, preset); index >= 0)
                    {
                        Edit(std::move(edited), "Add Scene", {});
                        m_selectedScene = index;
                    }
                    break;
                }
                case MenuCommand::DuplicateScene:
                    if (const int index = gv::model::DuplicateScene(edited, m_selectedScene); index >= 0)
                    {
                        Edit(std::move(edited), "Duplicate Scene", {});
                        m_selectedScene = index;
                    }
                    break;
                case MenuCommand::DeleteScene:
                    if (gv::model::DeleteScene(edited, m_selectedScene))
                        Edit(std::move(edited), "Delete Scene", {});
                    break;
                case MenuCommand::TriggerSelected:
                    Trigger(m_selectedScene);
                    break;
                case MenuCommand::Fullscreen:
                    ToggleFullscreen();
                    break;
                case MenuCommand::ToggleLeftPanes:
                    m_settings.panes.Toggle(gv::app::PaneArea::Left);
                    m_settingsDirty = true;
                    break;
                case MenuCommand::ToggleRightPanes:
                    m_settings.panes.Toggle(gv::app::PaneArea::Right);
                    m_settingsDirty = true;
                    break;
                case MenuCommand::ToggleBottomPanes:
                    m_settings.panes.Toggle(gv::app::PaneArea::Bottom);
                    m_settingsDirty = true;
                    break;
                case MenuCommand::FocusMode:
                    m_settings.panes.ToggleFocus();
                    m_settingsDirty = true;
                    break;
                case MenuCommand::ClearRecent:
                    m_settings.recentPerformances.clear();
                    gv::app::SetRecentFiles({});
                    SaveSettings();
                    break;
                case MenuCommand::OpenRecent:
                    break;
            }
        }
        (void) running;
    }

    /// Menu item states and the window title, when they change.
    void UpdateChrome()
    {
        using gv::app::MenuCommand;
        const bool hasScene = m_selectedScene >= 0;
        gv::app::SetMenuEnabled(MenuCommand::Undo, m_document.CanUndo());
        gv::app::SetMenuEnabled(MenuCommand::Redo, m_document.CanRedo());
        gv::app::SetMenuEnabled(MenuCommand::Reload, !m_document.Path().empty() && m_document.IsDirty());
        gv::app::SetMenuEnabled(MenuCommand::DuplicateScene, hasScene);
        gv::app::SetMenuEnabled(MenuCommand::DeleteScene, hasScene);
        gv::app::SetMenuEnabled(MenuCommand::TriggerSelected, hasScene);
        gv::app::SetMenuChecked(MenuCommand::ToggleLeftPanes, !m_settings.panes.IsCollapsed(gv::app::PaneArea::Left));
        gv::app::SetMenuChecked(MenuCommand::ToggleRightPanes, !m_settings.panes.IsCollapsed(gv::app::PaneArea::Right));
        gv::app::SetMenuChecked(MenuCommand::ToggleBottomPanes, !m_settings.panes.IsCollapsed(gv::app::PaneArea::Bottom));
        gv::app::SetMenuChecked(MenuCommand::FocusMode, m_settings.panes.focus);
        gv::app::SetUndoRedoLabels(m_document.UndoLabel(), m_document.RedoLabel());

        const std::string title = m_document.Title() + (m_document.IsDirty() ? " \u2014 Edited" : "") + " \u2014 " + gv::app_info::kName;
        if (title != m_windowTitle)
        {
            m_windowTitle = title;
            SDL_SetWindowTitle(m_editorWindow, title.c_str());
        }
    }

    // --- Main loop ------------------------------------------------------------------------

    void HandleEvent(const SDL_Event& event, bool& running)
    {
        if (event.type == SDL_QUIT || (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE))
        {
            if (ConfirmDiscardChanges())
                running = false;
        }
        else if (event.type == SDL_DROPFILE)
        {
            m_droppedFiles.emplace_back(event.drop.file);
            SDL_free(event.drop.file);
        }
        else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED)
        {
            UpdateRenderSize(); // moved to another screen
        }
        else if (m_audio.HandleEvent(event))
        {
            AddMessage("The audio input was removed.");
        }
        else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE && m_fullscreen)
        {
            ToggleFullscreen();
        }
    }

    /// One output frame. Returns false when the smoke run is over.
    auto Frame(gv::app::Shell& shell, int frame) -> bool
    {
        bool running = true;
        HandleMenuCommands(running);
        SyncFullscreen();
        m_pinch = gv::app::TakePinch(); // collected while SDL pumped this frame's events

        // MIDI scene triggers.
        if (auto value = m_midi->TakeSceneTrigger())
        {
            if (auto scene = m_document.Get().SceneForCc20(*value))
                Trigger(static_cast<int>(*scene));
            else
                GV_LOG_INFO(kLogCategory, "CC 20 = %d doesn't match any scene", *value);
        }

        if (m_options.smoke)
        {
            SmokeStep();
        }

        // Audio: everything captured since the last frame.
        const size_t samples = m_ring.Read(m_pcm.data(), m_pcm.size());
        SDL_GL_MakeCurrent(m_editorWindow, m_editorContext);
        m_video.SetCanvasSize(m_engine.RenderWidth(), m_engine.RenderHeight());
        const auto midiClock = m_midi->Clock();
        const gv::engine::ClockInput clock{midiClock.receiving, midiClock.running, midiClock.positionKnown, midiClock.pulses, midiClock.totalPulses};
        m_engine.SetClock(clock); // Tick moves the switchers on, then tells the video sources what to play
        m_engine.SetSampleRate(m_audio.SampleRate());
        m_engine.Tick(m_pcm.data(), samples / 2, m_settings.gain);

        SDL_GL_MakeCurrent(m_editorWindow, m_editorContext);
        const GLuint presented = m_engine.Present();

        if (m_options.smokeNoEditor)
        {
            glFlush();
            PaceSmoke();
        }
        else if (m_fullscreen)
        {
            DrawFullscreen(presented);
        }
        else
        {
            DrawEditor(shell, presented);
        }

        RecordFrameInterval();
        RecordLiveGpu();
        UpdateChrome();
        UpdateLibrary(frame);
        if (m_settingsDirty && frame % 60 == 0)
        {
            SaveSettings();
        }
        if (frame % kRecoveryIntervalFrames == kRecoveryIntervalFrames - 1)
        {
            WriteRecovery();
        }
        return !m_smokeDone;
    }

    auto SmokeSucceeded() const -> bool
    {
        return m_smokeOk;
    }

private:
    void DrawEditor(gv::app::Shell& shell, GLuint presented)
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        gv::app::ShellState state;
        state.document = &m_document;
        state.selectedScene = &m_selectedScene;
        state.droppedFiles = &m_droppedFiles;
        state.issues = &m_issues;
        state.messages = &m_messages;
        state.settings = &m_settings;
        state.engine = &m_engine;
        state.midi = m_midi.get();
        state.audio = &m_audio;
        state.video = &m_video;
        state.library = &m_library;
        state.favourites = &m_favourites;
        state.availableTextures = &m_availableTextures;
        state.libraryVersion = m_libraryVersion;
        state.indexing = m_indexer.Running();
        state.indexProgress = m_indexer.Progress();
        state.thumbnails = &m_thumbnails;
        state.preview = presented;
        state.pinch = m_pinch;
        state.frameIntervalsMs = &m_frameIntervals;
        state.slowFrames = m_slowFrames;
        state.liveGpuMs = LiveGpuMs();
        shell.Draw(state, *this);
        m_document.ReleaseRetired();

        ImGui::Render();
        int width = 0;
        int height = 0;
        SDL_GL_GetDrawableSize(m_editorWindow, &width, &height);
        if (m_engine.PresentFence() != nullptr)
        {
            glWaitSync(m_engine.PresentFence(), 0, GL_TIMEOUT_IGNORED);
        }
        if (GLsync previews = m_thumbnails.LatestFence(); previews != nullptr && glIsSync(previews))
        {
            glWaitSync(previews, 0, GL_TIMEOUT_IGNORED);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glClearColor(0.08f, 0.08f, 0.09f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (!m_options.smokeEditorSnapshot.empty() && !m_editorSnapshotTaken &&
            std::chrono::duration<double>(Clock::now() - m_smokeStart).count() > m_options.smokeEditorSnapshotAt)
        {
            std::vector<unsigned char> rgba(static_cast<size_t>(width) * height * 4);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            WriteBmp(m_options.smokeEditorSnapshot, rgba, width, height);
            m_editorSnapshotTaken = true;
        }
        SDL_GL_SwapWindow(m_editorWindow);
        if (m_options.smoke)
        {
            PaceSmoke(); // hidden windows don't wait for vsync
        }
    }

    /// Fullscreen: only the visuals, letterboxed, no UI.
    void DrawFullscreen(GLuint presented)
    {
        int width = 0;
        int height = 0;
        SDL_GL_GetDrawableSize(m_editorWindow, &width, &height);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        const int textureWidth = m_engine.RenderWidth();
        const int textureHeight = m_engine.RenderHeight();
        if (presented != 0 && textureWidth > 0 && textureHeight > 0)
        {
            if (m_readFbo == 0)
                glGenFramebuffers(1, &m_readFbo);
            const gv::Rect fit = gv::FitRect(textureWidth, textureHeight, width, height);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_readFbo);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, presented, 0);
            glBlitFramebuffer(0, 0, textureWidth, textureHeight, fit.x, fit.y, fit.x + fit.width, fit.y + fit.height, GL_COLOR_BUFFER_BIT,
                              GL_LINEAR);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        }
        SDL_GL_SwapWindow(m_editorWindow);
    }

    void RecordFrameInterval()
    {
        const auto now = Clock::now();
        if (m_lastFrame.time_since_epoch().count() != 0)
        {
            const float ms = std::chrono::duration<float, std::milli>(now - m_lastFrame).count();
            m_frameIntervals.push_back(ms);
            if (m_frameIntervals.size() > kFrameHistory)
                m_frameIntervals.pop_front();
            if (ms > 25.f)
                m_slowFrames++;
        }
        m_lastFrame = now;
    }

    /// Samples the live lane's GPU time: a one-second average for the editor, and per-scene averages for smoke runs.
    /// Skips the first half second after the live scene changes, while the new lane's timer average catches up.
    void RecordLiveGpu()
    {
        const int live = m_engine.LiveScene();
        const int lane = live >= 0 ? m_engine.Scene(live).lane : -1;
        if (live != m_gpuScene)
        {
            m_gpuScene = live;
            m_gpuSettle = kGpuSettleFrames;
            m_gpuWindow.clear();
        }
        const auto stats = m_engine.GetStats();
        if (lane < 0 || lane >= static_cast<int>(stats.laneGpuMs.size()) || m_gpuSettle-- > 0)
            return;
        const double ms = stats.laneGpuMs[static_cast<size_t>(lane)];
        m_gpuWindow.push_back(ms);
        if (m_gpuWindow.size() > kGpuWindowFrames)
            m_gpuWindow.pop_front();
        auto& total = m_gpuByScene[live];
        total.first += ms;
        total.second++;
    }

    /// The live lane's GPU ms over the last second, or -1 if not known.
    auto LiveGpuMs() const -> double
    {
        if (m_gpuWindow.empty())
            return -1.0;
        double sum = 0.0;
        for (double ms : m_gpuWindow)
            sum += ms;
        return sum / static_cast<double>(m_gpuWindow.size());
    }

    void PaceSmoke()
    {
        m_smokeDeadline += std::chrono::microseconds(16667);
        const auto now = Clock::now();
        if (m_smokeDeadline < now)
            m_smokeDeadline = now;
        std::this_thread::sleep_until(m_smokeDeadline);
    }

    void SmokeStep()
    {
        if (m_smokeStart.time_since_epoch().count() == 0)
        {
            m_smokeStart = Clock::now();
            m_smokeDeadline = m_smokeStart;
        }
        const double elapsed = std::chrono::duration<double>(Clock::now() - m_smokeStart).count();
        const auto& performance = m_document.Get();
        if (performance.scenes.empty())
        {
            GV_LOG_ERROR(kLogCategory, "smoke: no performance loaded");
            m_smokeDone = true;
            return;
        }
        if (m_options.smokeEdits)
        {
            SmokeEdits(elapsed);
            // Held ticks in the half second after each canvas edit (the edit's own cost).
            if (m_smokeCanvasEditAt >= 0.0 && elapsed >= m_smokeCanvasEditAt + 0.5)
            {
                const long long held = m_engine.GetStats().heldTicks - m_smokeCanvasHeldBefore;
                m_smokeCanvasHeld += held;
                GV_LOG_INFO(kLogCategory, "smoke edit (canvas): %lld held ticks in the 0.5 s after it", held);
                m_smokeCanvasEditAt = -1.0;
            }
        }
        if (m_options.smokeEdits && m_options.smokeLayer.rfind("texture", 0) == 0)
            SmokeToggleTexture(elapsed);
        if (m_options.smokeCrash && elapsed > 2.0)
        {
            auto edited = m_document.Get();
            edited.name = "recovered by the crash drill";
            Edit(std::move(edited), "Rename Performance", {});
            WriteRecovery();
            GV_LOG_INFO(kLogCategory, "smoke: simulating a crash with unsaved changes");
            std::_Exit(3);
        }
        // --smoke-cut-every 0: no automatic cuts (e.g. when MIDI drives the scenes).
        const int cut = m_options.smokeCutSeconds > 0 ? static_cast<int>(elapsed / m_options.smokeCutSeconds) : 0;
        if (cut != m_smokeLastCut && m_smokeLastCut >= 0 && !m_options.smokeSnapshots.empty())
        {
            SaveSnapshot(m_options.smokeSnapshots / ("cut" + std::to_string(m_smokeLastCut) + "-scene" + std::to_string(m_engine.LiveScene()) + ".bmp"));
        }
        if (cut != m_smokeLastCut)
        {
            m_smokeLastCut = cut;
            // Sequential, or random (out-of-order jumps, as a show with improvised triggers would do).
            const int scene = m_options.smokeRandom ? static_cast<int>(m_smokeRandom() % performance.scenes.size())
                                                    : cut % static_cast<int>(performance.scenes.size());
            Trigger(scene);
            m_smokeCuts++;
        }
        if (m_options.smokeReportSeconds > 0 && elapsed >= m_smokeNextReport)
        {
            m_smokeNextReport = elapsed + m_options.smokeReportSeconds;
            const auto stats = m_engine.GetStats();
            GV_LOG_INFO(kLogCategory, "soak: %.0f min, footprint %.0f MB, %d cuts, held %lld of %lld (%.2f%%), frames over 25 ms %lld, preview instances %d",
                        elapsed / 60.0, gv::PhysicalFootprintBytes() / 1048576.0, m_smokeCuts, stats.heldTicks, stats.ticks,
                        stats.ticks > 0 ? 100.0 * stats.heldTicks / stats.ticks : 0.0, m_slowFrames, m_thumbnails.LiveInstances());
        }
        if (elapsed >= m_options.smokeSeconds)
        {
            const auto stats = m_engine.GetStats();
            const long long liveTicks = stats.ticks;
            GV_LOG_INFO(kLogCategory, "smoke: %.1fs, %d cuts, %lld ticks, %lld presented, %lld held (%.2f%%: %lld waiting for a scene to prepare, %lld late within 0.5 s of a cut, %lld late otherwise), frames over 25 ms: %lld, pre-warm %d/%d",
                        elapsed, m_smokeCuts, liveTicks, stats.presentedFrames, stats.heldTicks,
                        liveTicks > 0 ? 100.0 * stats.heldTicks / liveTicks : 0.0, stats.heldWaitingTicks, stats.heldNearCutTicks, stats.heldTicks - stats.heldWaitingTicks - stats.heldNearCutTicks,
                        m_slowFrames, stats.warmDone, stats.warmTotal);
            GV_LOG_INFO(kLogCategory, "smoke: render size %dx%d (%s)", m_engine.RenderWidth(), m_engine.RenderHeight(),
                        performance.canvas.matchScreen ? "match screen" : "show canvas");
            for (int i = 0; i < static_cast<int>(performance.scenes.size()); ++i)
            {
                const auto info = m_engine.Scene(i);
                const auto held = stats.heldByScene.count(i) ? stats.heldByScene.at(i) : 0;
                const auto source = m_engine.VideoSourceOf(i);
                const auto video = source ? m_video.StateOf(*source) : std::nullopt;
                char dropped[32] = "never live long enough";
                if (info.droppedFraction >= 0.0)
                    std::snprintf(dropped, sizeof(dropped), "%.1f%% of its live frames", info.droppedFraction * 100.0);
                char gpu[32] = "-";
                if (auto it = m_gpuByScene.find(i); it != m_gpuByScene.end() && it->second.second > 0)
                    std::snprintf(gpu, sizeof(gpu), "%.1f ms", it->second.first / static_cast<double>(it->second.second));
                GV_LOG_INFO(kLogCategory, "smoke: scene %d %-10s %6.1f ms/frame%s, live GPU %s, held %lld (%s), video %s", i,
                            performance.scenes[static_cast<size_t>(i)].id.c_str(), info.frameMs, info.heavy ? " (heavy)" : "", gpu, held,
                            dropped, video ? gv::io::ToString(*video) : "-");
            }
            const auto clock = m_midi->Clock();
            GV_LOG_INFO(kLogCategory, "smoke: MIDI clock %s, %.1f BPM, %s, position %s, source %s", clock.receiving ? "receiving" : "absent", clock.bpm,
                        clock.running ? "running" : "stopped", clock.positionKnown ? "known" : "unknown", clock.sourceName.c_str());
            if (m_options.smokeEdits)
                GV_LOG_INFO(kLogCategory, "smoke: canvas edits refused: %d, held ticks within 0.5 s after them: %lld", m_smokeCanvasRefusals, m_smokeCanvasHeld);
            m_smokeOk = stats.presentedFrames > liveTicks / 2 && m_smokeCanvasRefusals == 0;
            m_smokeDone = true;
        }
    }

    /// --smoke-layer: rewires each scene's feedback video into the chosen preset input.
    void ApplySmokeLayer(gv::model::Performance& performance)
    {
        using gv::model::PresetLayer;
        const std::string& layer = m_options.smokeLayer;
        const bool texture = layer == "texture" || layer.rfind("texture:", 0) == 0;
        if (!texture && layer != "before-composite" && layer != "on-top")
        {
            GV_LOG_ERROR(kLogCategory, "smoke: unknown --smoke-layer \"%s\" (before-composite, on-top, texture or texture:<name>)", layer.c_str());
            return;
        }
        for (auto& scene : performance.scenes)
        {
            auto& graph = scene.graph;
            for (auto& wire : graph.wires)
            {
                const auto* node = graph.Find(wire.toNode);
                if (node == nullptr || node->type != gv::model::NodeType::MilkdropPreset || wire.toInput != static_cast<int>(PresetLayer::Feedback))
                    continue;
                if (!texture)
                {
                    const auto input = layer == "on-top" ? PresetLayer::OnTop : PresetLayer::BeforeComposite;
                    if (graph.WireInto(wire.toNode, static_cast<int>(input)) != nullptr)
                        continue; // that input is taken (the overlay)
                    wire.toInput = static_cast<int>(input);
                    GV_LOG_INFO(kLogCategory, "smoke layer: scene %s: video into %s", scene.id.c_str(), layer.c_str());
                    continue;
                }
                std::string name = layer.size() > 8 ? layer.substr(8) : std::string();
                if (name.empty())
                {
                    const auto textures = gv::library::PresetTextures(gv::model::ParamsOf<gv::model::MilkdropPresetParams>(*node).path);
                    if (textures.empty())
                        continue;
                    name = textures.front();
                }
                wire.toInput = static_cast<int>(PresetLayer::Texture);
                wire.textureName = name;
                GV_LOG_INFO(kLogCategory, "smoke layer: scene %s: video into texture \"%s\"", scene.id.c_str(), name.c_str());
            }
        }
    }

    /// --smoke-layer texture: a third of the way into each cut, the live scene's texture input is detached
    /// (its video goes back to feedback); two thirds in, it's attached again.
    void SmokeToggleTexture(double elapsed)
    {
        using gv::model::PresetLayer;
        if (m_options.smokeCutSeconds <= 0)
            return;
        const double phase = std::fmod(elapsed, m_options.smokeCutSeconds) / m_options.smokeCutSeconds;
        const int step = static_cast<int>(elapsed / m_options.smokeCutSeconds) * 3 + (phase < 1.0 / 3 ? 0 : phase < 2.0 / 3 ? 1 : 2);
        if (step == m_smokeToggleStep || step % 3 == 0)
            return;
        m_smokeToggleStep = step;
        const int live = m_engine.LiveScene();
        if (live < 0)
            return;
        auto edited = m_document.Get();
        auto& scene = edited.scenes[static_cast<size_t>(live)];
        const bool detach = step % 3 == 1;
        for (auto& wire : scene.graph.wires)
        {
            const auto* to = scene.graph.Find(wire.toNode);
            if (to == nullptr || to->type != gv::model::NodeType::MilkdropPreset)
                continue; // switcher inputs use the same numbers
            if (detach && wire.toInput == static_cast<int>(PresetLayer::Texture))
            {
                m_smokeDetached[scene.id] = wire.textureName;
                wire.toInput = static_cast<int>(PresetLayer::Feedback);
                wire.textureName.clear();
                Edit(std::move(edited), "Smoke Layer", {});
                GV_LOG_INFO(kLogCategory, "smoke layer: scene %d: texture input detached", live);
                return;
            }
            if (!detach && wire.toInput == static_cast<int>(PresetLayer::Feedback) && m_smokeDetached.count(scene.id) > 0)
            {
                wire.toInput = static_cast<int>(PresetLayer::Texture);
                wire.textureName = m_smokeDetached[scene.id];
                m_smokeDetached.erase(scene.id);
                Edit(std::move(edited), "Smoke Layer", {});
                GV_LOG_INFO(kLogCategory, "smoke layer: scene %d: texture input attached again", live);
                return;
            }
        }
    }

    /// The node canvas's edits on the live scene, through the same gate the canvas uses (EditGraph):
    /// 15 adds a Contrast node mid-chain, 16 moves a video wire from On top (or Feedback) to Before composite,
    /// 17 wires a chain into one of the preset's texture inputs, 18 deletes an effect node and bridges its wires.
    void SmokeCanvasEdit(int step, int live)
    {
        using namespace gv::model;
        const auto started = Clock::now();
        m_selectedScene = live; // so the canvas draws the scene being edited
        if (step == 15 && VideoIdsOf(m_document.Get().scenes[static_cast<size_t>(live)].graph).empty() && !m_document.Get().videos.empty())
        {
            auto edited = m_document.Get();
            AssignVideo(edited, live, edited.videos.front().id);
            Edit(std::move(edited), "Assign Video", {});
        }
        const Graph& graph = m_document.Get().scenes[static_cast<size_t>(live)].graph;
        auto firstWire = [&](auto&& match) -> std::optional<Wire> {
            for (const auto& wire : graph.wires)
            {
                if (match(wire))
                    return wire;
            }
            return std::nullopt;
        };
        auto isEffect = [&](const std::string& id) {
            const Node* node = graph.Find(id);
            return node != nullptr && IsEffect(node->type);
        };
        auto isPreset = [&](const std::string& id) {
            const Node* node = graph.Find(id);
            return node != nullptr && node->type == NodeType::MilkdropPreset;
        };
        std::string what = "nothing to do";
        std::vector<GraphError> errors;
        if (step == 15)
        {
            if (const auto wire = firstWire([&](const Wire& w) { return isEffect(w.fromNode) && isEffect(w.toNode); }))
            {
                what = "added a Contrast node between " + wire->fromNode + " and " + wire->toNode;
                errors = EditGraph(live, [&](Graph& g) {
                    const Node* to = g.Find(wire->toNode);
                    const std::string id = AddNode(g, NodeType::Contrast, to->x, to->y + 120.0f);
                    ParamsOf<ContrastParams>(*g.Find(id)).contrast = 1.6f;
                    Connect(g, wire->fromNode, id, 0);
                    Connect(g, id, wire->toNode, wire->toInput, wire->textureName);
                }, "Add Contrast", {});
            }
        }
        else if (step == 16)
        {
            auto into = [&](PresetLayer layer) {
                return firstWire([&](const Wire& w) { return isPreset(w.toNode) && w.toInput == static_cast<int>(layer); });
            };
            auto wire = into(PresetLayer::OnTop);
            if (!wire)
                wire = into(PresetLayer::Feedback);
            if (wire)
            {
                what = "rewired " + wire->fromNode + " from input " + std::to_string(wire->toInput) + " to before composite";
                errors = EditGraph(live, [&](Graph& g) {
                    Disconnect(g, wire->toNode, wire->toInput);
                    Connect(g, wire->fromNode, wire->toNode, static_cast<int>(PresetLayer::BeforeComposite));
                }, "Rewire", {});
            }
        }
        else if (step == 17)
        {
            for (const auto& node : graph.nodes)
            {
                if (node.type != NodeType::MilkdropPreset)
                    continue;
                const auto textures = gv::library::PresetTextures(ParamsOf<MilkdropPresetParams>(node).path);
                const auto chain = firstWire([&](const Wire& w) { return w.toNode == node.id && isEffect(w.fromNode); });
                if (textures.empty() || !chain)
                    continue;
                what = "wired " + chain->fromNode + " into " + node.id + "'s texture " + textures.front();
                errors = EditGraph(live, [&](Graph& g) {
                    Connect(g, chain->fromNode, node.id, static_cast<int>(PresetLayer::Texture), textures.front());
                }, "Connect Texture", {});
                break;
            }
        }
        else if (step == 18)
        {
            // An effect with an effect upstream: remove it and wire its input to where it went.
            for (const auto& node : graph.nodes)
            {
                const Node* upstream = IsEffect(node.type) ? graph.Upstream(node.id, 0) : nullptr;
                if (upstream == nullptr || !IsEffect(upstream->type))
                    continue;
                std::vector<Wire> out;
                for (const auto& wire : graph.wires)
                {
                    if (wire.fromNode == node.id)
                        out.push_back(wire);
                }
                const std::string id = node.id;
                const std::string from = upstream->id;
                what = "deleted " + id + " and wired " + from + " past it";
                errors = EditGraph(live, [&](Graph& g) {
                    RemoveNode(g, id);
                    for (const auto& wire : out)
                        Connect(g, from, wire.toNode, wire.toInput, wire.textureName);
                }, "Delete Node", {});
                break;
            }
        }
        m_smokeCanvasEditAt = std::chrono::duration<double>(Clock::now() - m_smokeStart).count();
        m_smokeCanvasHeldBefore = m_engine.GetStats().heldTicks;
        GV_LOG_INFO(kLogCategory, "smoke edit (canvas): scene %d: %s%s (%.2f ms on the main thread)", live, what.c_str(), errors.empty() ? "" : " - REFUSED",
                    std::chrono::duration<double, std::milli>(Clock::now() - started).count());
        for (const auto& error : errors)
            GV_LOG_INFO(kLogCategory, "  refused: %s", error.message.c_str());
        if (!errors.empty())
            m_smokeCanvasRefusals++;
    }

    /// Scripted edits while playing, one per second from 3 s, to exercise live updates of the engine.
    void SmokeEdits(double elapsed)
    {
        const int step = static_cast<int>(elapsed) - 3;
        if (step < 0 || step == m_smokeEditStep)
            return;
        m_smokeEditStep = step;
        auto edited = m_document.Get();
        const int live = m_engine.LiveScene();
        const int count = static_cast<int>(edited.scenes.size());
        switch (step)
        {
            case 0:
                gv::model::MoveScene(edited, count - 1, 0);
                Edit(std::move(edited), "Move Scene", {});
                GV_LOG_INFO(kLogCategory, "smoke edit: moved the last scene to the top; live is now %d", m_engine.LiveScene());
                break;
            case 1:
                if (live >= 0)
                {
                    const auto other = edited.scenes[static_cast<size_t>((live + 2) % count)].FirstPreset();
                    gv::model::ReplacePreset(edited, live, 0, other);
                    Edit(std::move(edited), "Change Preset", {});
                    GV_LOG_INFO(kLogCategory, "smoke edit: changed the live scene's preset (scene %d)", live);
                }
                break;
            case 2:
                if (live >= 0)
                {
                    auto& graph = edited.scenes[static_cast<size_t>(live)].graph;
                    if (gv::model::VideoIdsOf(graph).empty() && !edited.videos.empty())
                        gv::model::AssignVideo(edited, live, edited.videos.front().id);
                    for (auto& node : graph.nodes)
                    {
                        if (node.type == gv::model::NodeType::Opacity)
                            gv::model::ParamsOf<gv::model::OpacityParams>(node).opacity = 0.3f;
                    }
                    Edit(std::move(edited), "Change Opacity", {});
                    GV_LOG_INFO(kLogCategory, "smoke edit: changed the live scene's opacity");
                }
                break;
            case 3:
                if (live >= 0)
                {
                    gv::model::DeleteScene(edited, live);
                    Edit(std::move(edited), "Delete Scene", {});
                    GV_LOG_INFO(kLogCategory, "smoke edit: deleted the live scene; live is now %d (its instance keeps rendering)", m_engine.LiveScene());
                }
                break;
            case 4:
                while (m_document.CanUndo())
                    Undo();
                GV_LOG_INFO(kLogCategory, "smoke edit: undid everything; %zu scenes, dirty=%d", m_document.Get().scenes.size(), m_document.IsDirty());
                break;
            case 5:
            case 7:
            {
                // Preset switcher jumps: into a scene switching presets at its third entry, then within it to
                // the second (with that entry's look changed while live).
                const auto playlist = std::find_if(edited.scenes.begin(), edited.scenes.end(), [](const auto& s) { return s.SwitchesPresets() && s.presetEntries.size() >= 3; });
                if (playlist == edited.scenes.end())
                    break;
                const int scene = static_cast<int>(playlist - edited.scenes.begin());
                if (step == 5)
                {
                    TriggerItem(scene, 2);
                    m_selectedScene = scene;
                    GV_LOG_INFO(kLogCategory, "smoke edit: jumped into scene %d at preset 3", scene);
                }
                else
                {
                    GV_LOG_INFO(kLogCategory, "smoke edit: scene %d live=%d on preset %d", scene, m_engine.LiveScene(), m_engine.Scene(scene).playlistItem + 1);
                    SetEntrySaturation(playlist->graph, 1, 0.f);
                    Edit(std::move(edited), "Change Saturation", {});
                    TriggerItem(scene, 1);
                    GV_LOG_INFO(kLogCategory, "smoke edit: jumped within scene %d to preset 2 (black and white)", scene);
                }
                break;
            }
            case 11:
                // Dropping a preset on the preview: replaces the preset entry on screen in the live scene.
                if (live >= 0 && edited.scenes[static_cast<size_t>(live)].SwitchesPresets())
                {
                    const auto preset = edited.scenes[0].FirstPreset();
                    const int item = gv::model::ReplacePreset(edited, live, m_engine.LiveItem(), preset);
                    Edit(std::move(edited), "Replace Switcher Preset", {});
                    if (item > 0)
                        TriggerItem(live, item);
                    GV_LOG_INFO(kLogCategory, "smoke edit: replaced preset %d of live scene %d with %s", item + 1, live, preset.filename().c_str());
                }
                break;
            case 15:
            case 16:
            case 17:
            case 18:
                if (live >= 0)
                    SmokeCanvasEdit(step, live);
                break;
            case 19:
                while (m_document.CanUndo())
                    Undo();
                GV_LOG_INFO(kLogCategory, "smoke edit: undid the canvas edits too; %zu scenes, dirty=%d", m_document.Get().scenes.size(), m_document.IsDirty());
                break;
            case 9:
            case 13:
                for (int scene = 0; scene < static_cast<int>(edited.scenes.size()); ++scene)
                {
                    if (edited.scenes[static_cast<size_t>(scene)].SwitchesPresets())
                        GV_LOG_INFO(kLogCategory, "smoke edit: scene %d live=%d on preset %d", scene, m_engine.LiveScene(), m_engine.Scene(scene).playlistItem + 1);
                }
                break;
            default:
                break;
        }
    }

    /// Writes the presented texture as a BMP.
    void SaveSnapshot(const std::filesystem::path& file)
    {
        const int width = m_engine.RenderWidth();
        const int height = m_engine.RenderHeight();
        if (m_readFbo == 0)
            glGenFramebuffers(1, &m_readFbo);
        std::vector<unsigned char> rgba(static_cast<size_t>(width) * height * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_readFbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_engine.PresentedTexture(), 0);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        WriteBmp(file, rgba, width, height);
    }

    /// Writes bottom-up RGBA pixels (as GL reads them) as a 24-bit BMP.
    static void WriteBmp(const std::filesystem::path& file, const std::vector<unsigned char>& rgba, int width, int height)
    {
        const int rowBytes = (width * 3 + 3) & ~3;
        const uint32_t imageBytes = static_cast<uint32_t>(rowBytes * height);
        std::vector<unsigned char> bmp(54 + imageBytes, 0);
        auto put32 = [&](size_t at, uint32_t value) { for (int i = 0; i < 4; ++i) bmp[at + i] = static_cast<unsigned char>(value >> (8 * i)); };
        bmp[0] = 'B';
        bmp[1] = 'M';
        put32(2, 54 + imageBytes);
        put32(10, 54);
        put32(14, 40);
        put32(18, static_cast<uint32_t>(width));
        put32(22, static_cast<uint32_t>(height));
        bmp[26] = 1;
        bmp[28] = 24;
        put32(34, imageBytes);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const unsigned char* p = &rgba[static_cast<size_t>((y * width + x) * 4)];
                unsigned char* q = &bmp[54 + static_cast<size_t>(y * rowBytes + x * 3)];
                q[0] = p[2];
                q[1] = p[1];
                q[2] = p[0];
            }
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(bmp.data()), static_cast<std::streamsize>(bmp.size()));
    }

    void ApplyTexturePaths()
    {
        std::vector<std::string> paths;
        for (const auto& path : m_settings.texturePaths)
            paths.push_back(path.string());
        m_engine.SetTexturePaths(paths);
    }

    void SaveSettings()
    {
        if (m_options.smoke)
            return;
        if (auto error = m_settings.Save(m_settingsFile); !error.empty())
            GV_LOG_ERROR(kLogCategory, "%s", error.c_str());
        m_settingsDirty = false;
    }

    void AddMessage(std::string message)
    {
        GV_LOG_INFO(kLogCategory, "%s", message.c_str());
        m_messages.push_back(std::move(message));
        if (m_messages.size() > kMaxMessages)
            m_messages.pop_front();
    }

    SDL_Window* m_editorWindow;
    SDL_GLContext m_editorContext;
    Options m_options;

    std::filesystem::path m_settingsFile;
    gv::app::Settings m_settings;
    bool m_settingsDirty{false};

    gv::app::VideoManager m_video; // declared before the engine: destroyed after it, so lanes never see freed textures
    gv::engine::Engine m_engine;
    bool m_fullscreen{false};          //!< Only the visuals (Cmd+F).
    bool m_nativeForFullscreen{false}; //!< The visuals view entered macOS full screen, so leaving it leaves that too.
    float m_pinch{1.f};                //!< Trackpad pinch collected for this frame (1: none).
    bool m_nativeSeen{false};          //!< The window has been in macOS full screen since the visuals view started.
    bool m_menuBarHidden{false};
    GLuint m_readFbo{0};
    gv::SpscRingBuffer<float> m_ring;
    std::vector<float> m_pcm = std::vector<float>(48000 * 2);
    gv::io::AudioInput m_audio;
    std::unique_ptr<gv::io::MidiInput> m_midi;

    gv::library::Library m_library;
    gv::library::Favourites m_favourites;
    gv::library::Indexer m_indexer;
    gv::thumbnails::ThumbnailService m_thumbnails;
    std::set<std::string> m_availableTextures;
    uint64_t m_libraryVersion{0};
    bool m_libraryDirty{false};

    gv::model::Document m_document;
    uint64_t m_syncedRevision{0};
    int m_selectedScene{-1};
    std::vector<std::filesystem::path> m_droppedFiles;
    std::string m_windowTitle;
    std::vector<gv::model::Issue> m_issues;
    std::deque<std::string> m_messages;

    std::deque<float> m_frameIntervals;
    std::deque<double> m_gpuWindow;                       //!< The live lane's GPU ms, last second.
    std::map<int, std::pair<double, long long>> m_gpuByScene; //!< Live GPU ms sum and samples per scene (smoke summary).
    int m_gpuScene{-1};
    int m_gpuSettle{0};
    long long m_slowFrames{0};
    Clock::time_point m_lastFrame{};

    Clock::time_point m_smokeStart{};
    Clock::time_point m_smokeDeadline{};
    int m_smokeLastCut{-1};
    int m_smokeToggleStep{-1};
    std::map<std::string, std::string> m_smokeDetached; //!< Scene id -> the texture its video was detached from.
    int m_smokeCuts{0};
    bool m_smokeDone{false};
    bool m_editorSnapshotTaken{false};
    int m_smokeEditStep{-1};
    int m_smokeCanvasRefusals{0};
    double m_smokeCanvasEditAt{-1.0}; //!< When the last canvas edit ran (smoke seconds), until its held ticks are counted.
    long long m_smokeCanvasHeldBefore{0};
    long long m_smokeCanvasHeld{0};
    double m_smokeNextReport{0.0};
    std::mt19937 m_smokeRandom{12345};
    bool m_smokeOk{false};
};

} // namespace

int main(int argc, char* argv[])
{
    const auto options = ParseOptions(argc, argv);

    // Before anything creates Zebra's folders, so the old ones are copied across on the first launch.
    const std::string migration = gv::paths::MigrateLegacyFolders();
    gv::log::Init(gv::paths::LogsDir());
    GV_LOG_INFO(kLogCategory, "%s %s starting", gv::app_info::kName, gv::app_info::kVersion);
    if (!migration.empty())
        GV_LOG_INFO(kLogCategory, "%s", migration.c_str());

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0)
    {
        GV_LOG_ERROR(kLogCategory, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    const Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (options.smoke ? SDL_WINDOW_HIDDEN : 0);
    SDL_Window* window = SDL_CreateWindow(gv::app_info::kName, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1600, 1000, windowFlags);
    SDL_GLContext context = window != nullptr ? SDL_GL_CreateContext(window) : nullptr;
    if (context == nullptr)
    {
        GV_LOG_ERROR(kLogCategory, "could not create the editor window: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, context);
    gv::app::StartPinchCapture(window);
    SDL_GL_SetSwapInterval(1);
    GV_LOG_INFO(kLogCategory, "OpenGL %s, renderer %s", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    // v2: the M4 editor layout (the M2 shell's saved layout doesn't have its panels).
    static const std::string iniPath = (gv::paths::ApplicationSupportDir() / "layout-v2.ini").string();
    io.IniFilename = options.smoke ? nullptr : iniPath.c_str();
    gv::app::Shell::ApplyTheme();
    gv::app::InstallMenus();
    ImGui_ImplSDL2_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init("#version 410 core");

    int status = 0;
    {
        App app(window, context, options);
        if (!app.Initialize())
        {
            status = 1;
        }
        else
        {
            gv::app::Shell shell;
            bool running = true;
            for (int frame = 0; running; ++frame)
            {
                SDL_Event event;
                while (SDL_PollEvent(&event))
                {
                    ImGui_ImplSDL2_ProcessEvent(&event);
                    app.HandleEvent(event, running);
                }
                if (!app.Frame(shell, frame))
                {
                    running = false;
                }
            }
            if (options.smoke)
            {
                status = app.SmokeSucceeded() ? 0 : 2;
            }
        }
        app.Shutdown();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    GV_LOG_INFO(kLogCategory, "exited (status %d)", status);
    return status;
}
