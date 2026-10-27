#include "app/macos/Window.hpp"

#include <SDL.h>
#include <SDL_syswm.h>

#import <AppKit/AppKit.h>

namespace gv::app {

namespace {

auto CocoaWindow(SDL_Window* window) -> NSWindow*
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA)
    {
        return nil;
    }
    return info.info.cocoa.window;
}

} // namespace

void ToggleNativeFullscreen(SDL_Window* window)
{
    [CocoaWindow(window) toggleFullScreen:nil];
}

auto IsNativeFullscreen(SDL_Window* window) -> bool
{
    NSWindow* nsWindow = CocoaWindow(window);
    return nsWindow != nil && (nsWindow.styleMask & NSWindowStyleMaskFullScreen) != 0;
}

} // namespace gv::app
