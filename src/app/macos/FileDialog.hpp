#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gv::app {

/// Native open panel for one file. `extensions` like {"json"}; empty allows any file.
auto ChooseFile(const std::string& title, const std::vector<std::string>& extensions) -> std::optional<std::filesystem::path>;

/// Native open panel for one folder.
auto ChooseFolder(const std::string& title) -> std::optional<std::filesystem::path>;

/// Native save panel. `defaultName` like "My Show.json"; the extension is enforced.
auto ChooseSaveFile(const std::string& title, const std::string& defaultName, const std::string& extension) -> std::optional<std::filesystem::path>;

enum class SaveChoice
{
    Save,
    DontSave,
    Cancel
};

/// "Do you want to save the changes you made to <documentName>?" with Save / Don't Save / Cancel.
auto AskToSaveChanges(const std::string& documentName) -> SaveChoice;

/// Shows the file in Finder.
void RevealInFinder(const std::filesystem::path& file);

} // namespace gv::app
