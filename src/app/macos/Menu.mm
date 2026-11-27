#include "app/macos/Menu.hpp"

#include "app_info.hpp"

#import <AppKit/AppKit.h>

#include <map>

using gv::app::MenuCommand;

namespace {

std::vector<int> g_commands;
std::map<int, bool> g_enabled;
std::map<int, bool> g_checked;
NSMenuItem* g_undoItem = nil;
NSMenuItem* g_redoItem = nil;
NSMenu* g_recentMenu = nil;

} // namespace

@interface GVMenuTarget : NSObject <NSMenuItemValidation>
@end

@implementation GVMenuTarget
- (void)invoke:(NSMenuItem*)sender
{
    g_commands.push_back(static_cast<int>(sender.tag));
}
- (void)about:(NSMenuItem*)sender
{
    [NSApp orderFrontStandardAboutPanelWithOptions:@{
        NSAboutPanelOptionApplicationName : @(gv::app_info::kName),
        NSAboutPanelOptionApplicationVersion : @(gv::app_info::kVersion),
    }];
}
- (BOOL)validateMenuItem:(NSMenuItem*)item
{
    if (item.action == @selector(about:))
        return YES;
    if (auto checked = g_checked.find(static_cast<int>(item.tag)); checked != g_checked.end())
        item.state = checked->second ? NSControlStateValueOn : NSControlStateValueOff;
    auto it = g_enabled.find(static_cast<int>(item.tag));
    return it == g_enabled.end() ? YES : it->second;
}
@end

namespace gv::app {

namespace {

GVMenuTarget* g_target = nil;

auto AddItem(NSMenu* menu, NSString* title, MenuCommand command, NSString* key, NSEventModifierFlags modifiers = NSEventModifierFlagCommand) -> NSMenuItem*
{
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:@selector(invoke:) keyEquivalent:key];
    item.keyEquivalentModifierMask = modifiers;
    item.target = g_target;
    item.tag = static_cast<NSInteger>(command);
    [menu addItem:item];
    return item;
}

auto AddMenu(NSString* title, NSInteger index) -> NSMenu*
{
    NSMenu* menu = [[NSMenu alloc] initWithTitle:title];
    NSMenuItem* holder = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
    holder.submenu = menu;
    NSMenu* bar = [NSApp mainMenu];
    [bar insertItem:holder atIndex:std::min(index, bar.numberOfItems)];
    return menu;
}

/// SDL names its application menu items after the executable ("About zebra", "Quit zebra"): use the app's
/// name instead, and show it in the About panel too.
void NameApplicationMenu()
{
    NSMenu* bar = [NSApp mainMenu];
    NSMenu* appMenu = bar.numberOfItems > 0 ? [bar itemAtIndex:0].submenu : nil;
    if (appMenu == nil)
        return;
    NSString* process = [NSProcessInfo processInfo].processName;
    NSString* name = @(gv::app_info::kName);
    for (NSMenuItem* item in appMenu.itemArray)
    {
        if (process.length > 0)
            item.title = [item.title stringByReplacingOccurrencesOfString:process withString:name];
        if (item.action == @selector(orderFrontStandardAboutPanel:))
        {
            item.action = @selector(about:);
            item.target = g_target;
        }
    }
}

} // namespace

void InstallMenus()
{
    @autoreleasepool
    {
        if (g_target != nil || [NSApp mainMenu] == nil)
            return;
        g_target = [[GVMenuTarget alloc] init];
        NameApplicationMenu();

        // SDL's application menu is first; ours follow it, before SDL's Window menu.
        NSMenu* file = AddMenu(@"File", 1);
        AddItem(file, @"New Performance", MenuCommand::New, @"n");
        AddItem(file, @"Open Performance…", MenuCommand::Open, @"o");
        NSMenuItem* recent = [[NSMenuItem alloc] initWithTitle:@"Open Recent" action:nil keyEquivalent:@""];
        g_recentMenu = [[NSMenu alloc] initWithTitle:@"Open Recent"];
        recent.submenu = g_recentMenu;
        [file addItem:recent];
        [file addItem:[NSMenuItem separatorItem]];
        AddItem(file, @"Save", MenuCommand::Save, @"s");
        AddItem(file, @"Save As…", MenuCommand::SaveAs, @"s", NSEventModifierFlagCommand | NSEventModifierFlagShift);
        AddItem(file, @"Revert to Saved", MenuCommand::Reload, @"r");

        NSMenu* edit = AddMenu(@"Edit", 2);
        g_undoItem = AddItem(edit, @"Undo", MenuCommand::Undo, @"z");
        g_redoItem = AddItem(edit, @"Redo", MenuCommand::Redo, @"z", NSEventModifierFlagCommand | NSEventModifierFlagShift);

        NSMenu* scene = AddMenu(@"Scene", 3);
        AddItem(scene, @"Add Scene", MenuCommand::AddScene, @"n", NSEventModifierFlagCommand | NSEventModifierFlagShift);
        AddItem(scene, @"Duplicate Scene", MenuCommand::DuplicateScene, @"d");
        AddItem(scene, @"Delete Scene", MenuCommand::DeleteScene, @"");
        [scene addItem:[NSMenuItem separatorItem]];
        AddItem(scene, @"Go Live", MenuCommand::TriggerSelected, @"", 0); // Return is handled in the scene list, not here: a menu key equivalent would steal it from text fields

        NSMenu* view = AddMenu(@"View", 4);
        AddItem(view, @"Fullscreen Visuals", MenuCommand::Fullscreen, @"f");
        [view addItem:[NSMenuItem separatorItem]];
        AddItem(view, @"Scenes Panel", MenuCommand::ToggleLeftPanes, @"1");
        AddItem(view, @"Scene and Setup Panels", MenuCommand::ToggleRightPanes, @"2");
        AddItem(view, @"Presets, Videos, Nodes, Status and Messages", MenuCommand::ToggleBottomPanes, @"3");
        AddItem(view, @"Focus Mode", MenuCommand::FocusMode, @"0");

        SetRecentFiles({});
    }
}

auto TakeMenuCommands() -> std::vector<int>
{
    std::vector<int> commands;
    commands.swap(g_commands);
    return commands;
}

void SetMenuEnabled(MenuCommand command, bool enabled)
{
    g_enabled[static_cast<int>(command)] = enabled;
}

void SetMenuChecked(MenuCommand command, bool checked)
{
    g_checked[static_cast<int>(command)] = checked;
}

void SetUndoRedoLabels(const std::string& undo, const std::string& redo)
{
    g_undoItem.title = undo.empty() ? @"Undo" : [NSString stringWithFormat:@"Undo %s", undo.c_str()];
    g_redoItem.title = redo.empty() ? @"Redo" : [NSString stringWithFormat:@"Redo %s", redo.c_str()];
}

void SetRecentFiles(const std::vector<std::filesystem::path>& files)
{
    if (g_recentMenu == nil)
        return;
    [g_recentMenu removeAllItems];
    for (size_t i = 0; i < files.size(); ++i)
    {
        NSString* title = @(files[i].filename().c_str());
        NSMenuItem* item = AddItem(g_recentMenu, title, static_cast<MenuCommand>(static_cast<int>(MenuCommand::OpenRecent) + static_cast<int>(i)), @"", 0);
        item.toolTip = @(files[i].c_str());
    }
    if (!files.empty())
        [g_recentMenu addItem:[NSMenuItem separatorItem]];
    AddItem(g_recentMenu, @"Clear Menu", MenuCommand::ClearRecent, @"", 0);
}

} // namespace gv::app
