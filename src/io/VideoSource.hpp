#pragma once

#include <OpenGL/gl3.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace gv::io {

enum class VideoState
{
    Starting,     //!< Opening / waiting for the first frame.
    Live,         //!< Frames are arriving.
    Disconnected, //!< Syphon server not available (yet); no frame is shown.
    Missing,      //!< File not found.
    Failed,       //!< Couldn't be opened.
};

auto ToString(VideoState state) -> const char*;

/// How a file video's picture is fitted to the output canvas.
enum class VideoFit
{
    Fill,   //!< Covers the canvas, cropping the edges that don't fit.
    Fit,    //!< The whole picture, with black bars.
    Stretch //!< Stretched to the canvas shape.
};

/// A video feed turned into a GL_TEXTURE_2D in bottom-up (OpenGL) orientation, as patch 0002 expects.
///
/// Created and updated on the main thread with the editor's GL context current; the textures are shared
/// with the lanes through the share group.
class VideoSource
{
public:
    virtual ~VideoSource() = default;

    /// Pulls the newest frame, if any. Returns the texture to blend, or 0 when there is nothing to show
    /// (never a stale frame from a source that went away).
    virtual auto Update() -> GLuint = 0;

    /// Starts or pauses decoding (file sources). Paused sources keep their position.
    virtual void SetActive(bool active)
    {
    }

    /// Jumps back to the start point (file sources).
    virtual void Restart()
    {
    }

    /// Where playback starts, restarts and loops back to, and how fast it plays (file sources). Moving the
    /// start point jumps there, so an edit shows straight away.
    virtual void SetPlayback(double startSeconds, double speed)
    {
    }

    /// Whether it loops at the end (file sources, default on). Off: it stops on its last frame until
    /// Restart(). Turning it back on while it holds restarts it. Cheap when unchanged.
    virtual void SetLoop(bool loop)
    {
    }

    /// Fits the upright picture to a canvas of this size (file sources): the texture then has the canvas's size.
    virtual void SetLayout(VideoFit fit, int canvasWidth, int canvasHeight)
    {
    }

    /// Length and current position in seconds (file sources), or 0 if not known (yet).
    virtual auto DurationSeconds() const -> double
    {
        return 0.0;
    }
    virtual auto PositionSeconds() const -> double
    {
        return 0.0;
    }

    virtual auto State() const -> VideoState = 0;
    virtual auto Describe() const -> std::string = 0;
};

/// AVFoundation playback of a file, muted and looping from its start point, decoded straight into GL textures (no CPU copies).
/// The picture is turned upright (the rotation phones record), then fitted to the canvas set by SetLayout.
auto CreateFileVideoSource(const std::filesystem::path& file) -> std::unique_ptr<VideoSource>;

/// Syphon input from the server matching `appName` and `serverName` (an empty field matches anything).
/// Connects when the server appears and reconnects after it goes away.
auto CreateSyphonVideoSource(const std::string& appName, const std::string& serverName) -> std::unique_ptr<VideoSource>;

struct SyphonServerInfo
{
    std::string appName;
    std::string serverName;
};

/// Syphon servers currently announced on this machine.
auto ListSyphonServers() -> std::vector<SyphonServerInfo>;

} // namespace gv::io
