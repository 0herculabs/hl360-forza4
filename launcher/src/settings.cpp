#include "settings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <random>

#include <toml++/toml.hpp>

namespace launcher {

namespace {

// Defaults are the runtime's own (src/input/mnk/mnk_input_driver.cpp), except
// throttle/brake which also get E/Q as their first, easiest key.
const KeyBind kDefaultBinds[] = {
    {"right_trigger", "Accelerate  (RT)", "Acelerar  (RT)", "Driving", "Conducción", "E,O", {}},
    {"left_trigger", "Brake  (LT)", "Frenar  (LT)", "Driving", "Conducción", "Q,I", {}},
    {"lstick_left", "Steer left", "Girar a la izquierda", "Driving", "Conducción", "A", {}},
    {"lstick_right", "Steer right", "Girar a la derecha", "Driving", "Conducción", "D", {}},
    {"lstick_up", "Left stick up", "Stick izquierdo arriba", "Driving", "Conducción", "W", {}},
    {"lstick_down", "Left stick down", "Stick izquierdo abajo", "Driving", "Conducción", "S", {}},
    {"a", "A", "A", "Buttons", "Botones", "Semicolon,Space", {}},
    {"b", "B", "B", "Buttons", "Botones", "Quote,Backspace", {}},
    {"x", "X", "X", "Buttons", "Botones", "L", {}},
    {"y", "Y", "Y", "Buttons", "Botones", "P", {}},
    {"left_shoulder", "LB", "LB", "Buttons", "Botones", "1", {}},
    {"right_shoulder", "RB", "RB", "Buttons", "Botones", "3", {}},
    {"start", "Start", "Start", "Buttons", "Botones", "X,Return", {}},
    {"back", "Back", "Back", "Buttons", "Botones", "Z,Tab", {}},
    {"lstick_press", "Left stick click", "Pulsar stick izquierdo", "Buttons", "Botones", "F", {}},
    {"dpad_up", "Up", "Arriba", "D-pad", "Cruceta", "Shift+Up", {}},
    {"dpad_down", "Down", "Abajo", "D-pad", "Cruceta", "Shift+Down", {}},
    {"dpad_left", "Left", "Izquierda", "D-pad", "Cruceta", "Shift+Left", {}},
    {"dpad_right", "Right", "Derecha", "D-pad", "Cruceta", "Shift+Right", {}},
    {"rstick_up", "Look up", "Mirar arriba", "Camera", "Cámara", "Up", {}},
    {"rstick_down", "Look down", "Mirar abajo", "Camera", "Cámara", "Down", {}},
    {"rstick_left", "Look left", "Mirar a la izquierda", "Camera", "Cámara", "Left", {}},
    {"rstick_right", "Look right", "Mirar a la derecha", "Camera", "Cámara", "Right", {}},
    {"rstick_press", "Right stick click", "Pulsar stick derecho", "Camera", "Cámara", "K", {}},
};

template <typename T>
void Read(const toml::table& t, const char* key, T& out) {
  if (auto v = t[key].value<T>()) out = *v;
}

void ReadInt(const toml::table& t, const char* key, int& out) {
  if (auto v = t[key].value<int64_t>()) out = int(*v);
}

bool WriteFile(const std::filesystem::path& file, const std::string& header, const toml::table& t) {
  auto tmp = file;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) return false;
    out << header << t << "\n";
    if (!out) return false;
  }
  std::error_code ec;
  std::filesystem::rename(tmp, file, ec);
  return !ec;
}

}  // namespace

Settings::Settings() { ResetBinds(); }

void Settings::ResetBinds() {
  binds.assign(std::begin(kDefaultBinds), std::end(kDefaultBinds));
  for (auto& b : binds) b.keys = b.default_keys;
}

void Settings::ResetDisplay() {
  Settings defaults;
  fullscreen = defaults.fullscreen;
  monitor = defaults.monitor;
  window_width = defaults.window_width;
  window_height = defaults.window_height;
  present_letterbox = defaults.present_letterbox;
  experimental_120fps = defaults.experimental_120fps;
  resolution_scale = defaults.resolution_scale;
  anisotropic_override = defaults.anisotropic_override;
  swap_post_effect = defaults.swap_post_effect;
  present_effect = defaults.present_effect;
  fm4_frame_limit = defaults.fm4_frame_limit;
}

bool Settings::Load(const std::filesystem::path& file) {
  toml::table t;
  try {
    t = toml::parse_file(file.u8string());
  } catch (const toml::parse_error&) {
    return false;
  }
  Read(t, "game_data_root", game_data_root);
  Read(t, "user_data_root", user_data_root);
  Read(t, "profile_name", profile_name);
  Read(t, "profile_xuid", profile_xuid);
  ReadInt(t, "user_language", user_language);

  Read(t, "fullscreen", fullscreen);
  ReadInt(t, "monitor", monitor);
  ReadInt(t, "window_width", window_width);
  ReadInt(t, "window_height", window_height);
  if (window_width <= 0 || window_height <= 0) window_width = 1600, window_height = 900;
  Read(t, "present_letterbox", present_letterbox);
  double refresh_rate=60.0;
  Read(t,"video_mode_refresh_rate",refresh_rate);
  experimental_120fps=refresh_rate==120.0;

  ReadInt(t, "resolution_scale", resolution_scale);
  ReadInt(t, "anisotropic_override", anisotropic_override);
  gpu_backend = Settings{}.gpu_backend;
#if defined(__APPLE__)
  Read(t, "gpu_backend", gpu_backend);
  if (gpu_backend != "metal" && gpu_backend != "vulkan") gpu_backend = "vulkan";
#endif
  ReadInt(t, "fm4_frame_limit", fm4_frame_limit);
  if (fm4_frame_limit != 30 && fm4_frame_limit != 60) fm4_frame_limit = 0;
  // Migrate the old independent controls to mutually exclusive frame-rate modes.
  if (fm4_frame_limit == 30) experimental_120fps = false;
  else if (experimental_120fps) fm4_frame_limit = 0;
  Read(t, "controller_vibration", controller_vibration);
  Read(t, "swap_post_effect", swap_post_effect);
  Read(t, "present_effect", present_effect);

  Read(t, "input_backend", input_backend);
#if defined(__APPLE__)
  input_backend = "sdl";
#endif
  Read(t, "wheel_enabled", wheel.enabled);
  Read(t, "wheel_device", wheel.device);
  Read(t, "wheel_ffb", wheel.ffb); Read(t, "wheel_ffb_invert", wheel.ffb_invert);
  ReadInt(t, "wheel_ffb_gain", wheel.ffb_gain);
  wheel.ffb_gain = std::clamp(wheel.ffb_gain,0,100);
#define READ_WHEEL(name) ReadInt(t, "wheel_" #name, wheel.name)
  READ_WHEEL(steering_axis); READ_WHEEL(throttle_axis); READ_WHEEL(brake_axis);
  READ_WHEEL(left); READ_WHEEL(center); READ_WHEEL(right);
  READ_WHEEL(throttle_rest); READ_WHEEL(throttle_full); READ_WHEEL(brake_rest); READ_WHEEL(brake_full);
  READ_WHEEL(calibration);
  READ_WHEEL(shift_up_action); READ_WHEEL(shift_down_action);
  Read(t, "wheel_clutch_enabled", wheel.clutch_enabled);
  READ_WHEEL(clutch_axis); READ_WHEEL(clutch_rest); READ_WHEEL(clutch_full);
  READ_WHEEL(clutch_calibration); READ_WHEEL(clutch_action); READ_WHEEL(clutch_threshold);
#undef READ_WHEEL
  const char* wheel_buttons[]{"a","b","x","y","lb","rb","start","back","ls","rs","up","down","left","right","shift_up","shift_down"};
  for (size_t i=0;i<wheel.buttons.size();++i) ReadInt(t,(std::string("wheel_button_")+wheel_buttons[i]).c_str(),wheel.buttons[i]);
  Read(t, "mnk_mode", mnk_mode);
  Read(t, "mnk_mouse", mnk_mouse);
  Read(t, "mnk_sensitivity", mnk_sensitivity);
  for (auto& b : binds) {
    Read(t, (std::string("keybind_") + b.id).c_str(), b.keys);
  }
  Read(t, "audio_mute", audio_mute);
  return true;
}

bool Settings::Save(const std::filesystem::path& file) const {
  toml::table t;
  t.insert("game_data_root", game_data_root);
  t.insert("user_data_root", user_data_root);
  t.insert("profile_name", profile_name);
  t.insert("profile_xuid", profile_xuid);
  t.insert("user_language", user_language);

  t.insert("fullscreen", fullscreen);
  t.insert("monitor", monitor);
  t.insert("window_width", window_width);
  t.insert("window_height", window_height);
  // Not the host's vertical sync but the guest vblank timer: FM4 paces itself
  // on it and runs too fast without it. Always on (older files may say off).
  t.insert("vsync", true);
  t.insert("present_letterbox", present_letterbox);

  t.insert("gpu_backend", gpu_backend);
  t.insert("fm4_frame_limit", fm4_frame_limit);
  t.insert("controller_vibration", controller_vibration);
  t.insert("resolution_scale", resolution_scale);
  t.insert("anisotropic_override", anisotropic_override);
  t.insert("swap_post_effect", swap_post_effect);
  t.insert("present_effect", present_effect);
  // FM4 needs render-to-texture readback for the road and car thumbnails.
  // "fast" keeps the fix without the 30 fps cost of the synchronous mode.
  // Always write it so old launcher files containing full/some/none are healed.
  t.insert("readback_resolve", "fast");

  t.insert("input_backend", input_backend);
  t.insert("video_mode_refresh_rate", experimental_120fps?120.0:60.0);
  t.insert("vsync",true);
  t.insert("wheel_enabled", wheel.enabled);
  t.insert("wheel_device", wheel.device);
  t.insert("wheel_ffb", wheel.ffb);t.insert("wheel_ffb_invert", wheel.ffb_invert);
  t.insert("wheel_ffb_gain", std::clamp(wheel.ffb_gain,0,100));
#define WRITE_WHEEL(name) t.insert("wheel_" #name, wheel.name)
  WRITE_WHEEL(steering_axis); WRITE_WHEEL(throttle_axis); WRITE_WHEEL(brake_axis);
  WRITE_WHEEL(left); WRITE_WHEEL(center); WRITE_WHEEL(right);
  WRITE_WHEEL(throttle_rest); WRITE_WHEEL(throttle_full); WRITE_WHEEL(brake_rest); WRITE_WHEEL(brake_full);
  WRITE_WHEEL(calibration);
  WRITE_WHEEL(shift_up_action); WRITE_WHEEL(shift_down_action);
  t.insert("wheel_clutch_enabled", wheel.clutch_enabled);
  WRITE_WHEEL(clutch_axis); WRITE_WHEEL(clutch_rest); WRITE_WHEEL(clutch_full);
  WRITE_WHEEL(clutch_calibration); WRITE_WHEEL(clutch_action); WRITE_WHEEL(clutch_threshold);
#undef WRITE_WHEEL
  const char* wheel_buttons[]{"a","b","x","y","lb","rb","start","back","ls","rs","up","down","left","right","shift_up","shift_down"};
  for (size_t i=0;i<wheel.buttons.size();++i) t.insert(std::string("wheel_button_")+wheel_buttons[i],wheel.buttons[i]);
  t.insert("mnk_mode", mnk_mode);
  t.insert("mnk_mouse", mnk_mouse);
  t.insert("mnk_sensitivity", mnk_sensitivity);
  for (const auto& b : binds) {
    t.insert(std::string("keybind_") + b.id, b.keys);
  }
  // Stable port defaults; migrate values written by the experimental UI.
  // Cached pipelines are already precompiled by the runtime at startup.
  t.insert("async_shader_compilation", false);
  t.insert("frame_stats", false);
  t.insert("audio_mute", audio_mute);
  return WriteFile(file,
                   "# Written by the FM4 launcher. The game loads this after fm4.toml,\n"
                   "# so these values win. Edit from the launcher.\n\n",
                   t);
}

bool LauncherState::Load(const std::filesystem::path& file) {
  toml::table t;
  try {
    t = toml::parse_file(file.u8string());
  } catch (const toml::parse_error&) {
    return false;
  }
  std::string language;
  Read(t, "language", language);
  spanish = language == "es";
  Read(t, "active_profile", active_xuid);
  Read(t, "game_dir", legacy_game_dir);
  Read(t, "user_dir", legacy_user_dir);
  profiles.clear();
  if (auto* list = t["profiles"].as_array()) {
    for (auto& node : *list) {
      auto* p = node.as_table();
      if (!p) continue;
      Profile profile{(*p)["name"].value_or(std::string{}), (*p)["xuid"].value_or(std::string{})};
      if (!IsValidGamertag(profile.name)) continue;
      if (profile.xuid.size() != 16) profile.xuid = GenerateXuid();
      profiles.push_back(std::move(profile));
    }
  }
  return true;
}

bool LauncherState::Save(const std::filesystem::path& file) const {
  toml::table t;
  t.insert("language", spanish ? "es" : "en");
  t.insert("active_profile", active_xuid);
  toml::array list;
  for (const auto& p : profiles) {
    list.push_back(toml::table{{"name", p.name}, {"xuid", p.xuid}});
  }
  t.insert("profiles", std::move(list));
  return WriteFile(file, "# FM4 launcher: UI language and local profiles.\n\n", t);
}

bool IsValidGamertag(const std::string& name) {
  if (name.empty() || name.size() > 15 || name.front() == ' ' || name.back() == ' ') {
    return false;
  }
  char prev = 0;
  for (char c : name) {
    const bool alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (!alnum && !(c == ' ' && prev != ' ')) return false;
    prev = c;
  }
  return true;
}

std::vector<Profile> DiscoverSavedProfiles(const std::filesystem::path& user_root) {
  std::vector<Profile> profiles;
  std::error_code ec;
  std::filesystem::directory_iterator it(user_root, ec), end;
  for (; !ec && it != end; it.increment(ec)) {
    auto xuid = it->path().filename().u8string();
    if (xuid.size() != 16 || xuid == "0000000000000000" ||
        !std::all_of(xuid.begin(), xuid.end(), [](unsigned char c) { return std::isxdigit(c); }))
      continue;
    const auto save = it->path() / "4D530910/00000001/ForzaProfile";
    std::error_code save_error;
    if (!std::filesystem::is_regular_file(save / "ForzaProfile", save_error) ||
        !std::filesystem::is_regular_file(save / "PlayerDatabase", save_error)) continue;
    std::transform(xuid.begin(), xuid.end(), xuid.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    profiles.push_back({xuid == "B13EBABEBABEBABE" ? "User" : "Player " + xuid.substr(12), xuid});
  }
  std::sort(profiles.begin(), profiles.end(),
            [](const Profile& a, const Profile& b) { return a.xuid < b.xuid; });
  return profiles;
}

std::string GenerateXuid() {
  std::random_device rd;
  uint64_t value = (static_cast<uint64_t>(rd()) << 32) | rd();
  value = 0xE000000000000000ull | (value & 0x00003FFFFFFFFFFFull);
  char buf[17];
  std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(value));
  return buf;
}

}  // namespace launcher




