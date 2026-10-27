#pragma once

struct SDL_Window;

namespace gv::app {

/// Enters or leaves macOS full screen (its own Space, like the green button). Used instead of SDL's fullscreen:
/// SDL's crashes AppKit if the window is already in macOS full screen.
void ToggleNativeFullscreen(SDL_Window* window);

/// True while the window is in (or entering) macOS full screen, however it got there.
auto IsNativeFullscreen(SDL_Window* window) -> bool;

} // namespace gv::app
