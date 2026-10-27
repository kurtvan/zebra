#pragma once

struct SDL_Window;

namespace gv::app {

/// Turns off macOS's own full screen (green button, Globe+F, Window > Enter Full Screen) for the window.
/// Our fullscreen toggle is SDL's, which crashes AppKit if the window is already in a full screen Space.
void DisableNativeFullscreen(SDL_Window* window);

} // namespace gv::app
