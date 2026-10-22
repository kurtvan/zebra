#include "app/macos/FileDialog.hpp"

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace gv::app {

namespace {

auto RunPanel(NSOpenPanel* panel) -> std::optional<std::filesystem::path>
{
    NSWindow* keyWindow = [NSApp keyWindow];
    const NSModalResponse response = [panel runModal];
    [keyWindow makeKeyAndOrderFront:nil];
    if (response != NSModalResponseOK || panel.URL == nil)
    {
        return std::nullopt;
    }
    return std::filesystem::path(panel.URL.path.UTF8String);
}

} // namespace

auto ChooseFile(const std::string& title, const std::vector<std::string>& extensions) -> std::optional<std::filesystem::path>
{
    @autoreleasepool
    {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title = @(title.c_str());
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        if (!extensions.empty())
        {
            NSMutableArray<UTType*>* types = [NSMutableArray array];
            for (const auto& extension : extensions)
            {
                if (UTType* type = [UTType typeWithFilenameExtension:@(extension.c_str())])
                    [types addObject:type];
            }
            panel.allowedContentTypes = types;
        }
        return RunPanel(panel);
    }
}

auto ChooseFolder(const std::string& title) -> std::optional<std::filesystem::path>
{
    @autoreleasepool
    {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title = @(title.c_str());
        panel.canChooseFiles = NO;
        panel.canChooseDirectories = YES;
        panel.allowsMultipleSelection = NO;
        return RunPanel(panel);
    }
}

} // namespace gv::app
