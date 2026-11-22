#pragma once

#include "model/LegacyScene.hpp"
#include "model/Performance.hpp"

#include <optional>

namespace gv::test {

// Reads a graph back as a format-2 ("classic") scene: the inverse of model::GraphFromClassic for everything
// it keeps. Only the tests need it, to check that older shows convert exactly.

/// `graph` as a classic scene, or nullopt if it isn't shaped like one.
auto ClassicFromGraph(const model::Graph& graph) -> std::optional<model::ClassicScene>;

/// `scene`'s graph as a classic scene (empty if it isn't shaped like one).
auto Classic(const model::Scene& scene) -> model::ClassicScene;

/// The video look of entry `item` of a classic playlist scene, or the scene's.
auto LookFor(const model::Scene& scene, int item) -> model::VideoLook;

} // namespace gv::test
