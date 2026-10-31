#include "settings.h"

#include <algorithm>
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
    // The game's camera adjustments (src/camera_view.cpp), with or without the keyboard pad.
    {"camera_fov_in", "Field of view: narrower", "Campo de visión: más estrecho", "Field of view", "Campo de visión",
     "Plus,NumpadPlus", {}, true},
    {"camera_fov_out", "Field of view: wider", "Campo de visión: más amplio", "Field of view", "Campo de visión",
     "Minus,NumpadMinus", {}, true},
    {"camera_fov_reset", "Field of view: default", "Campo de visión: original", "Field of view", "Campo de visión",
     "0,Numpad0", {}, true},
    {"camera_seat_up", "Seat up", "Asiento arriba", "Seat position", "Posición del asiento", "PageUp", {}, true},
    {"camera_seat_down", "Seat down", "Asiento abajo", "Seat position", "Posición del asiento", "PageDown", {}, true},
    {"camera_seat_forward", "Seat forward", "Asiento adelante", "Seat position", "Posición del asiento", "Home", {}, true},
    {"camera_seat_back", "Seat back", "Asiento atrás", "Seat position", "Posición del asiento", "End", {}, true},
    {"camera_seat_reset", "Seat: default", "Asiento: original", "Seat position", "Posición del asiento", "Insert", {}, true},
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


// The wheel_* and telemetry_* keys (src/wheel_input.cpp, src/telemetry).
void ReadWheel(const toml::table& t, fm4::wheel::Settings& w) {
  Read(t, "wheel_enabled", w.enabled);
  Read(t, "wheel_device", w.device);
  w.device = fm4::wheel::MigrateDeviceKey(w.device);
  ReadInt(t, "wheel_steering_axis", w.steering_axis);
  Read(t, "wheel_steering_invert", w.steering_invert);
  ReadInt(t, "wheel_steering_scale", w.steering_scale);
  ReadInt(t, "wheel_steering_deadzone", w.steering_deadzone);
  ReadInt(t, "wheel_rotation", w.wheel_rotation);
  ReadInt(t, "wheel_game_rotation", w.game_rotation);
  Read(t, "wheel_throttle", w.throttle);
  Read(t, "wheel_brake", w.brake);
  Read(t, "wheel_clutch", w.clutch);
  Read(t, "wheel_clutch_button", w.clutch_button);
  Read(t, "wheel_clutch_analog", w.clutch_analog);
  Read(t, "wheel_buttons", w.buttons);
  ReadInt(t, "wheel_pedal_deadzone", w.pedal_deadzone);
  const char* pedals[]{"throttle", "brake", "clutch"};
  for (int i = 0; i < 3; ++i) {
    ReadInt(t, (std::string("wheel_") + pedals[i] + "_deadzone_bottom").c_str(), w.deadzone_bottom[i]);
    ReadInt(t, (std::string("wheel_") + pedals[i] + "_deadzone_top").c_str(), w.deadzone_top[i]);
  }
  Read(t, "wheel_ffb_device", w.ffb_device);
  Read(t, "wheel_ffb_mode", w.ffb_mode);
  w.ffb_mode = fm4::wheel::FfbModeName(fm4::wheel::ParseFfbMode(w.ffb_mode));
  ReadInt(t, "wheel_ffb_strength", w.ffb_strength);
  ReadInt(t, "wheel_ffb_min_force", w.ffb_min_force);
  ReadInt(t, "wheel_ffb_spring", w.ffb_spring);
  ReadInt(t, "wheel_ffb_damper", w.ffb_damper);
  ReadInt(t, "wheel_ffb_friction", w.ffb_friction);
  ReadInt(t, "wheel_ffb_inertia", w.ffb_inertia);
  ReadInt(t, "wheel_ffb_rumble", w.ffb_rumble);
  ReadInt(t, "wheel_ffb_rumble_engine", w.ffb_rumble_engine);
  ReadInt(t, "wheel_ffb_rumble_road", w.ffb_rumble_road);
  Read(t, "wheel_ffb_invert", w.ffb_invert);
  Read(t, "wheel_log_inputs", w.log_inputs);
  Read(t, "telemetry_udp", w.telemetry_udp);
  Read(t, "telemetry_udp_format", w.telemetry_format);
  Read(t, "telemetry_udp_target", w.telemetry_target);
  Read(t, "telemetry_udp_moving_only", w.telemetry_moving_only);

  // FM4 0.3 stored calibrated axes and button numbers on the one wheel device.
  // Carry them over once; the next save writes only the keys above.
  auto old_int = [&](const char* key, int fallback) {
    auto v = t[key].value<int64_t>();
    return v ? int(*v) : fallback;
  };
  auto old_axis = [&](const char* axis, const char* rest, const char* full) {
    if (!t.contains(axis)) return std::string();
    const bool inverted = old_int(rest, -32768) > old_int(full, 32767);
    return std::string(inverted ? "-" : "+") + "axis" + std::to_string(old_int(axis, 0));
  };
  if (!t.contains("wheel_throttle") && t.contains("wheel_throttle_axis")) {
    w.throttle = old_axis("wheel_throttle_axis", "wheel_throttle_rest", "wheel_throttle_full");
    w.brake = old_axis("wheel_brake_axis", "wheel_brake_rest", "wheel_brake_full");
    if (t["wheel_clutch_enabled"].value_or(false) && old_int("wheel_clutch_axis", -1) >= 0)
      w.clutch = old_axis("wheel_clutch_axis", "wheel_clutch_rest", "wheel_clutch_full");
    // The old steering ran from a captured left to right; the new one uses
    // the axis's whole range, so the old right end tells its direction.
    w.steering_invert = old_int("wheel_right", 32767) < old_int("wheel_left", -32768);
    const char* names[]{"A", "B", "X", "Y", "LB", "RB", "START", "BACK", "LS", "RS", "UP", "DOWN", "LEFT", "RIGHT"};
    const char* keys[]{"a", "b", "x", "y", "lb", "rb", "start", "back", "ls", "rs", "up", "down", "left", "right"};
    std::string buttons = "UP=h0.up,DOWN=h0.down,LEFT=h0.left,RIGHT=h0.right";
    for (int i = 0; i < 14; ++i) {
      const int n = old_int((std::string("wheel_button_") + keys[i]).c_str(), -1);
      if (n >= 0) buttons += std::string(",") + names[i] + "=" + std::to_string(n);
    }
    const int up = old_int("wheel_button_shift_up", -1), down = old_int("wheel_button_shift_down", -1);
    const int up_action = old_int("wheel_shift_up_action", 5), down_action = old_int("wheel_shift_down_action", 4);
    if (up >= 0 && up_action >= 0 && up_action < 14) buttons += std::string(",") + names[up_action] + "=" + std::to_string(up);
    if (down >= 0 && down_action >= 0 && down_action < 14)
      buttons += std::string(",") + names[down_action] + "=" + std::to_string(down);
    w.buttons = buttons;
    const int clutch_action = old_int("wheel_clutch_action", 1);
    if (clutch_action >= 0 && clutch_action < 14) w.clutch_button = names[clutch_action];
  }
  if (!t.contains("wheel_ffb_mode") && t.contains("wheel_ffb")) {
    w.ffb_mode = t["wheel_ffb"].value_or(false) ? "on" : "off";
    w.ffb_strength = std::clamp(old_int("wheel_ffb_gain", w.ffb_strength), 0, 100);
  }
}

void WriteWheel(toml::table& t, const fm4::wheel::Settings& w) {
  t.insert("wheel_enabled", w.enabled);
  t.insert("wheel_device", w.device);
  t.insert("wheel_steering_axis", w.steering_axis);
  t.insert("wheel_steering_invert", w.steering_invert);
  t.insert("wheel_steering_scale", std::clamp(w.steering_scale, 10, 1000));
  t.insert("wheel_steering_deadzone", std::clamp(w.steering_deadzone, 0, 50));
  t.insert("wheel_rotation", std::clamp(w.wheel_rotation, 90, 3600));
  t.insert("wheel_game_rotation", w.game_rotation > 0 ? std::clamp(w.game_rotation, 90, 3600) : 0);
  t.insert("wheel_throttle", w.throttle);
  t.insert("wheel_brake", w.brake);
  t.insert("wheel_clutch", w.clutch);
  t.insert("wheel_clutch_button", w.clutch_button);
  t.insert("wheel_clutch_analog", w.clutch_analog);
  t.insert("wheel_buttons", w.buttons);
  t.insert("wheel_pedal_deadzone", std::clamp(w.pedal_deadzone, 0, 50));
  const char* pedals[]{"throttle", "brake", "clutch"};
  for (int i = 0; i < 3; ++i) {
    t.insert(std::string("wheel_") + pedals[i] + "_deadzone_bottom", std::clamp(w.deadzone_bottom[i], -1, 50));
    t.insert(std::string("wheel_") + pedals[i] + "_deadzone_top", std::clamp(w.deadzone_top[i], 0, 50));
  }
  t.insert("wheel_ffb_device", w.ffb_device);
  t.insert("wheel_ffb_mode", w.ffb_mode);
  t.insert("wheel_ffb_strength", std::clamp(w.ffb_strength, 0, 200));
  t.insert("wheel_ffb_min_force", std::clamp(w.ffb_min_force, 0, 30));
  t.insert("wheel_ffb_spring", std::clamp(w.ffb_spring, 0, 200));
  t.insert("wheel_ffb_damper", std::clamp(w.ffb_damper, 0, 200));
  t.insert("wheel_ffb_friction", std::clamp(w.ffb_friction, 0, 200));
  t.insert("wheel_ffb_inertia", std::clamp(w.ffb_inertia, 0, 200));
  t.insert("wheel_ffb_rumble", std::clamp(w.ffb_rumble, 0, 100));
  t.insert("wheel_ffb_rumble_engine", std::clamp(w.ffb_rumble_engine, 0, 100));
  t.insert("wheel_ffb_rumble_road", std::clamp(w.ffb_rumble_road, 0, 100));
  t.insert("wheel_ffb_invert", w.ffb_invert);
  t.insert("wheel_log_inputs", w.log_inputs);
  t.insert("telemetry_udp", w.telemetry_udp);
  t.insert("telemetry_udp_format", w.telemetry_format == "horizon" ? "horizon" : "fm7");
  t.insert("telemetry_udp_target", w.telemetry_target);
  t.insert("telemetry_udp_moving_only", w.telemetry_moving_only);
}

}  // namespace

void Settings::ApplyGraphicsPreset(int preset) {
  graphics_preset = std::clamp(preset, 0, 3);
  if (graphics_preset == 3) return;
  resolution_scale = graphics_preset == 0 ? 1 : 2;
  anisotropic_override = 3 + graphics_preset;
  present_effect = graphics_preset == 2 ? "bilinear" : "fsr";
  swap_post_effect = "none";
}

Settings::Settings() {
  binds.assign(std::begin(kDefaultBinds), std::end(kDefaultBinds));
  for (auto& b : binds) b.keys = b.default_keys;
}

void Settings::ResetBinds(bool camera) {
  for (auto& b : binds) {
    if (b.camera == camera) b.keys = b.default_keys;
  }
}

void Settings::ResetDisplay() {
  Settings defaults;
  fullscreen = defaults.fullscreen;
  vsync = defaults.vsync;
  monitor = defaults.monitor;
  window_width = defaults.window_width;
  window_height = defaults.window_height;
  present_letterbox = defaults.present_letterbox;
  display_wide_view = defaults.display_wide_view;
  display_hud_edges = defaults.display_hud_edges;
  experimental_120fps = defaults.experimental_120fps;
  resolution_scale = defaults.resolution_scale;
  anisotropic_override = defaults.anisotropic_override;
  swap_post_effect = defaults.swap_post_effect;
  present_effect = defaults.present_effect;
  video_mode_width = defaults.video_mode_width;
  video_mode_height = defaults.video_mode_height;
  present_fsr_sharpness_reduction = defaults.present_fsr_sharpness_reduction;
  exterior_shadows = defaults.exterior_shadows;
  car_detail = defaults.car_detail;
  full_rate_mirror = defaults.full_rate_mirror;
  full_rate_bloom = defaults.full_rate_bloom;
  soft_particles = defaults.soft_particles;
  scenery_shadows = defaults.scenery_shadows;
  reflections = defaults.reflections;
  crowds_every_race = defaults.crowds_every_race;
  ApplyGraphicsPreset(1);
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
  Read(t, "vsync", vsync);
  ReadInt(t, "monitor", monitor);
  ReadInt(t, "window_width", window_width);
  ReadInt(t, "window_height", window_height);
  if (window_width <= 0 || window_height <= 0) window_width = 1600, window_height = 900;
  Read(t, "present_letterbox", present_letterbox);
  Read(t, "display_wide_view", display_wide_view);
  Read(t, "display_hud_edges", display_hud_edges);
  double refresh_rate=60.0;
  Read(t,"video_mode_refresh_rate",refresh_rate);
  experimental_120fps=refresh_rate==120.0;

  ReadInt(t, "resolution_scale", resolution_scale);
  ReadInt(t, "anisotropic_override", anisotropic_override);
  graphics_preset = 3;
  ReadInt(t, "graphics_preset", graphics_preset);
  graphics_preset = std::clamp(graphics_preset, 0, 3);
  // ShiftGlue's Vulkan backend draws FM4 best: always Vulkan (drops stale values).
  gpu_backend = "vulkan";
  Read(t, "swap_post_effect", swap_post_effect);
  Read(t, "present_effect", present_effect);
  // Xenia profile baseline: default 720p guest mode, independent of output size.
  video_mode_width = 1280;
  video_mode_height = 720;
  present_fsr_sharpness_reduction = 0.2;
  async_shader_compilation = false;
  readback_resolve = "none";
  resolution_scale = std::clamp(resolution_scale, 1, 4);
  anisotropic_override = std::clamp(anisotropic_override, 0, 5);
  swap_post_effect = "none";
  if (graphics_preset < 3) ApplyGraphicsPreset(graphics_preset);
  Read(t, "scenario_exterior_shadows", exterior_shadows);
  ReadInt(t, "scenario_car_detail", car_detail);
  car_detail = std::clamp(car_detail, 0, 2);
  Read(t, "scenario_full_rate_mirror", full_rate_mirror);
  Read(t, "scenario_full_rate_bloom", full_rate_bloom);
  Read(t, "scenario_soft_particles", soft_particles);
  Read(t, "scenario_scenery_shadows", scenery_shadows);
  ReadInt(t, "scenario_reflections", reflections);
  reflections = std::clamp(reflections, 0, 2);
  Read(t, "scenario_crowds_every_race", crowds_every_race);

  Read(t, "input_backend", input_backend);
  ReadWheel(t, wheel);
  Read(t, "mnk_mode", mnk_mode);
  Read(t, "mnk_mouse", mnk_mouse);
  Read(t, "mnk_sensitivity", mnk_sensitivity);
  for (auto& b : binds) {
    Read(t, (std::string("keybind_") + b.id).c_str(), b.keys);
  }
  Read(t, "audio_mute", audio_mute);
  Read(t, "audio_output_mode", audio_output_mode);
  if (audio_output_mode != "auto" && audio_output_mode != "stereo" && audio_output_mode != "surround")
    audio_output_mode = "auto";
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
  t.insert("vsync", vsync);
  // Vulkan: VSync on presents FIFO; off presents immediate, which also keeps
  // an independent flip and so variable refresh rate (mailbox went through
  // the desktop compositor on Nvidia).
  if (gpu_backend == "vulkan") {
    t.insert("vulkan_present_mode", vsync ? "fifo" : "immediate");
    for (const char* mode : {"immediate", "mailbox", "fifo_relaxed"})
      t.insert(std::string("vulkan_allow_present_mode_") + mode, !vsync);
  }
  t.insert("present_letterbox", present_letterbox);
  t.insert("display_wide_view", display_wide_view);
  t.insert("display_hud_edges", display_hud_edges);

  t.insert("gpu_backend", gpu_backend);
  t.insert("resolution_scale", resolution_scale);
  t.insert("graphics_preset", graphics_preset);
  t.insert("scenario_exterior_shadows", exterior_shadows);
  t.insert("scenario_car_detail", car_detail);
  t.insert("scenario_full_rate_mirror", full_rate_mirror);
  t.insert("scenario_full_rate_bloom", full_rate_bloom);
  t.insert("scenario_soft_particles", soft_particles);
  t.insert("scenario_scenery_shadows", scenery_shadows);
  t.insert("scenario_reflections", reflections);
  t.insert("scenario_crowds_every_race", crowds_every_race);
  t.insert("anisotropic_override", anisotropic_override);
  t.insert("swap_post_effect", swap_post_effect);
  t.insert("present_effect", present_effect);
  t.insert("video_mode_width", 1280);
  t.insert("video_mode_height", 720);
  t.insert("present_fsr_sharpness_reduction", 0.2);
  // Preserve the selected mode across every launcher save.
  t.insert("readback_resolve", "none");
  t.insert("readback_memexport", false);

  t.insert("input_backend", input_backend);
  t.insert("video_mode_refresh_rate", experimental_120fps?120.0:60.0);
  // The host presenter has its own limit; at 60 it capped the 120 FPS mode.
  t.insert("host_present_fps_limit", experimental_120fps?120:60);
  // FM4 steps once per guest vblank (ShiftGlue's default is FH1's twice).
  t.insert("fh1_guest_vblank_hz", experimental_120fps?120.0:60.0);
  WriteWheel(t, wheel);
  t.insert("mnk_mode", mnk_mode);
  t.insert("mnk_mouse", mnk_mouse);
  t.insert("mnk_sensitivity", mnk_sensitivity);
  for (const auto& b : binds) {
    t.insert(std::string("keybind_") + b.id, b.keys);
  }
  t.insert("async_shader_compilation", false);
  t.insert("frame_stats", false);
  t.insert("audio_mute", audio_mute);
  t.insert("audio_output_mode", audio_output_mode);
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

std::string GenerateXuid() {
  std::random_device rd;
  uint64_t value = (static_cast<uint64_t>(rd()) << 32) | rd();
  value = 0xE000000000000000ull | (value & 0x00003FFFFFFFFFFFull);
  char buf[17];
  std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(value));
  return buf;
}

}  // namespace launcher




