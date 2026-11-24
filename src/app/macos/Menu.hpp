#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gv::app {

enum class MenuCommand
{
    New = 1,
    Open,
    Save,
    SaveAs,
    Reload,
    Undo,
    Redo,
    AddScene,
    DuplicateScene,
    DeleteScene,
    TriggerSelected,
    Fullscreen,
    ClearRecent,
    ToggleLeftPanes,   //!< Scenes.
    ToggleRightPanes,  //!< Scene, Setup.
    ToggleBottomPanes, //!< Presets, Videos, Nodes, Status, Messages.
    FocusMode,         //!< Hide all three; again: bring back what was open.
    OpenRecent = 1000, //!< OpenRecent + n opens the n-th recent file.
};

/// Adds File, Edit, Scene and View menus to the macOS menu bar (next to SDL's application and Window menus).
/// Call once after SDL video is initialized.
void InstallMenus();

/// Menu commands chosen since the last call (menu clicks and key equivalents like Cmd+S).
auto TakeMenuCommands() -> std::vector<int>;

/// Enables or disables a command's menu item.
void SetMenuEnabled(MenuCommand command, bool enabled);

/// Shows a check mark next to a command's menu item.
void SetMenuChecked(MenuCommand command, bool checked);

/// Shows "Undo <label>" / "Redo <label>" (empty label: plain "Undo"/"Redo").
void SetUndoRedoLabels(const std::string& undo, const std::string& redo);

/// Rebuilds File > Open Recent.
void SetRecentFiles(const std::vector<std::filesystem::path>& files);

} // namespace gv::app
