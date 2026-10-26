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

auto ChooseSaveFile(const std::string& title, const std::string& defaultName, const std::string& extension) -> std::optional<std::filesystem::path>
{
    @autoreleasepool
    {
        NSSavePanel* panel = [NSSavePanel savePanel];
        panel.title = @(title.c_str());
        panel.nameFieldStringValue = @(defaultName.c_str());
        if (UTType* type = [UTType typeWithFilenameExtension:@(extension.c_str())])
            panel.allowedContentTypes = @[type];
        NSWindow* keyWindow = [NSApp keyWindow];
        const NSModalResponse response = [panel runModal];
        [keyWindow makeKeyAndOrderFront:nil];
        if (response != NSModalResponseOK || panel.URL == nil)
            return std::nullopt;
        return std::filesystem::path(panel.URL.path.UTF8String);
    }
}

auto AskToSaveChanges(const std::string& documentName) -> SaveChoice
{
    @autoreleasepool
    {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = [NSString stringWithFormat:@"Do you want to save the changes you made to \u201c%s\u201d?", documentName.c_str()];
        alert.informativeText = @"Your changes will be lost if you don't save them.";
        [alert addButtonWithTitle:@"Save"];
        [alert addButtonWithTitle:@"Cancel"];
        [alert addButtonWithTitle:@"Don't Save"];
        NSWindow* keyWindow = [NSApp keyWindow];
        const NSModalResponse response = [alert runModal];
        [keyWindow makeKeyAndOrderFront:nil];
        if (response == NSAlertFirstButtonReturn)
            return SaveChoice::Save;
        if (response == NSAlertThirdButtonReturn)
            return SaveChoice::DontSave;
        return SaveChoice::Cancel;
    }
}

void RevealInFinder(const std::filesystem::path& file)
{
    @autoreleasepool
    {
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[[NSURL fileURLWithPath:@(file.c_str())]]];
    }
}

} // namespace gv::app
