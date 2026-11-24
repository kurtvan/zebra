#pragma once

#include "model/Graph.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gv::model {

constexpr int kCurrentFormatVersion = 3; //!< 3: scenes are node graphs. 2: video files live in a shared pool (Performance::videos).
constexpr int kMaxScenes = 128; //!< One MIDI CC (CC 20) on one channel.
constexpr int kMinCanvasSize = 320;  //!< Canvas width and height, in pixels (even numbers only).
constexpr int kMaxCanvasSize = 7680;

/// The show's output size: the screen's, or a fixed size every scene renders at.
struct Canvas
{
    bool matchScreen{true}; //!< The screen the window is on, capped at a 1080p-class width (the app's kMaxRenderWidth).
    int width{1920};        //!< Fixed sizes: kMinCanvasSize..kMaxCanvasSize, even. Kept while matchScreen is on.
    int height{1080};

    auto operator==(const Canvas&) const -> bool = default;
};

/// Which MIDI clock beats get a beat hit.
enum class BeatHitEvery
{
    Beat,     //!< Every beat.
    TwoBeats, //!< Every second beat (beats 1 and 3 of a bar on the song grid).
    Bar,      //!< The first beat of each bar (4/4).
};

/// The synthetic transient a beat hit adds to the audio.
enum class BeatHitSound
{
    Kick,  //!< A low thump: drives bass.
    Snare, //!< A noisy snap: drives mid and treble.
    Burst, //!< A short broadband click: drives every band.
};

/// Adds a synthetic transient to the live scene's audio on the MIDI clock's beats, to strengthen the
/// visuals' reaction. Only while the clock is running; the visuals hear it, nothing is played.
struct BeatHit
{
    bool enabled{false};
    BeatHitEvery every{BeatHitEvery::Beat};
    BeatHitSound sound{BeatHitSound::Kick};
    float strength{0.5f}; //!< 0..1, peak level relative to full scale.

    auto operator==(const BeatHit&) const -> bool = default;
};

/// How a pool video's picture is fitted to the output canvas.
enum class VideoFit
{
    Fill,   //!< Covers the canvas, cropping the edges that don't fit.
    Fit,    //!< The whole picture, with black bars.
    Stretch //!< Stretched to the canvas shape.
};

constexpr double kMinVideoSpeed = 0.1;
constexpr double kMaxVideoSpeed = 4.0;

/// A video file in the performance's shared pool. Scenes refer to it by id; scenes using the same video
/// share one player, so it carries on from where it was unless a scene restarts it.
struct PoolVideo
{
    std::string id;   //!< Stable and unique within the performance.
    std::string name; //!< Display label; empty: the file name.
    std::filesystem::path path;
    double startSeconds{0.0}; //!< Where playback starts, restarts and loops back to.
    double speed{1.0};        //!< kMinVideoSpeed..kMaxVideoSpeed, forward only.
    VideoFit fit{VideoFit::Fill};

    bool missing{false}; //!< Set by the file checks, not serialized.

    auto DisplayName() const -> std::string
    {
        return name.empty() ? path.stem().string() : name;
    }
};

/// One preset a scene can show: what reaches the viewport, through the switcher feeding it if there is one.
struct PresetEntry
{
    std::string node;           //!< The Milkdrop preset node; empty when nothing reaches the viewport.
    std::filesystem::path path; //!< Empty: no file chosen (or nothing connected).
    int input{-1};              //!< Its input on the scene's preset switcher; -1 without one.

    auto operator==(const PresetEntry&) const -> bool = default;
};

struct Scene
{
    std::string id;   //!< Stable and unique within the performance.
    std::string name; //!< Display label; may be empty (the UI falls back to id).
    Graph graph;      //!< What the scene shows.
    BeatHit beatHit;
    int cc20Value{0}; //!< 0..127, unique within the performance. Doesn't change when scenes are reordered.

    /// Derived from `graph` by Refresh(), not serialized: the presets the viewport can show, in the order
    /// of the switcher feeding it (its wired inputs), or the one preset wired straight in. Never empty.
    /// The engine prepares a scene one entry at a time; entry 0 is where it starts.
    std::vector<PresetEntry> presetEntries{PresetEntry{}};
    std::string presetSwitcher; //!< The switcher feeding the viewport, when it has a preset wired in.

    /// Set by the loader / file checks, not serialized.
    bool presetMissing{false};         //!< The first entry's file is missing.
    bool videoMissing{false};          //!< A pool video the scene uses is missing.
    std::vector<bool> presetsMissing;  //!< Per entry of presetEntries.

    /// Recomputes presetEntries and presetSwitcher from `graph`. Called by the loader, CheckFiles and the
    /// edit helpers, so every scene the document holds is up to date.
    void Refresh();

    /// True if a switcher feeds the viewport: the scene switches between presets.
    auto SwitchesPresets() const -> bool
    {
        return !presetSwitcher.empty();
    }

    /// The presets' files, by entry.
    auto Presets() const -> std::vector<std::filesystem::path>
    {
        std::vector<std::filesystem::path> paths;
        for (const auto& entry : presetEntries)
            paths.push_back(entry.path);
        return paths;
    }

    /// The preset shown when the scene starts.
    auto FirstPreset() const -> const std::filesystem::path&
    {
        return presetEntries.front().path;
    }

    /// The entry on input `input` of the preset switcher, or -1.
    auto EntryOfInput(int input) const -> int
    {
        for (size_t i = 0; i < presetEntries.size(); ++i)
        {
            if (presetEntries[i].input == input && !presetSwitcher.empty())
                return static_cast<int>(i);
        }
        return -1;
    }

    /// Whether any video source node of the scene uses pool video `id`.
    auto UsesVideo(const std::string& id) const -> bool
    {
        const auto ids = VideoIdsOf(graph);
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }

    auto DisplayName() const -> const std::string&
    {
        return name.empty() ? id : name;
    }
};

struct Performance
{
    int version{kCurrentFormatVersion};
    std::string name;
    Canvas canvas;
    std::vector<Scene> scenes;
    std::vector<PoolVideo> videos;

    auto FindVideo(const std::string& id) const -> const PoolVideo*;

    /// Index of the scene triggered by CC 20 value `value`, if any.
    auto SceneForCc20(int value) const -> std::optional<size_t>;

    /// Lowest CC 20 value not used by any scene, if any is free.
    auto FreeCc20Value() const -> std::optional<int>;
};

} // namespace gv::model
