#pragma once

struct SDL_Window;

namespace gv::app {

/// Starts collecting trackpad pinches (AppKit's magnify gesture) over `window`. Call once, after creating it.
void StartPinchCapture(SDL_Window* window);

/// The pinch collected since the last call, as a zoom factor: above 1 spreads the fingers, 1 is no pinch.
auto TakePinch() -> float;

/// Enters or leaves macOS full screen (its own Space, like the green button). Used instead of SDL's fullscreen:
/// SDL's crashes AppKit if the window is already in macOS full screen.
void ToggleNativeFullscreen(SDL_Window* window);

/// True while the window is in (or entering) macOS full screen, however it got there.
auto IsNativeFullscreen(SDL_Window* window) -> bool;

/// In macOS full screen: true keeps the menu bar and Dock hidden even when the mouse reaches the screen
/// edge; false goes back to macOS's usual full screen behaviour (they slide in at the edge).
void SetMenuBarHidden(bool hidden);

} // namespace gv::app
