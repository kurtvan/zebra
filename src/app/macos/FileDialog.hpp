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

} // namespace gv::app
