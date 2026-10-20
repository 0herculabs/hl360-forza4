#pragma once

#include <string>
#include <optional>
#include <algorithm>

namespace launcher {

// Archive and state-file paths must stay below their selected destination.
// Apply Windows rules even when an archive is inspected on another platform.
inline bool IsSafeRelativePath(const std::string& path) {
  if (path.empty() || path.front() == '/' || path.find_first_of("\\:\r\n") != std::string::npos ||
      path.find('\0') != std::string::npos) return false;
  size_t start = 0;
  while (start < path.size()) {
    const size_t end = path.find('/', start);
    const std::string part = path.substr(start, end - start);
    if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ' ||
        part.find_first_of("<>\"|?*") != std::string::npos) return false;
    if (end == std::string::npos) return true;
    start = end + 1;
  }
  return false;
}

// Guest devices use backslashes on every host; validate after normalization.
inline std::optional<std::string> NormalizeGuestRelativePath(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  if (!path.empty() && !IsSafeRelativePath(path)) return std::nullopt;
  return path;
}

}  // namespace launcher
