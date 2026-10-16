// Where saves and the cache live: next to the game with portable.txt there,
// otherwise Documents, or %LOCALAPPDATA% when Windows' Controlled Folder
// Access keeps the game out of Documents.

#pragma once

#include <rex/rex_app.h>

namespace user_paths {

// From ReXApp::OnConfigurePaths. Explicit user_data_root / cache_root
// options are kept.
void Configure(rex::PathConfig& paths);

// Once logging and the window are up: logs the choice and, the first time
// Documents is found blocked, explains it in a message box.
void Report(const std::filesystem::path& user_data_root);

}  // namespace user_paths
