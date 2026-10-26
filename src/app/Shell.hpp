#pragma once

#include "app/Settings.hpp"
#include "app/VideoManager.hpp"
#include "engine/Engine.hpp"
#include "io/AudioInput.hpp"
#include "io/MidiInput.hpp"
#include "model/PerformanceJson.hpp"

#include <OpenGL/gl3.h>

#include <deque>
#include <filesystem>
#include <string>
#include <vector>

namespace gv::app {

/// What the shell can ask the app to do.
class AppActions
{
public:
    virtual ~AppActions() = default;
    virtual void OpenPerformance(const std::filesystem::path& file) = 0;
    virtual void ReloadPerformance() = 0;
    virtual void ToggleFullscreen() = 0; //!< The window shows only the visuals, on whichever screen it's on.
    virtual void SetAudioDevice(const std::string& deviceName) = 0;    //!< Empty: system default.
    virtual void SetMidiChannel(int channel) = 0;
    virtual void SetLibraryRoot(const std::filesystem::path& folder) = 0;
    virtual void SetTextureFolder(const std::filesystem::path& folder) = 0;
    virtual void SettingsChanged() = 0;
    virtual void Trigger(int scene) = 0;
};

/// Everything the shell shows. Owned by the app; the shell only reads it (except settings fields it edits).
struct ShellState
{
    const model::Performance* performance{nullptr};
    std::filesystem::path performancePath;
    const std::vector<model::Issue>* issues{nullptr};
    std::deque<std::string>* messages{nullptr};
    Settings* settings{nullptr};
    const engine::Engine* engine{nullptr};
    const io::MidiInput* midi{nullptr};
    const io::AudioInput* audio{nullptr};
    const VideoManager* video{nullptr};
    GLuint preview{0};
    const std::deque<float>* frameIntervalsMs{nullptr};
    long long slowFrames{0};
};

/// Temporary M2 editor shell (replaced by the real editor in M4): preview, scene list, setup and status.
class Shell
{
public:
    void Draw(ShellState& state, AppActions& actions);

private:
    void BuildDefaultLayout(unsigned int dockspace);
    void DrawPreview(const ShellState& state, AppActions& actions);
    void DrawScenes(const ShellState& state, AppActions& actions);
    void DrawSetup(ShellState& state, AppActions& actions);
    void DrawStatus(const ShellState& state);
    void DrawMessages(const ShellState& state);

    bool m_layoutBuilt{false};
    std::vector<std::string> m_audioDevices;
    bool m_listsLoaded{false};
};

} // namespace gv::app
