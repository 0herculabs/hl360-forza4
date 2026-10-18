// fm4 - crash diagnostics.

#pragma once

#include <filesystem>

namespace fm4 {

// Writes crash_<timestamp>.txt (symbolized stack) and .dmp into log_dir when
// the process hits an unhandled exception. Windows only; no-op elsewhere.
void InstallCrashHandler(const std::filesystem::path& log_dir);

}  // namespace fm4
