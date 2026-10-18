// fm4 - protected file signatures.

#pragma once

#include <filesystem>

namespace rex {
class Runtime;
}

namespace fm4 {

// Recomputes the XEX's protected-file signature table from the files under
// <game_root>, so modified data files (mods) are accepted. Call after the
// XEX is loaded and before the module launches.
void ResignProtectedFiles(rex::Runtime* runtime, const std::filesystem::path& game_root);

}  // namespace fm4

