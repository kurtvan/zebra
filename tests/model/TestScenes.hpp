#pragma once

#include "model/LegacyScene.hpp"
#include "model/Performance.hpp"

#include <string>

namespace gv::test {

/// A scene playing `classic` (by default just the preset "/presets/<id>.milk"), as a graph.
inline auto MakeScene(const std::string& id, int cc, model::ClassicScene classic = {}) -> model::Scene
{
    if (classic.presetPath.empty() && !classic.playlist.enabled)
        classic.presetPath = "/presets/" + id + ".milk";
    model::Scene scene;
    scene.id = id;
    scene.cc20Value = cc;
    scene.graph = model::GraphFromClassic(classic);
    scene.Refresh();
    return scene;
}

/// A classic scene with a file video and the given look.
inline auto WithVideo(const std::string& preset, const std::string& videoId, model::VideoLook look = {}) -> model::ClassicScene
{
    model::ClassicScene classic;
    classic.presetPath = preset;
    classic.video.enabled = true;
    classic.video.source.videoId = videoId;
    classic.video.look = look;
    return classic;
}

} // namespace gv::test
