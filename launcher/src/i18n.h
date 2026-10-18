#pragma once

// Launcher UI language: English by default, Spanish on request (EN | ES
// switch in the tab bar, remembered in launcher.toml). Every user-visible
// string is written as L("English", "Español").

namespace launcher {

inline bool g_spanish = false;

inline const char* L(const char* en, const char* es) { return g_spanish ? es : en; }

}  // namespace launcher
