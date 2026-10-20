#pragma once

#include <filesystem>

namespace launcher {
// A Finder bundle lives beside the game; its executable lives three levels in.
inline std::filesystem::path PackageDirectory(std::filesystem::path base) {
  base = base.lexically_normal();
  if (base.filename().empty()) base = base.parent_path();
#if defined(__APPLE__)
  // SDL returns Resources for an application bundle, MacOS for some tools.
  if ((base.filename() == "MacOS" || base.filename() == "Resources") && base.parent_path().filename() == "Contents" &&
      base.parent_path().parent_path().extension() == ".app") {
    return base.parent_path().parent_path().parent_path();
  }
#endif
  return base;
}
}  // namespace launcher
