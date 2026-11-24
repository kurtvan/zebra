#include "app/macos/Window.hpp"

#include <SDL.h>
#include <SDL_syswm.h>

#import <AppKit/AppKit.h>

#include <algorithm>

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

double g_pinch = 1.0; //!< Pinch factor collected since the last TakePinch (main thread only).
id g_pinchMonitor = nil;

} // namespace

void StartPinchCapture(SDL_Window* window)
{
    NSWindow* nsWindow = CocoaWindow(window);
    if (nsWindow == nil || g_pinchMonitor != nil)
        return;
    __weak NSWindow* weakWindow = nsWindow;
    // SDL2 doesn't pass AppKit's magnify gesture on, so watch for it before it reaches SDL's view.
    g_pinchMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskMagnify
                                                           handler:^NSEvent*(NSEvent* event) {
                                                               if (event.window == weakWindow)
                                                                   g_pinch *= std::max(1.0 + event.magnification, 0.1);
                                                               return event;
                                                           }];
}

auto TakePinch() -> float
{
    const double pinch = g_pinch;
    g_pinch = 1.0;
    return static_cast<float>(pinch);
}

void ToggleNativeFullscreen(SDL_Window* window)
{
    [CocoaWindow(window) toggleFullScreen:nil];
}

auto IsNativeFullscreen(SDL_Window* window) -> bool
{
    NSWindow* nsWindow = CocoaWindow(window);
    return nsWindow != nil && (nsWindow.styleMask & NSWindowStyleMaskFullScreen) != 0;
}

void SetMenuBarHidden(bool hidden)
{
    NSApplicationPresentationOptions options = NSApp.presentationOptions;
    const NSApplicationPresentationOptions autoHide = NSApplicationPresentationAutoHideMenuBar | NSApplicationPresentationAutoHideDock;
    const NSApplicationPresentationOptions hide = NSApplicationPresentationHideMenuBar | NSApplicationPresentationHideDock;
    if (hidden)
        options = (options & ~autoHide) | hide;
    else
        options = (options & ~hide) | ((options & NSApplicationPresentationFullScreen) != 0 ? autoHide : 0);
    // AppKit throws on combinations it doesn't allow; keep the menu bar as it is rather than crash.
    @try
    {
        NSApp.presentationOptions = options;
    }
    @catch (NSException* exception)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "could not change the menu bar: %s", exception.reason.UTF8String);
    }
}

} // namespace gv::app
