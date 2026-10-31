#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include "../../src/wheel_mapping.h"

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
  // The game's camera adjustments (keybind_camera_*): work without the
  // keyboard pad, shown in their own card.
  bool camera = false;
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
  bool vsync = true;
  int monitor = 0;
  int window_width = 1600;
  int window_height = 900;
  bool present_letterbox = true;
  // Wide windows show more of the world at the sides (the game's Hor+); the
  // HUD and 16:9 screens keep their proportions.
  bool display_wide_view = true;
  bool display_hud_edges = false;
  bool experimental_120fps = false;

  // Quality
  std::string gpu_backend = "vulkan";  // d3d12 | vulkan
  int resolution_scale = 2;      // internal render scale over 1280x720
  int anisotropic_override = 4;  // 0..5 = off,1x,2x,4x,8x,16x
  std::string swap_post_effect = "none";    // none | fxaa | fxaa_extreme
  std::string present_effect = "fsr";  // bilinear | cas | fsr
  int video_mode_width = 1280;
  int video_mode_height = 720;
  double present_fsr_sharpness_reduction = 0.2;
  int graphics_preset = 1;  // 0 low, 1 medium, 2 high, 3 custom
  void ApplyGraphicsPreset(int preset);

  bool async_shader_compilation = false;
  std::string readback_resolve = "none";  // none | fast | full

  // Race render scenario settings (graphics_fixes.h), applied on Play.
  bool exterior_shadows = false;  // off: the race shadow workaround
  int car_detail = 0;             // 0 console, 1 high, 2 ultra
  bool full_rate_mirror = false;
  bool full_rate_bloom = false;
  bool soft_particles = false;
  bool scenery_shadows = false;
  int reflections = 0;            // 0 console, 1 every race, 2 double
  bool crowds_every_race = false;

  // Input
  std::string input_backend = "sdl";  // sdl | xinput
  fm4::wheel::Settings wheel;  // wheel_* and telemetry_* keys
  bool mnk_mode = true;
  bool mnk_mouse = false;
  double mnk_sensitivity = 1.0;
  std::vector<KeyBind> binds;

  // Audio
  bool audio_mute = false;
  std::string audio_output_mode = "auto";

  Settings();
  // The keyboard pad's keys, or the camera's.
  void ResetBinds(bool camera = false);
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
