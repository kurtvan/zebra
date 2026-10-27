#include "app/macos/Window.hpp"

#include <SDL.h>
#include <SDL_syswm.h>

#import <AppKit/AppKit.h>

namespace gv::app {

void DisableNativeFullscreen(SDL_Window* window)
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA)
    {
        return;
    }
    NSWindow* nsWindow = info.info.cocoa.window;
    NSWindowCollectionBehavior behavior = nsWindow.collectionBehavior;
    behavior &= ~(NSWindowCollectionBehaviorFullScreenPrimary | NSWindowCollectionBehaviorFullScreenAuxiliary);
    nsWindow.collectionBehavior = behavior | NSWindowCollectionBehaviorFullScreenNone;
}

} // namespace gv::app
