#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include "../../src/wheel.h"

namespace launcher {

// One keyboard binding of the virtual pad (cvar keybind_<id>). `keys` uses the
// runtime's syntax: comma-separated alternatives, optional "Shift+" prefix.
struct KeyBind {
  const char* id;        // cvar suffix
  const char* label;     // shown in the launcher (English)
  const char* label_es;  // ... (Spanish)
  const char* group;     // section in the Controls page (English)
  const char* group_es;
  const char* default_keys;
  std::string keys;
};

// A local profile: the gamertag the game shows and the offline XUID its saves
// are stored under (<user data>/<XUID>/4D530910/...).
struct Profile {
  std::string name;
  std::string xuid;  // 16 hex digits
};

// Everything the launcher lets the player change. Written to fm4.user.toml,
// which the game loads after its own fm4.toml.
struct Settings {
  // Paths and profile (the active one is also written for the game).
  std::string game_data_root;
  std::string user_data_root;
  std::string profile_name = "Player";
  std::string profile_xuid;
  int user_language = 1;  // XC_LANGUAGE

  // Display
  bool fullscreen = true;
  int monitor = 0;
  int window_width = 1600;
  int window_height = 900;
  bool present_letterbox = true;
  bool experimental_120fps = false;

  // Quality
  std::string gpu_backend = "d3d12";  // d3d12 | vulkan
  int resolution_scale = 1;      // internal render scale over 1280x720
  int anisotropic_override = 3;  // 0..5 = off,1x,2x,4x,8x,16x
  std::string swap_post_effect = "none";    // none | fxaa | fxaa_extreme
  std::string present_effect = "bilinear";  // bilinear | cas | fsr

  // Input
  std::string input_backend = "sdl";  // sdl | xinput
  fm4::wheel::Config wheel;
  bool mnk_mode = true;
  bool mnk_mouse = false;
  double mnk_sensitivity = 1.0;
  std::vector<KeyBind> binds;

  // Audio
  bool audio_mute = false;

  Settings();
  void ResetBinds();
  void ResetDisplay();
  bool Load(const std::filesystem::path& file);
  bool Save(const std::filesystem::path& file) const;
};

// launcher.toml: the launcher's own state.
struct LauncherState {
  bool spanish = false;
  std::vector<Profile> profiles;
  std::string active_xuid;
  // Paths from older launcher.toml files (game_dir / user_dir), used when
  // fm4.user.toml does not have them yet.
  std::string legacy_game_dir;
  std::string legacy_user_dir;

  bool Load(const std::filesystem::path& file);
  bool Save(const std::filesystem::path& file) const;
};

// Gamertag rules shared with the game: 1-15 letters, digits and single spaces.
bool IsValidGamertag(const std::string& name);

// Random offline XUID (0xE000xxxxxxxxxxxx) with the restricted-account bits clear.
std::string GenerateXuid();

}  // namespace launcher
