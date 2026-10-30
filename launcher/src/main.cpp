// fm4-launcher - profiles, settings and game install for the recompiled
// Forza Motorsport 4. Sits next to fm4.exe; writes fm4.user.toml (read by the
// game after its own fm4.toml) and launches the game.
//
// Layout: the cover art at the top of a column on the left with the profile
// picker and the play button under it; settings live in tabs on the right,
// grouped into cards. The cover is the dashboard tile art of the player's own
// Disc 1 (nxeart), extracted at install time - it is not part of the program.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "../../src/crash_handler.h"
#include "../../src/wheel.h"
#include "i18n.h"
#include "installer.h"
#include "graphics_fixes.h"
#include "settings.h"

namespace fs = std::filesystem;
using namespace launcher;

namespace {

// Palette, taken from the Forza Motorsport 4 logo.
const ImVec4 kAccent(0.88f, 0.25f, 0.16f, 1.0f);
const ImVec4 kAccentHover(0.97f, 0.36f, 0.25f, 1.0f);
const ImVec4 kOk(0.36f, 0.80f, 0.45f, 1.0f);
const ImVec4 kWarn(0.98f, 0.74f, 0.25f, 1.0f);
const ImVec4 kBad(0.95f, 0.35f, 0.30f, 1.0f);
const ImVec4 kText(0.93f, 0.93f, 0.95f, 1.0f);
const ImVec4 kDim(0.60f, 0.61f, 0.66f, 1.0f);
const ImVec4 kBg(0.067f, 0.071f, 0.082f, 1.0f);
const ImVec4 kCard(0.105f, 0.110f, 0.125f, 1.0f);
const ImVec4 kControl(0.165f, 0.172f, 0.192f, 1.0f);

float g_scale = 1.0f;
ImFont* g_bold = nullptr;
float S(float v) { return v * g_scale; }
ImU32 Col(const ImVec4& c, float alpha = 1.0f) {
  return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

enum class Tab { kInstructions, kProfiles, kGraphics, kControls, kWheel, kAudio, kInstall, kSupport };

// --- Key capture -----------------------------------------------------------

// SDL scancode -> name understood by the runtime's ParseVirtualKey
// (src/ui/keybinds.cpp in the SDK).
std::string KeyName(SDL_Scancode sc) {
  if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return std::string(1, char('A' + (sc - SDL_SCANCODE_A)));
  if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return std::string(1, char('1' + (sc - SDL_SCANCODE_1)));
  if (sc == SDL_SCANCODE_0) return "0";
  if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return "F" + std::to_string(1 + sc - SDL_SCANCODE_F1);
  if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return "Numpad" + std::to_string(1 + sc - SDL_SCANCODE_KP_1);
  switch (sc) {
    case SDL_SCANCODE_KP_0: return "Numpad0";
    case SDL_SCANCODE_KP_ENTER: return "NumpadEnter";
    case SDL_SCANCODE_KP_PLUS: return "NumpadPlus";
    case SDL_SCANCODE_KP_MINUS: return "NumpadMinus";
    case SDL_SCANCODE_KP_MULTIPLY: return "NumpadStar";
    case SDL_SCANCODE_KP_DIVIDE: return "NumpadSlash";
    case SDL_SCANCODE_RETURN: return "Return";
    case SDL_SCANCODE_SPACE: return "Space";
    case SDL_SCANCODE_TAB: return "Tab";
    case SDL_SCANCODE_BACKSPACE: return "Backspace";
    case SDL_SCANCODE_DELETE: return "Delete";
    case SDL_SCANCODE_INSERT: return "Insert";
    case SDL_SCANCODE_HOME: return "Home";
    case SDL_SCANCODE_END: return "End";
    case SDL_SCANCODE_PAGEUP: return "PageUp";
    case SDL_SCANCODE_PAGEDOWN: return "PageDown";
    case SDL_SCANCODE_LEFT: return "Left";
    case SDL_SCANCODE_RIGHT: return "Right";
    case SDL_SCANCODE_UP: return "Up";
    case SDL_SCANCODE_DOWN: return "Down";
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return "Shift";
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return "Control";
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return "Alt";
    case SDL_SCANCODE_GRAVE: return "Backtick";
    case SDL_SCANCODE_MINUS: return "Minus";
    case SDL_SCANCODE_EQUALS: return "Plus";
    case SDL_SCANCODE_COMMA: return "Comma";
    case SDL_SCANCODE_PERIOD: return "Period";
    case SDL_SCANCODE_SEMICOLON: return "Semicolon";
    case SDL_SCANCODE_SLASH: return "Slash";
    case SDL_SCANCODE_BACKSLASH: return "Backslash";
    case SDL_SCANCODE_LEFTBRACKET: return "LBracket";
    case SDL_SCANCODE_RIGHTBRACKET: return "RBracket";
    case SDL_SCANCODE_APOSTROPHE: return "Quote";
    case SDL_SCANCODE_CAPSLOCK: return "CapsLock";
    default: return {};
  }
}

// Friendlier names for a few runtime key names when shown in chips.
std::string KeyLabel(const std::string& key) {
  const std::string shift = L("Shift+", "Mayús+");
  const std::pair<const char*, std::string> names[] = {
      {"Space", L("Space", "Espacio")}, {"Return", "Enter"}, {"Backspace", L("Backspace", "Retroceso")},
      {"Semicolon", L("; / Ñ", "Ñ / ;")}, {"Quote", "' / ´"}, {"Up", "Up"}, {"Down", "Down"},
      {"Left", "Left"}, {"Right", "Right"}, {"Control", "Ctrl"},
  };
  std::string base = key, prefix;
  if (key.rfind("Shift+", 0) == 0) base = key.substr(6), prefix = shift;
  for (const auto& [from, to] : names) {
    if (base == from) return prefix + to;
  }
  return prefix + base;
}

// --- Cover art -------------------------------------------------------------

struct Cover {
  SDL_GPUTexture* texture = nullptr;
  SDL_GPUSampler* sampler = nullptr;
  SDL_GPUTextureSamplerBinding binding{};
  int width = 0, height = 0;
  // ImGui >= 1.92.2's SDL_GPU backend takes the SDL_GPUTexture* itself (and
  // samples it with its own linear sampler).
  ImTextureID id() const { return (ImTextureID)(intptr_t)texture; }
};

void ReleaseCover(SDL_GPUDevice* gpu, Cover& cover) {
  if (cover.texture) SDL_ReleaseGPUTexture(gpu, cover.texture);
  if (cover.sampler) SDL_ReleaseGPUSampler(gpu, cover.sampler);
  cover = Cover{};
}

bool LoadCover(SDL_GPUDevice* gpu, const fs::path& file, Cover& out) {
  std::ifstream in(file, std::ios::binary);
  const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  int w = 0, h = 0, channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()), int(data.size()),
                                          &w, &h, &channels, 4);
  if (!pixels) return false;

  SDL_GPUTextureCreateInfo info = {};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  info.width = Uint32(w);
  info.height = Uint32(h);
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  out.texture = SDL_CreateGPUTexture(gpu, &info);

  SDL_GPUTransferBufferCreateInfo tb_info = {};
  tb_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  tb_info.size = Uint32(w * h * 4);
  SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(gpu, &tb_info);
  std::memcpy(SDL_MapGPUTransferBuffer(gpu, tb, false), pixels, size_t(w) * h * 4);
  SDL_UnmapGPUTransferBuffer(gpu, tb);
  stbi_image_free(pixels);

  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu);
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureTransferInfo src = {};
  src.transfer_buffer = tb;
  SDL_GPUTextureRegion region = {};
  region.texture = out.texture;
  region.w = Uint32(w);
  region.h = Uint32(h);
  region.d = 1;
  SDL_UploadToGPUTexture(copy, &src, &region, false);
  SDL_EndGPUCopyPass(copy);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_ReleaseGPUTransferBuffer(gpu, tb);

  SDL_GPUSamplerCreateInfo sampler = {};
  sampler.min_filter = SDL_GPU_FILTER_LINEAR;
  sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
  sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
  sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  out.sampler = SDL_CreateGPUSampler(gpu, &sampler);
  out.binding.texture = out.texture;
  out.binding.sampler = out.sampler;
  out.width = w;
  out.height = h;
  return out.texture && out.sampler;
}

// --- Wheel tab ---------------------------------------------------------------
//
// The racing wheel setup from the Pinyon Shift (Forza Horizon) launcher: bind
// the wheel, pedals and buttons from any USB devices with a guided setup or
// one row at a time, watch a live view of exactly what the game will read,
// and set force feedback and telemetry. Bindings use the game's wheel_*
// syntax (src/wheel_mapping.h) and are saved as soon as they change.

namespace wheel_ui {
namespace w = fm4::wheel;
using Clock = std::chrono::steady_clock;

enum class Kind { kSteering, kPedal, kButton };
enum class InputType { kAxis, kButton, kHat };

// One physical input. `device` is the device match text (empty for none).
struct Input {
  std::string device;
  InputType type = InputType::kAxis;
  int index = 0;
  uint8_t hat = 0;
  bool inverted = false;
  bool Same(const Input& o) const {
    return type == o.type && index == o.index && hat == o.hat && device == o.device;
  }
};

struct Binding {
  const char* key;  // STEER, THROTTLE, BRAKE, CLUTCH, an Xbox control or a gear
  const char* label;
  const char* label_es;
  const char* prompt;
  const char* prompt_es;
  Kind kind;
  const char* chip;
  std::vector<Input> inputs;  // buttons may have several (any of them presses it)
  // Live, as the game would read it.
  float value = 0;
  bool pressed = false;
  std::string text;
};

std::vector<Binding> DefaultRows() {
  auto button = [](const char* key, const char* label, const char* label_es, const char* chip) {
    return Binding{key, label, label_es,
               "Press the input you want for this Xbox button: a rim button, a button box, a shifter paddle, a hat or a lever.",
               "Pulsa la entrada que quieras para este botón de Xbox: un botón del aro, una botonera, una leva, una cruceta o una palanca.",
               Kind::kButton, chip, {}};
  };
  auto camera = [](const char* key, const char* label, const char* label_es) {
    return Binding{key, label, label_es,
                   "Press the input you want for this: a rim button, a button box button or a hat direction. Held in game, it keeps going.",
                   "Pulsa la entrada que quieras para esto: un botón del aro, de una botonera o una cruceta. Mantenido en el juego, sigue.",
                   Kind::kButton, "", {}};
  };
  auto gear = [](const char* key, const char* label, const char* label_es, const char* chip, const char* prompt,
                 const char* prompt_es) { return Binding{key, label, label_es, prompt, prompt_es, Kind::kButton, chip, {}}; };
  return {
      {"STEER", "Steering", "Dirección", "Turn the wheel to the LEFT, at least a quarter turn, then back to centre.",
       "Gira el volante a la IZQUIERDA, al menos un cuarto de vuelta, y vuelve al centro.", Kind::kSteering, "", {}},
      {"THROTTLE", "Throttle", "Acelerador", "Press the THROTTLE pedal all the way down, then release it.",
       "Pisa el ACELERADOR a fondo y suéltalo.", Kind::kPedal, "", {}},
      {"BRAKE", "Brake", "Freno", "Press the BRAKE pedal all the way down, then release it.",
       "Pisa el FRENO a fondo y suéltalo.", Kind::kPedal, "", {}},
      {"CLUTCH", "Clutch", "Embrague",
       "Press the CLUTCH pedal all the way down, then release it. It presses the clutch button set under Steering and pedals; choose Manual with clutch in the game.",
       "Pisa el EMBRAGUE a fondo y suéltalo. Pulsa el botón de embrague elegido en Dirección y pedales; elige Manual con embrague en el juego.",
       Kind::kPedal, "", {}},
      button("RB", "RB (shift up)", "RB (subir marcha)", "RB"),
      button("LB", "LB (shift down)", "LB (bajar marcha)", "LB"),
      button("A", "A", "A", "A"),
      button("B", "B", "B", "B"),
      button("X", "X", "X", "X"),
      button("Y", "Y", "Y", "Y"),
      button("BACK", "Back (view)", "Back (vista)", "Back"),
      button("START", "Start (pause)", "Start (pausa)", "Start"),
      button("LS", "LS (left stick click)", "LS (clic stick izq.)", "LS"),
      button("RS", "RS (right stick click)", "RS (clic stick der.)", "RS"),
      button("UP", "D-pad up", "Cruceta arriba", "\xE2\x86\x91"),
      button("DOWN", "D-pad down", "Cruceta abajo", "\xE2\x86\x93"),
      button("LEFT", "D-pad left", "Cruceta izquierda", "\xE2\x86\x90"),
      button("RIGHT", "D-pad right", "Cruceta derecha", "\xE2\x86\x92"),
      // An H-pattern shifter, last so a wheel without one skips them all at 1st gear.
      gear("GEAR1", "Shifter 1st", "Palanca 1.\xC2\xAA", "1",
           "Put the H-shifter in 1st gear. No H-shifter? Click Skip to finish the setup.",
           "Pon la palanca en 1.\xC2\xAA. ¿Sin palanca en H? Pulsa Omitir para terminar."),
      gear("GEAR2", "Shifter 2nd", "Palanca 2.\xC2\xAA", "2", "Put the H-shifter in 2nd gear.", "Pon la palanca en 2.\xC2\xAA."),
      gear("GEAR3", "Shifter 3rd", "Palanca 3.\xC2\xAA", "3", "Put the H-shifter in 3rd gear.", "Pon la palanca en 3.\xC2\xAA."),
      gear("GEAR4", "Shifter 4th", "Palanca 4.\xC2\xAA", "4", "Put the H-shifter in 4th gear.", "Pon la palanca en 4.\xC2\xAA."),
      gear("GEAR5", "Shifter 5th", "Palanca 5.\xC2\xAA", "5", "Put the H-shifter in 5th gear.", "Pon la palanca en 5.\xC2\xAA."),
      gear("GEAR6", "Shifter 6th", "Palanca 6.\xC2\xAA", "6", "Put the H-shifter in 6th gear.", "Pon la palanca en 6.\xC2\xAA."),
      gear("GEAR7", "Shifter 7th", "Palanca 7.\xC2\xAA", "7", "Put the H-shifter in 7th gear, or click Skip if it has no 7th.",
           "Pon la palanca en 7.\xC2\xAA, o pulsa Omitir si no tiene."),
      gear("REVERSE", "Shifter reverse", "Palanca marcha atrás", "R", "Put the H-shifter in reverse.", "Pon la palanca en marcha atrás."),
      // The game's camera adjustments (src/camera_view.cpp), last: not in the
      // live view's chips or the guided setup.
      camera("CAM_FOV_IN", "Field of view: narrower", "Campo de visión: más estrecho"),
      camera("CAM_FOV_OUT", "Field of view: wider", "Campo de visión: más amplio"),
      camera("CAM_FOV_RESET", "Field of view: default", "Campo de visión: original"),
      camera("CAM_SEAT_UP", "Seat up", "Asiento arriba"),
      camera("CAM_SEAT_DOWN", "Seat down", "Asiento abajo"),
      camera("CAM_SEAT_FORWARD", "Seat forward", "Asiento adelante"),
      camera("CAM_SEAT_BACK", "Seat back", "Asiento atrás"),
      camera("CAM_SEAT_RESET", "Seat: default", "Asiento: original"),
  };
}

bool IsCamera(const Binding& row) { return std::strncmp(row.key, "CAM_", 4) == 0; }

// Listens for the input a step asks for: the axis that moves furthest
// (steering, pedals), or the first button, hat direction or lever axis pressed
// after the step began.
struct Capture {
  int row = -1;
  bool guided = false, adding = false;
  struct Base { std::vector<int16_t> axes; std::vector<bool> buttons; std::vector<uint8_t> hats; };
  std::map<uint32_t, Base> base;
  bool found = false, user_invert = false;
  Input candidate;
  uint32_t candidate_id = 0;
  int16_t rest = 0, peak = 0;
  Clock::time_point released_at{};
};

struct State {
  bool loaded = false;
  std::vector<Binding> rows = DefaultRows();
  Capture capture;
  std::string info, error;
  std::map<uint32_t, std::pair<std::vector<int16_t>, Clock::time_point>> activity;
  char target[64] = {};
  bool open_bindings = true, open_devices = false, open_ffb = false, open_steering = false, open_telemetry = false;
};

Binding& Get(State& s, const char* key) {
  for (auto& r : s.rows)
    if (std::strcmp(r.key, key) == 0) return r;
  return s.rows.front();
}

const w::Device* Resolve(const std::vector<w::Device>& devices, const std::string& match) {
  return match.empty() ? nullptr : w::FindDevice(devices, match);
}

std::string DeviceName(const std::vector<w::Device>& devices, const std::string& match) {
  if (match.empty()) return L("Wheel base", "Base del volante");
  if (const auto* d = Resolve(devices, match)) return d->name;
  return match + L(" (not connected)", " (no conectado)");
}

std::string Describe(const Input& i) {
  static const char* hats[] = {"", "up", "right", "", "down", "", "", "", "left"};
  switch (i.type) {
    case InputType::kAxis: return "axis " + std::to_string(i.index);
    case InputType::kButton: return "button " + std::to_string(i.index);
    default: return "hat " + std::to_string(i.index) + " " + (i.hat < 9 ? hats[i.hat] : "?");
  }
}

// Reads the wheel_* settings, with inputs that name no device on the wheel base.
void Load(State& s, const w::Settings& c) {
  s.rows = DefaultRows();
  if (!c.device.empty())
    Get(s, "STEER").inputs.push_back({c.device, InputType::kAxis, std::clamp(c.steering_axis, 0, 63), 0, c.steering_invert});
  auto split = [&](std::string_view text, std::string& device, std::string_view& rest) {
    const size_t bar = text.rfind('|');
    device = bar == std::string_view::npos ? c.device : std::string(text.substr(0, bar));
    rest = bar == std::string_view::npos ? text : text.substr(bar + 1);
  };
  const std::pair<const char*, const std::string*> pedals[] = {{"THROTTLE", &c.throttle}, {"BRAKE", &c.brake}, {"CLUTCH", &c.clutch}};
  for (const auto& [key, text] : pedals) {
    if (auto b = w::ParseAxisBinding(*text)) {
      Input i{b->device.empty() ? c.device : b->device, InputType::kAxis, b->axis, 0, b->direction == w::AxisDirection::kInverted};
      Get(s, key).inputs.push_back(i);
    }
  }
  // wheel_buttons, entry by entry, keeping each input's device.
  std::string target;
  size_t start = 0;
  const std::string& text = c.buttons;
  while (start <= text.size()) {
    size_t comma = text.find(',', start);
    if (comma == std::string::npos) comma = text.size();
    std::string part = text.substr(start, comma - start);
    start = comma + 1;
    const size_t eq = part.find('=');
    if (eq != std::string::npos) {
      target = part.substr(0, eq);
      for (auto& ch : target) ch = char(std::toupper(static_cast<unsigned char>(ch)));
      part = part.substr(eq + 1);
    }
    auto parsed = w::ParseButtonBindings("A=" + part);
    if (parsed.empty() || target.empty()) continue;
    Binding* row = nullptr;
    for (auto& r : s.rows)
      if (r.kind == Kind::kButton && target == r.key) row = &r;
    if (!row) continue;
    const auto& b = parsed.front();
    Input i;
    i.device = b.device.empty() ? c.device : b.device;
    if (b.axis >= 0) { i.type = InputType::kAxis; i.index = b.axis; i.inverted = b.axis_direction == w::AxisDirection::kInverted; }
    else if (b.hat >= 0) { i.type = InputType::kHat; i.index = b.hat; i.hat = b.hat_mask; }
    else { i.type = InputType::kButton; i.index = b.button; }
    if (std::none_of(row->inputs.begin(), row->inputs.end(), [&](const Input& o) { return o.Same(i); }))
      row->inputs.push_back(i);
  }
}

// Writes the bindings back into the wheel_* settings.
void Store(State& s, w::Settings& c) {
  const Binding& steer = Get(s, "STEER");
  c.device = steer.inputs.empty() ? std::string() : steer.inputs[0].device;
  if (!steer.inputs.empty()) {
    c.steering_axis = steer.inputs[0].index;
    c.steering_invert = steer.inputs[0].inverted;
  }
  // Inputs on the wheel base (or with no device, such as the default hat) name none.
  auto prefix = [&](const Input& i) { return i.device.empty() || i.device == c.device ? std::string() : i.device + "|"; };
  auto axis = [&](const Binding& r) {
    if (r.inputs.empty()) return std::string();
    const Input& i = r.inputs[0];
    return prefix(i) + (i.inverted ? "-" : "+") + "axis" + std::to_string(i.index);
  };
  c.throttle = axis(Get(s, "THROTTLE"));
  c.brake = axis(Get(s, "BRAKE"));
  c.clutch = axis(Get(s, "CLUTCH"));
  static const char* hats[] = {"", "up", "right", "", "down", "", "", "", "left"};
  std::string buttons;
  for (const auto& r : s.rows) {
    if (r.kind != Kind::kButton) continue;
    for (const auto& i : r.inputs) {
      if (!buttons.empty()) buttons += ",";
      buttons += std::string(r.key) + "=" + prefix(i);
      if (i.type == InputType::kAxis) buttons += std::string(i.inverted ? "-" : "+") + "axis" + std::to_string(i.index);
      else if (i.type == InputType::kHat) buttons += "h" + std::to_string(i.index) + "." + (i.hat < 9 ? hats[i.hat] : "up");
      else buttons += std::to_string(i.index);
    }
  }
  c.buttons = buttons;
  c.enabled = !c.device.empty();
}

// What the game reads from `input` for `row`.
void Evaluate(const State&, const w::Settings& c, const Binding& row, const Input& input, const w::Device& d, float& value,
              bool& pressed, std::string& text) {
  value = 0; pressed = false; text.clear();
  char buffer[32];
  if (input.type == InputType::kAxis && input.index < int(d.axes.size())) {
    const int16_t raw = d.axes[input.index];
    if (row.kind == Kind::kSteering) {
      value = w::SteeringFromAxis(raw, input.inverted, w::SteeringScale(c), c.steering_deadzone) / 32767.0f;
      std::snprintf(buffer, sizeof(buffer), "%+.0f %%", value * 100);
      text = buffer;
    } else {
      int bottom = 0, top = 0;
      if (row.kind == Kind::kPedal) {
        const int which = std::strcmp(row.key, "THROTTLE") == 0 ? 0 : std::strcmp(row.key, "BRAKE") == 0 ? 1 : 2;
        bottom = c.deadzone_bottom[which] < 0 ? c.pedal_deadzone : c.deadzone_bottom[which];
        top = c.deadzone_top[which];
      }
      const uint8_t p = w::PedalFromAxis(raw, input.inverted ? w::AxisDirection::kInverted : w::AxisDirection::kNormal, bottom, top);
      value = p / 255.0f;
      pressed = p >= 128;
      if (row.kind == Kind::kPedal) { std::snprintf(buffer, sizeof(buffer), "%.0f %%", value * 100); text = buffer; }
      else if (pressed) text = L("pressed", "pulsado");
    }
  } else if (input.type == InputType::kButton && input.index < int(d.buttons.size())) {
    pressed = d.buttons[input.index];
  } else if (input.type == InputType::kHat && input.index < int(d.hats.size())) {
    pressed = (d.hats[input.index] & input.hat) != 0;
  }
  if (row.kind == Kind::kButton) { value = pressed ? 1.0f : 0.0f; if (pressed) text = L("pressed", "pulsado"); }
}

void UpdateLive(State& s, const w::Settings& c, const std::vector<w::Device>& devices) {
  for (auto& r : s.rows) {
    r.value = 0; r.pressed = false; r.text.clear();
    for (const auto& i : r.inputs) {
      const auto* d = Resolve(devices, i.device);
      if (!d) continue;
      float v; bool p; std::string t;
      Evaluate(s, c, r, i, *d, v, p, t);
      if (&i == &r.inputs.front() || p) { r.value = v; r.pressed = p; r.text = t; }
      if (p) break;
    }
  }
}

// An axis already used by steering or a pedal is not offered again.
bool AxisTaken(const State& s, int row, const std::string& device, int axis) {
  for (size_t i = 0; i < s.rows.size(); ++i) {
    const auto& r = s.rows[i];
    if (int(i) == row || r.kind == Kind::kButton || r.inputs.empty()) continue;
    if (r.inputs[0].type == InputType::kAxis && r.inputs[0].index == axis && r.inputs[0].device == device) return true;
  }
  return false;
}

void BeginCapture(State& s, int row, bool guided, const std::vector<w::Device>& devices) {
  Capture c;
  c.row = row;
  c.guided = guided;
  for (const auto& d : devices) c.base[d.id] = {d.axes, d.buttons, d.hats};
  s.capture = std::move(c);
  s.error.clear();
}

void TrackCapture(State& s, const std::vector<w::Device>& devices) {
  Capture& c = s.capture;
  if (c.row < 0) return;
  const Binding& row = s.rows[c.row];
  const bool steering = row.kind == Kind::kSteering;
  auto direction = [&] { return steering ? c.peak > c.rest : c.peak < c.rest; };
  if (c.found) {
    // Follow the detected axis to its full travel; the direction comes from that.
    if (c.candidate.type != InputType::kAxis) return;
    for (const auto& d : devices) {
      if (d.id != c.candidate_id || c.candidate.index >= int(d.axes.size())) continue;
      const int16_t value = d.axes[c.candidate.index];
      if (std::abs(value - c.rest) > std::abs(c.peak - c.rest)) c.peak = value;
      if (!c.user_invert) c.candidate.inverted = direction();
    }
    return;
  }
  if (row.kind != Kind::kButton) {
    const int threshold = steering ? 10000 : 16000;
    int best = 0, best_axis = -1;
    const w::Device* best_device = nullptr;
    for (const auto& d : devices) {
      auto it = c.base.find(d.id);
      if (it == c.base.end()) { c.base[d.id] = {d.axes, d.buttons, d.hats}; continue; }
      const std::string match = w::DeviceMatch(d, devices);
      for (size_t i = 0; i < d.axes.size() && i < it->second.axes.size(); ++i) {
        if (AxisTaken(s, c.row, match, int(i))) continue;
        const int delta = d.axes[i] - it->second.axes[i];
        if (std::abs(delta) > std::abs(best)) { best = delta; best_device = &d; best_axis = int(i); }
      }
    }
    if (!best_device || std::abs(best) < threshold) return;
    c.found = true;
    c.candidate_id = best_device->id;
    c.rest = c.base[best_device->id].axes[best_axis];
    c.peak = best_device->axes[best_axis];
    c.candidate = {w::DeviceMatch(*best_device, devices), InputType::kAxis, best_axis, 0, direction()};
    return;
  }
  for (const auto& d : devices) {
    auto it = c.base.find(d.id);
    if (it == c.base.end()) { c.base[d.id] = {d.axes, d.buttons, d.hats}; continue; }
    auto& b = it->second;
    // A button or hat held when the step began (an H-shifter left in gear)
    // counts again once it is let go, so moving out of the gate and back binds it.
    for (size_t i = 0; i < d.buttons.size() && i < b.buttons.size(); ++i) if (!d.buttons[i]) b.buttons[i] = false;
    for (size_t i = 0; i < d.hats.size() && i < b.hats.size(); ++i) b.hats[i] &= d.hats[i];
    const std::string match = w::DeviceMatch(d, devices);
    for (size_t i = 0; i < d.buttons.size() && i < b.buttons.size(); ++i) {
      if (d.buttons[i] && !b.buttons[i]) {
        c.found = true; c.candidate_id = d.id; c.candidate = {match, InputType::kButton, int(i), 0, false};
        return;
      }
    }
    for (size_t i = 0; i < d.hats.size() && i < b.hats.size(); ++i) {
      const uint8_t pressed = d.hats[i] & ~b.hats[i];
      if (!pressed) continue;
      uint8_t bit = 1;
      while (!(pressed & bit)) bit <<= 1;
      c.found = true; c.candidate_id = d.id; c.candidate = {match, InputType::kHat, int(i), bit, false};
      return;
    }
    for (size_t i = 0; i < d.axes.size() && i < b.axes.size(); ++i) {
      if (AxisTaken(s, c.row, match, int(i))) continue;
      const int delta = d.axes[i] - b.axes[i];
      if (std::abs(delta) >= 24000) {
        c.found = true; c.candidate_id = d.id; c.rest = b.axes[i];
        c.candidate = {match, InputType::kAxis, int(i), 0, delta < 0};
        return;
      }
    }
  }
}

// The detected input is back at rest: a button or hat let go, an axis returned.
bool CandidateReleased(const State& s, const std::vector<w::Device>& devices) {
  const Capture& c = s.capture;
  for (const auto& d : devices) {
    if (d.id != c.candidate_id) continue;
    switch (c.candidate.type) {
      case InputType::kButton: return c.candidate.index < int(d.buttons.size()) && !d.buttons[c.candidate.index];
      case InputType::kHat: return c.candidate.index < int(d.hats.size()) && !(d.hats[c.candidate.index] & c.candidate.hat);
      default: return c.candidate.index < int(d.axes.size()) && std::abs(d.axes[c.candidate.index] - c.rest) < 3000;
    }
  }
  return false;
}

bool RestsMidRange(const State& s) {
  const Capture& c = s.capture;
  return c.found && c.candidate.type == InputType::kAxis && s.rows[c.row].kind != Kind::kSteering && std::abs(int(c.rest)) < 12000;
}

}  // namespace wheel_ui

// --- App state -------------------------------------------------------------

struct App {
  SDL_Window* window = nullptr;
  SDL_GPUDevice* gpu = nullptr;
  fs::path exe_dir;
  fs::path user_config;      // fm4.user.toml: what the game reads
  fs::path port_config;      // fm4.toml: the port's own settings
  fs::path launcher_config;  // launcher.toml: UI language and profiles
  fs::path manifest_path;
  fs::path game_exe;
  fs::path tool_exe;
  fs::path cover_path;
  Cover cover;
  bool reload_cover = false;

  Settings settings;
  LauncherState state;
  std::vector<ManifestEntry> manifest;
  Tab tab = Tab::kProfiles;
  std::string notice;

  // Install / verify state
  enum class GameState { kUnknown, kMissing, kIncomplete, kUnverified, kVerified };
  GameState game_state = GameState::kUnknown;
  std::string game_state_detail;
  size_t content_packages = 0;
  TaskProgress task;
  std::thread worker;
  std::string task_title;
  enum class TaskKind { kNone, kPlayDisc, kContentDisc, kVerify, kForzaPlus, kRemoveForzaPlus };
  TaskKind task_kind = TaskKind::kNone;
  std::atomic<int> import_step{0};  // play disc: 1 = extracting, 2 = verifying

  // Async file dialogs report here (SDL may call back on another thread).
  std::mutex dialog_mutex;
  std::optional<std::string> picked_iso;
  std::optional<std::string> picked_game_folder;
  std::optional<std::string> picked_user_folder;
  std::optional<std::string> picked_plus_zip, picked_plus_update;
  std::string plus_zip, plus_update;
  int plus_mode = 0;
  std::unique_ptr<fm4::wheel::DeviceMonitor> wheel_monitor;
  wheel_ui::State wheel_ui;
  // Profiles
  char new_profile[32] = {};
  int delete_candidate = -1;
  bool open_delete_popup = false;

  // Key capture: index into settings.binds, and whether it appends.
  int capture_index = -1;
  bool capture_append = false;

  // Running game
  SDL_Process* game = nullptr;
  fs::file_time_type launched_at;
  std::string last_exit;

  bool Installed() const {
    return game_state == GameState::kVerified || game_state == GameState::kUnverified;
  }

  bool ContentInstalled() const {
    return content_packages >= kContentPackages;
  }

  fs::path GameDir() const {
    return settings.game_data_root.empty() ? exe_dir / "game" : fs::u8path(settings.game_data_root);
  }
  fs::path UserDir() const {
    return settings.user_data_root.empty() ? exe_dir / "user" : fs::u8path(settings.user_data_root);
  }
  bool PlusActive() const { return GameDir().filename() == ".fm4-plus"; }
  fs::path BaseGameDir() const { return PlusActive() ? GameDir().parent_path() : GameDir(); }
  fs::path BaseUserDir() const { return PlusActive() ? UserDir().parent_path() : UserDir(); }
  bool PlusInstalled() const { return fs::exists(BaseGameDir()/".fm4-plus/.fm4-plus-ready"); }
  void SelectPlus(bool enabled) {
    const auto game = BaseGameDir(), user = BaseUserDir();
    settings.game_data_root = (enabled ? game / ".fm4-plus" : game).u8string();
    settings.user_data_root = (enabled ? user / ".fm4-plus" : user).u8string();
    Save();
    CheckGame();
  }

  const Profile* ActiveProfile() const {
    for (const auto& p : state.profiles) {
      if (p.xuid == state.active_xuid) return &p;
    }
    return nullptr;
  }

  void Save() {
    settings.game_data_root = GameDir().u8string();
    settings.user_data_root = UserDir().u8string();
    if (const Profile* p = ActiveProfile()) {
      settings.profile_name = p->name;
      settings.profile_xuid = p->xuid;
    }
    if (!settings.Save(user_config)) {
      notice = std::string(L("Could not save ", "No se pudo guardar ")) + user_config.u8string();
    }
    state.Save(launcher_config);
  }

  void Load() {
    if (!state.Load(launcher_config)) {
      // First run: follow the system language (launcher UI and game).
      int count = 0;
      SDL_Locale** locales = SDL_GetPreferredLocales(&count);
      state.spanish = count > 0 && locales[0]->language && std::strcmp(locales[0]->language, "es") == 0;
      SDL_free(locales);
    }
    g_spanish = state.spanish;
    // First run of this launcher: start from the older files' values.
    if (!settings.Load(user_config)) {
      if (state.spanish) settings.user_language = 5;
      settings.Load(port_config);
      // Development builds discover the extracted discs in this workspace.
      for (auto root = exe_dir.parent_path(); !root.empty(); root = root.parent_path()) {
        if (fs::exists(root / "fm4_manifest.toml") && fs::exists(root / "game/disc1/default.xex")) {
          if (settings.game_data_root.empty()) settings.game_data_root = (root / "game/disc1").u8string();
          if (settings.user_data_root.empty()) settings.user_data_root = (root / "user").u8string();
          break;
        }
        if (root == root.parent_path()) break;
      }
      if (!state.legacy_game_dir.empty()) settings.game_data_root = state.legacy_game_dir;
      if (!state.legacy_user_dir.empty()) settings.user_data_root = state.legacy_user_dir;
    }
    if (state.profiles.empty()) {
      state.profiles.push_back({IsValidGamertag(settings.profile_name) ? settings.profile_name : "Player",
                                settings.profile_xuid.size() == 16 ? settings.profile_xuid : GenerateXuid()});
    }
    if (!ActiveProfile()) state.active_xuid = state.profiles.front().xuid;
    Save();
  }

  // FM4 always verifies the original Disc 1 manifest.
  std::vector<ManifestEntry> VerifyManifest(const fs::path& dir) const {
    auto out = manifest;
    if (fs::exists(dir / ".fm4-plus-ready")) {
      for (const auto& mod : LoadManifest(dir / ".fm4-plus.sha256")) {
        // The mod never supplies executable code or the port's render scenarios.
        if (mod.path.rfind("Media/", 0) != 0 || mod.path.find("RenderScenarios/") != std::string::npos) continue;
        auto entry = std::find_if(out.begin(), out.end(), [&](const ManifestEntry& e) {
          return SDL_strcasecmp(e.path.c_str(), mod.path.c_str()) == 0;
        });
        if (entry == out.end()) out.push_back(mod); else *entry = mod;
      }
    }
    return PhysicsVerificationManifest(dir, ShadowVerificationManifest(dir, out));
  }

  void CheckGame() {
    const fs::path dir = GameDir();
    std::error_code ec;
    content_packages = CountContentPackages(UserDir());
    if (!fs::exists(dir / "default.xex", ec)) {
      game_state = GameState::kMissing;
      game_state_detail = L("Install Disc 1 in the Install tab.", "Instala el Disco 1 en la pestaña Instalación.");
      return;
    }
    TaskProgress quick;
    const VerifyReport report = VerifyGame(dir, VerifyManifest(dir), false, quick);
    if (!report.ok()) {
      game_state = GameState::kIncomplete;
      game_state_detail = std::to_string(report.missing.size()) + L(" files missing, ", " archivos faltan, ") +
                          std::to_string(report.wrong_size.size()) +
                          L(" with a different size. Install Disc 1 again.",
                            " con otro tamaño. Vuelve a instalar el Disco 1.");
    } else if (fs::exists(VerifiedMarker(dir), ec)) {
      game_state = GameState::kVerified;
      game_state_detail = dir.u8string();
    } else {
      game_state = GameState::kUnverified;
      game_state_detail = L("Files present, contents not verified yet.",
                            "Archivos presentes, sin verificar el contenido.");
    }
    if (!fs::exists(cover_path, ec) && ExtractCover(dir, tool_exe, cover_path)) reload_cover = true;
  }

  bool TaskRunning() const { return task.state == TaskProgress::State::kRunning; }

  void StartTask(std::string title, TaskKind kind, std::function<bool()> body) {
    if (worker.joinable()) worker.join();
    task_title = std::move(title);
    task_kind = kind;
    task.cancel = false;
    task.done = 0;
    task.total = 0;
    task.SetStatus("");
    task.state = TaskProgress::State::kRunning;
    worker = std::thread([this, body = std::move(body)] {
      const bool ok = body();
      task.state = ok ? TaskProgress::State::kDone
                      : (task.cancel ? TaskProgress::State::kCancelled : TaskProgress::State::kFailed);
    });
  }

  // Full hash check; writes the verified marker on success.
  bool RunVerify(const fs::path& dir) {
    const VerifyReport report = VerifyGame(dir, VerifyManifest(dir), true, task);
    if (fs::exists(dir / ".fm4-plus-ready")) {
      const auto dlc = LoadManifest(UserDir() / ".fm4-plus.sha256");
      if (!fs::exists(UserDir() / ".fm4-plus.sha256") || !VerifyGame(UserDir(), dlc, true, task).ok()) {
        task.SetStatus(L("Forza Plus content verification failed.", "Fallo al verificar el contenido de Forza Plus."));
        return false;
      }
    }
    std::error_code ec;
    if (report.ok() && !task.cancel) {
      const auto fix_error = SetRaceShadowWorkaround(dir, true);
      if (!fix_error.empty()) {
        task.SetStatus(fix_error);
        return false;
      }
      std::ofstream(VerifiedMarker(dir)) << "verified against game_files.sha256\n";
      if (fs::exists(dir / ".fm4-plus-ready")) {
        task.SetStatus(L("Forza Plus files and installed content verified.", "Archivos de Forza Plus y contenido instalado verificados."));
        return true;
      }
      task.SetStatus(std::string(L("All good: ", "Todo correcto: ")) + std::to_string(manifest.size()) +
                     L(" files match the version the port was built from.",
                       " archivos coinciden con la versión del port."));
      return true;
    }
    fs::remove(VerifiedMarker(dir), ec);
    std::string text = std::string(L("Verification failed: ", "La verificación falló: ")) +
                       std::to_string(report.missing.size()) + L(" missing, ", " faltan, ") +
                       std::to_string(report.wrong_size.size()) + L(" wrong size, ", " con otro tamaño, ") +
                       std::to_string(report.wrong_hash.size()) + L(" wrong contents.", " con contenido distinto.");
    const auto& first = !report.missing.empty()      ? report.missing
                        : !report.wrong_size.empty() ? report.wrong_size
                                                     : report.wrong_hash;
    if (!first.empty()) text += std::string(L("\nFirst: ", "\nPrimero: ")) + first.front();
    if (std::find(report.wrong_hash.begin(), report.wrong_hash.end(), "default.xex") != report.wrong_hash.end()) {
      text += L("\ndefault.xex does not match: the port was built from the USA Rev 1 disc.",
                "\ndefault.xex no coincide: el port se hizo con el disco USA Rev 1.");
    }
    task.SetStatus(text);
    return false;
  }

  // Starts the right install task for the disc image the player picked.
  void StartImport(const fs::path& iso) {
    Save();
    const DiscKind kind = IdentifyIso(iso);
    if (kind == DiscKind::kPlay) {
      const fs::path game_dir = GameDir();
      StartTask(L("Disc 1 (Play Disc)", "Disco 1 (Play Disc)"), TaskKind::kPlayDisc, [this, iso, game_dir] {
        import_step = 1;
        std::error_code ec;
        fs::remove(VerifiedMarker(game_dir), ec);
        if (!ExtractPlayDisc(iso, game_dir, task) || task.cancel) return false;
        import_step = 2;
        return RunVerify(game_dir);
      });
    } else if (kind == DiscKind::kContent) {
      const fs::path game_dir = GameDir(), user_dir = UserDir();
      StartTask(L("Disc 2 (Content Install Disc)", "Disco 2 (Content Install Disc)"), TaskKind::kContentDisc,
                [this, iso, game_dir, user_dir] {
                  return InstallContentDisc(iso, game_dir, user_dir, tool_exe, task);
                });
    } else {
      notice = std::string(L("That image is not a Forza Motorsport 4 USA Rev 1 disc: ",
                             "Esa imagen no es un disco de Forza Motorsport 4 USA Rev 1: ")) +
               iso.filename().u8string();
    }
  }

  void LaunchGame() {
    if (!fs::exists(game_exe)) {
      notice = std::string(L("Not found: ", "No se encuentra ")) + game_exe.u8string();
      return;
    }
    const auto physics_error = SetPlus120Physics(GameDir(), settings.experimental_120fps);
    if (!physics_error.empty()) {
      notice = physics_error;
      return;
    }
    // Compatibility fixes are part of the port, not player-facing settings.
    const auto fix_error = SetRaceShadowWorkaround(GameDir(), true);
    if (!fix_error.empty()) {
      notice = std::string(L("Could not prepare the game: ", "No se pudo preparar el juego: ")) + fix_error;
      return;
    }
    Save();
    // DirectInput wheel handles are exclusive. Release the preview before
    // starting the child, including any diagnostic haptic effect.
    wheel_monitor.reset();
    wheel_ui.capture = {};
    const std::string exe = game_exe.u8string();
    const std::string cwd = exe_dir.u8string();
    const char* args[] = {exe.c_str(), nullptr};
    // A WIN32-subsystem launcher has no console handles, so the default
    // (inherit stdio) fails in DuplicateHandle; the game logs to files anyway.
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char**>(args));
    SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, cwd.c_str());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    game = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!game) {
      notice = std::string(L("Could not start the game: ", "No se pudo lanzar el juego: ")) + SDL_GetError();
    } else {
      last_exit.clear();
      launched_at = fs::file_time_type::clock::now();
      SDL_MinimizeWindow(window);
    }
  }

  // Logs exist to explain failures: after a clean exit, remove the ones this
  // session wrote (fm4_NNN.log and its rotations). Crash reports stay.
  void DeleteSessionLogs() {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(exe_dir / "logs", ec)) {
      if (e.is_regular_file() && e.path().extension() == ".log" &&
          e.path().filename().u8string().rfind("fm4_", 0) == 0 &&
          e.last_write_time(ec) >= launched_at - std::chrono::seconds(2)) {
        fs::remove(e.path(), ec);
      }
    }
  }

  void PollGame() {
    if (!game) return;
    int exit_code = 0;
    if (SDL_WaitProcess(game, false, &exit_code)) {
      SDL_DestroyProcess(game);
      game = nullptr;
      SDL_RestoreWindow(window);
      if (exit_code != 0) {
        char hex[16];
        std::snprintf(hex, sizeof(hex), "0x%08X", unsigned(exit_code));
        last_exit = std::string(L("The game closed with an error (", "El juego se cerró con un error (")) + hex +
                    L("). The log and crash report are in the logs folder.",
                      "). El registro y el informe de fallo están en la carpeta logs.");
      } else if (!settings.wheel.enabled && !settings.wheel.telemetry_udp) {
        // Wheel and telemetry sessions keep their log: it lists the devices,
        // force feedback and the telemetry values read.
        DeleteSessionLogs();
      }
    }
  }
};

// --- Widgets ---------------------------------------------------------------

void TextDim(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, kDim);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

void TextColored(const ImVec4& color, const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

void Title(const char* text, float size = 1.0f) {
  ImGui::PushFont(g_bold, ImGui::GetStyle().FontSizeBase * size);
  ImGui::TextUnformatted(text);
  ImGui::PopFont();
}

// A rounded panel with a title; rows go inside a two-column table (label on
// the left, control on the right).
bool BeginCard(const char* id, const char* title, const char* subtitle = nullptr) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, kCard);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(12));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20), S(16)));
  ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
  Title(title, 1.15f);
  if (subtitle) TextDim(subtitle);
  ImGui::Dummy(ImVec2(0, S(4)));
  return true;
}

void EndCard() {
  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
  ImGui::Dummy(ImVec2(0, S(10)));
}

bool BeginRows(const char* id) {
  if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_None)) return false;
  // Fixed label column, so hints wrap under their label instead of squeezing it.
  ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, S(250));
  ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch);
  return true;
}

// Starts a settings row: label (+ optional hint) left, control column right.
void Row(const char* label, const char* hint = nullptr) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  if (hint) {
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - S(16));
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.88f);
    ImGui::TextUnformatted(hint);
    ImGui::PopFont();
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
  }
  ImGui::TableSetColumnIndex(1);
  ImGui::SetNextItemWidth(-FLT_MIN);
}

void EndRows() { ImGui::EndTable(); }

// iOS-style switch.
bool Toggle(const char* id, bool* value) {
  const float h = ImGui::GetFrameHeight() * 0.9f;
  const float w = h * 1.85f;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - w));
  ImVec2 p = ImGui::GetCursorScreenPos();
  p.y += (ImGui::GetFrameHeight() - h) * 0.5f;
  ImGui::InvisibleButton(id, ImVec2(w, ImGui::GetFrameHeight()));
  const bool clicked = ImGui::IsItemClicked();
  if (clicked) *value = !*value;
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec4 track = *value ? (hovered ? kAccentHover : kAccent)
                              : (hovered ? ImVec4(0.26f, 0.27f, 0.30f, 1) : ImVec4(0.21f, 0.22f, 0.25f, 1));
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Col(track), h * 0.5f);
  const float r = h * 0.5f - S(3);
  const float cx = *value ? p.x + w - h * 0.5f : p.x + h * 0.5f;
  dl->AddCircleFilled(ImVec2(cx, p.y + h * 0.5f), r, IM_COL32(255, 255, 255, 255));
  return clicked;
}

// Row of buttons, one selected. Fills the available width.
template <typename T>
bool Segmented(const char* id, T& value, const std::vector<std::pair<T, const char*>>& items) {
  ImGui::PushID(id);
  bool changed = false;
  const float full = ImGui::GetContentRegionAvail().x;
  const float gap = S(4);
  const float each = (full - gap * float(items.size() - 1)) / float(items.size());
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(7));
  for (size_t i = 0; i < items.size(); ++i) {
    const bool active = items[i].first == value;
    ImGui::PushStyleColor(ImGuiCol_Button, active ? kAccent : kControl);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? kAccentHover : ImVec4(0.22f, 0.23f, 0.26f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccent);
    if (i) ImGui::SameLine();
    ImGui::PushID(int(i));
    if (ImGui::Button(items[i].second, ImVec2(each, 0)) && !active) {
      value = items[i].first;
      changed = true;
    }
    ImGui::PopID();
    ImGui::PopStyleColor(3);
  }
  ImGui::PopStyleVar(2);
  ImGui::PopID();
  return changed;
}

template <typename T>
bool Combo(const char* id, T& value, const std::vector<std::pair<T, const char*>>& items) {
  const char* preview = "";
  for (const auto& [v, name] : items) {
    if (v == value) preview = name;
  }
  bool changed = false;
  if (ImGui::BeginCombo(id, preview)) {
    for (const auto& [v, name] : items) {
      if (ImGui::Selectable(name, v == value)) {
        value = v;
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

bool PrimaryButton(const char* label, const ImVec2& size) {
  ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccent);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
  const bool pressed = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
  return pressed;
}

// Small rounded label, e.g. a key or "Active".
void Chip(const char* text, const ImVec4& bg, const ImVec4& fg = kText) {
  const ImVec2 pad(S(8), S(3));
  const ImVec2 size = ImGui::CalcTextSize(text);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float y = p.y + (ImGui::GetFrameHeight() - size.y - pad.y * 2) * 0.5f;
  ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + size.x + pad.x * 2, y + size.y + pad.y * 2),
                                            Col(bg), S(6));
  ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + pad.x, y + pad.y), Col(fg), text);
  ImGui::Dummy(ImVec2(size.x + pad.x * 2, ImGui::GetFrameHeight()));
}

void StatusDot(const ImVec4& color) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float r = S(5);
  ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f + S(1)), r,
                                              Col(color));
  ImGui::Dummy(ImVec2(r * 2 + S(6), ImGui::GetTextLineHeight()));
  ImGui::SameLine(0, 0);
}

// --- Left column: cover, profile, play ------------------------------------

float CoverWidth() { return S(400); }
float CoverHeight(const App& app) {
  const float aspect = app.cover.texture ? float(app.cover.height) / float(app.cover.width) : 0.76f;
  return CoverWidth() * aspect;
}
float SettingsWidth() { return S(800); }
float WindowHeight() { return S(840); }

void CoverPanel(App& app, const ImVec2& size) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 p1(p0.x + size.x, p0.y + CoverHeight(app));

  // Column background, a darker shade of the card colour.
  dl->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), Col(ImVec4(0.085f, 0.09f, 0.10f, 1)));
  if (app.cover.texture) {
    dl->AddImage(app.cover.id(), p0, p1);
  } else {
    // No cover yet (Disc 1 not installed): a panel in the logo's colours.
    dl->AddRectFilledMultiColor(p0, p1, IM_COL32(48, 50, 56, 255), IM_COL32(30, 31, 36, 255),
                                IM_COL32(14, 14, 16, 255), IM_COL32(24, 25, 29, 255));
    ImGui::PushFont(g_bold, ImGui::GetStyle().FontSizeBase * 2.6f);
    dl->AddText(ImVec2(p0.x + S(28), p0.y + S(70)), Col(kAccent), "FORZA");
    ImGui::PopFont();
    ImGui::PushFont(g_bold, ImGui::GetStyle().FontSizeBase * 1.4f);
    dl->AddText(ImVec2(p0.x + S(30), p0.y + S(130)), IM_COL32(235, 235, 240, 235), "MOTORSPORT 4");
    ImGui::PopFont();
  }

  // Controls under the cover.
  const float pad = S(22);
  const float inner_w = size.x - pad * 2;
  ImGui::SetCursorScreenPos(ImVec2(p0.x + pad, p1.y + S(18)));
  ImGui::BeginGroup();
  ImGui::PushStyleColor(ImGuiCol_Text, kDim);
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.85f);
  ImGui::TextUnformatted(L("PROFILE", "PERFIL"));
  ImGui::PopFont();
  ImGui::PopStyleColor();
  ImGui::SetNextItemWidth(inner_w);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, kControl);
  const Profile* active = app.ActiveProfile();
  if (ImGui::BeginCombo("##profile", active ? active->name.c_str() : "")) {
    for (const auto& p : app.state.profiles) {
      ImGui::PushID(p.xuid.c_str());
      if (ImGui::Selectable(p.name.c_str(), &p == active)) {
        app.state.active_xuid = p.xuid;
        app.Save();
      }
      ImGui::PopID();
    }
    ImGui::Separator();
    if (ImGui::Selectable(L("Manage profiles...", "Gestionar perfiles..."))) app.tab = Tab::kProfiles;
    ImGui::EndCombo();
  }
  ImGui::PopStyleColor();
  ImGui::Dummy(ImVec2(0, S(6)));

  StatusDot(app.Installed()?kOk:kBad);
  ImGui::TextUnformatted(app.Installed()?L("Game installed", "Juego instalado"):L("Game not installed", "Juego no instalado"));
  if(app.PlusInstalled()) {
    StatusDot(kOk);ImGui::TextUnformatted(L("Project Forza Plus 4 installed", "Project Forza Plus 4 instalado"));
  }
  if (app.Installed()) {
    const bool all = app.ContentInstalled();
    StatusDot(all ? kOk : kWarn);
      ImGui::Text(L("Disc 2 content: %zu of %zu packs", "Contenido del Disco 2: %zu de %zu packs"),
                  app.content_packages, kContentPackages);
  }
  ImGui::Dummy(ImVec2(0, S(6)));

  if (app.TaskRunning()) {
    // Any running task takes the play button's place, so its progress is
    // always on screen.
    const float fraction = app.task.total ? float(double(app.task.done) / double(app.task.total)) : 0.0f;
    const char* heading = app.task_kind == App::TaskKind::kRemoveForzaPlus ? L("UNINSTALLING FORZA PLUS", "DESINSTALANDO FORZA PLUS")
                          : app.task_kind == App::TaskKind::kPlayDisc      ? L("INSTALLING DISC 1", "INSTALANDO DISCO 1")
                          : app.task_kind == App::TaskKind::kContentDisc ? L("INSTALLING DISC 2", "INSTALANDO DISCO 2")
                          : app.task_kind == App::TaskKind::kForzaPlus ? L("INSTALLING FORZA PLUS", "INSTALANDO FORZA PLUS")
                                                                         : L("VERIFYING FILES", "VERIFICANDO ARCHIVOS");
    ImGui::PushFont(g_bold, ImGui::GetStyle().FontSizeBase * 1.05f);
    ImGui::Text("%s  %d%%", heading, int(fraction * 100));
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kAccent);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(6));
    ImGui::ProgressBar(fraction, ImVec2(inner_w, S(14)), "");
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (app.task_kind == App::TaskKind::kPlayDisc) {
      ImGui::TextUnformatted(app.import_step == 2 ? L("Step 2/2: verifying", "Paso 2/2: verificando")
                                                  : L("Step 1/2: extracting", "Paso 1/2: extrayendo"));
      ImGui::SameLine();
    }
    if (app.task.total) {
      ImGui::Text("%.2f / %.2f GB", double(app.task.done) / 1e9, double(app.task.total) / 1e9);
    }
    if (app.task_kind!=App::TaskKind::kRemoveForzaPlus && ImGui::SmallButton(L("Cancel", "Cancelar"))) app.task.cancel = true;
    ImGui::PushTextWrapPos(p0.x + size.x - pad);
    TextDim(app.task.Status().c_str());
    ImGui::PopTextWrapPos();
  } else {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(10));
    ImGui::PushFont(g_bold, ImGui::GetStyle().FontSizeBase * 1.45f);
    if (app.Installed()) {
      ImGui::BeginDisabled(app.game != nullptr);
      if (PrimaryButton(app.game ? L("PLAYING", "EN JUEGO") : L("PLAY", "JUGAR"), ImVec2(inner_w, S(62)))) {
        app.LaunchGame();
      }
      ImGui::EndDisabled();
    } else if (PrimaryButton(L("INSTALL", "INSTALAR"), ImVec2(inner_w, S(62)))) {
      app.tab = Tab::kInstall;
    }
    ImGui::PopFont();
    ImGui::PopStyleVar();
  }
  if (app.task.state == TaskProgress::State::kFailed) {
    ImGui::PushTextWrapPos(p0.x + size.x - pad);
    TextColored(kBad, app.task.Status().c_str());
    ImGui::PopTextWrapPos();
  }
  if (!app.last_exit.empty()) {
    ImGui::PushTextWrapPos(p0.x + size.x - pad);
    TextColored(kBad, app.last_exit.c_str());
    ImGui::PopTextWrapPos();
  }
  ImGui::EndGroup();
  ImGui::SetCursorScreenPos(p0);
  ImGui::Dummy(size);
}

// --- Tabs ------------------------------------------------------------------

void TabInstructions(App& app) {
  BeginCard("##quickstart",L("Getting started", "Primeros pasos"),
      L("Install your discs, choose a profile and play. Project Forza Plus 4 and wheel support are optional.",
        "Instala tus discos, elige un perfil y juega. Project Forza Plus 4 y el volante son opcionales."));
  ImGui::TextWrapped("%s",L("Keep the launcher, executables and DLLs together. A short folder such as C:\\Games\\FM4 helps avoid long paths. Requires a Direct3D 12 GPU and the Visual C++ 2015-2022 x64 runtime.",
      "Mantén el launcher, los ejecutables y las DLL juntos. Una carpeta corta como C:\\Games\\FM4 evita problemas de rutas largas. Requiere una GPU Direct3D 12 y Visual C++ 2015-2022 x64."));
  EndCard();
  BeginCard("##compatibleisos",L("1. Compatible disc images", "1. Imágenes de disco compatibles"));
  ImGui::TextWrapped("%s",L("Forza Motorsport 4 - USA, Rev 1 (multilanguage). Extract the .iso from its ZIP before selecting it. The MD5 values below identify the ISO contents, not the ZIP archive.",
      "Forza Motorsport 4 - USA, Rev 1 (multidioma). Extrae el archivo .iso de su ZIP antes de seleccionarlo. Estos MD5 corresponden al contenido de la ISO, no al ZIP."));
  auto disc_hash = [&](const char* label,const char* hash) {
    ImGui::PushID(label);
    ImGui::Spacing();ImGui::TextUnformatted(label);
    ImGui::Text("MD5: %s",hash);
    if(ImGui::SmallButton(L("Copy MD5", "Copiar MD5"))) SDL_SetClipboardText(hash);
    ImGui::PopID();
  };
  disc_hash(L("Disc 1 - Play Disc (USA, Rev 1)", "Disco 1 - Play Disc (USA, Rev 1)"),"2470358250b8566bf5ddc3aaf1b76c68");
  disc_hash(L("Disc 2 - Content Install Disc (USA, Rev 1)", "Disco 2 - Content Install Disc (USA, Rev 1)"),"9915d1adaabf3b58441651a70e5e8dbe");
  TextDim(L("Game images and mod archives are not included with this release.", "Las imágenes del juego y los archivos del mod no se incluyen en este release."));
  EndCard();
  BeginCard("##installdiscshelp",L("2. Install the game", "2. Instalar el juego"));
  ImGui::TextWrapped("%s",L("1. Open Install and choose the Disc 1 ISO in Step 1. Wait for extraction and verification.\n2. In Step 2, choose the Disc 2 ISO to install the additional car and track packs.\n3. Choose or create a profile in Profiles, then press Play.",
      "1. Abre Instalación y selecciona la ISO del Disco 1 en el paso 1. Espera a que termine la extracción y verificación.\n2. En el paso 2, selecciona la ISO del Disco 2 para instalar los packs adicionales de coches y circuitos.\n3. Elige o crea un perfil en Perfiles y pulsa Jugar."));
  if(ImGui::Button(L("Open installation", "Abrir instalación"))) app.tab=Tab::kInstall;
  EndCard();
  BeginCard("##plushelp",L("3. Project Forza Plus 4 (optional)", "3. Project Forza Plus 4 (opcional)"));
  ImGui::TextWrapped("%s",L("Install both discs first. In Install, Step 3, select Project Forza Plus 4 v1.3.zip and optionally Project Forza Plus 4 Update v1.3.zip. Keep these mod archives zipped. Choose Normal, Hard, Sandbox or Vanilla and press Install Forza Plus. After completion, press Use Forza Plus to activate it; Use original game switches back.",
      "Instala primero los dos discos. En Instalación, paso 3, selecciona Project Forza Plus 4 v1.3.zip y, opcionalmente, Project Forza Plus 4 Update v1.3.zip. Conserva estos archivos del mod en ZIP. Elige Normal, Hard, Sandbox o Vanilla y pulsa Instalar Forza Plus. Al terminar, pulsa Usar Forza Plus para activarlo; Usar juego original permite volver."));
  ImGui::Spacing();
  ImGui::TextWrapped("%s",L("The mod uses a separate installation and saves, requiring extra disk space. This experimental adaptation preserves the port's executable; it does not reproduce every change from the mod's patched executable. Only DLC already installed with valid headers can be adapted.",
      "El mod usa una instalación y partidas separadas y necesita espacio adicional. Esta adaptación experimental conserva el ejecutable del port; no reproduce todos los cambios del ejecutable modificado del mod. Solo se adaptan los DLC ya instalados con sus cabeceras válidas."));
  EndCard();
  BeginCard("##wheelhelp",L("4. Wheel and pedals (optional)", "4. Volante y pedales (opcional)"));
  ImGui::TextWrapped("%s",L("In the Wheel tab, press Bind on each control and move it: steering, pedals, buttons and an optional H-shifter, from any USB devices (a separate pedal set or button box works). The live view shows exactly what the game will read. Pick the device that receives force feedback, press Test, and start with a low strength.",
      "En la pestaña Volante, pulsa Asignar en cada control y muévelo: dirección, pedales, botones y palanca en H opcional, desde cualquier dispositivo USB (pedales o botonera separados también). La vista en vivo muestra lo que leerá el juego. Elige el dispositivo de force feedback, pulsa Probar y empieza con poca fuerza."));
  ImGui::Spacing();
  ImGui::TextWrapped("%s",L("Set the rotation in your wheel software first. The clutch pedal presses a button (B by default), so select Manual with clutch in FM4. SimHub telemetry is in the same tab.",
      "Configura primero el giro en el programa de tu base. El embrague pulsa un botón (B por defecto), así que elige Manual con embrague en FM4. La telemetría para SimHub está en la misma pestaña."));
  if(ImGui::Button(L("Open the Wheel tab", "Abrir la pestaña Volante"))) app.tab=Tab::kWheel;
  EndCard();
  BeginCard("##updatehelp",L("Updates and troubleshooting", "Actualizaciones y problemas"));
  ImGui::TextWrapped("%s",L("Close the game and launcher before replacing release files. Keep fm4.user.toml, your game folders and user data. If installation fails, check free space, shorten folder paths and read the task error. Close the game before changing wheel settings.",
      "Cierra juego y launcher antes de sustituir archivos del release. Conserva fm4.user.toml, las carpetas del juego y los datos de usuario. Si falla la instalación, comprueba el espacio libre, acorta las rutas y consulta el error de la tarea. Cierra el juego antes de cambiar ajustes del volante."));
  EndCard();
}

void TabProfiles(App& app) {
  BeginCard("##profiles", L("Profiles", "Perfiles"),
            L("Each profile is a gamertag with its own saves, cars and options.",
              "Cada perfil es un gamertag con sus propias partidas, coches y opciones."));
  const float tile_w = S(180), tile_h = S(150);
  const float avail = ImGui::GetContentRegionAvail().x;
  const int per_row = std::max(1, int((avail + S(12)) / (tile_w + S(12))));
  int index = 0;
  for (int i = 0; i < int(app.state.profiles.size()); ++i) {
    const Profile& profile = app.state.profiles[i];
    const bool active = profile.xuid == app.state.active_xuid;
    if (index % per_row) ImGui::SameLine(0, S(12));
    ImGui::PushID(profile.xuid.c_str());
    ImGui::BeginGroup();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("tile", ImVec2(tile_w, tile_h));
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked() && !active) {
      app.state.active_xuid = profile.xuid;
      app.Save();
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + tile_w, p.y + tile_h),
                      Col(hovered ? ImVec4(0.17f, 0.18f, 0.20f, 1) : kControl), S(12));
    if (active) dl->AddRect(p, ImVec2(p.x + tile_w, p.y + tile_h), Col(kAccent), S(12), 0, S(2));
    // Avatar: initial in a circle.
    const ImVec2 c(p.x + tile_w * 0.5f, p.y + S(52));
    dl->AddCircleFilled(c, S(30), Col(active ? kAccent : ImVec4(0.30f, 0.31f, 0.35f, 1)));
    const char initial[2] = {char(std::toupper(static_cast<unsigned char>(profile.name[0]))), 0};
    ImGui::PushFont(g_bold, ImGui::GetStyle().FontSizeBase * 1.6f);
    const ImVec2 isz = ImGui::CalcTextSize(initial);
    dl->AddText(ImVec2(c.x - isz.x * 0.5f, c.y - isz.y * 0.5f), IM_COL32_WHITE, initial);
    ImGui::PopFont();
    const ImVec2 nsz = ImGui::CalcTextSize(profile.name.c_str());
    dl->AddText(ImVec2(p.x + (tile_w - nsz.x) * 0.5f, p.y + S(94)), Col(kText), profile.name.c_str());
    const char* tag = active ? L("Active", "Activo") : L("Use", "Usar");
    const ImVec2 tsz = ImGui::CalcTextSize(tag);
    dl->AddText(ImVec2(p.x + (tile_w - tsz.x) * 0.5f, p.y + S(120)), Col(active ? kAccent : kDim), tag);
    // Below the tile, outside it, so the click never reaches the tile.
    if (!active) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(kBad.x, kBad.y, kBad.z, 0.25f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(kBad.x, kBad.y, kBad.z, 0.45f));
      ImGui::PushStyleColor(ImGuiCol_Text, kBad);
      if (ImGui::Button(L("Delete profile", "Eliminar perfil"), ImVec2(tile_w, 0))) {
        app.delete_candidate = i;
        app.open_delete_popup = true;
      }
      ImGui::PopStyleColor(4);
    } else {
      ImGui::Dummy(ImVec2(tile_w, ImGui::GetFrameHeight()));
    }
    ImGui::EndGroup();
    ImGui::PopID();
    ++index;
  }
  EndCard();

  BeginCard("##newprofile", L("New profile", "Nuevo perfil"),
            L("1 to 15 letters, digits or spaces.", "De 1 a 15 letras, números o espacios."));
  const float button_w = S(120);
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - button_w - S(10));
  const bool enter = ImGui::InputTextWithHint("##new", L("Profile name", "Nombre del perfil"), app.new_profile,
                                              sizeof(app.new_profile), ImGuiInputTextFlags_EnterReturnsTrue);
  const std::string name = app.new_profile;
  const bool valid = IsValidGamertag(name);
  const bool exists = std::any_of(app.state.profiles.begin(), app.state.profiles.end(), [&](const Profile& p) {
    return SDL_strcasecmp(p.name.c_str(), name.c_str()) == 0;
  });
  ImGui::SameLine(0, S(10));
  ImGui::BeginDisabled(!valid || exists);
  if (PrimaryButton(L("Create", "Crear"), ImVec2(button_w, 0)) || (enter && valid && !exists)) {
    app.state.profiles.push_back({name, GenerateXuid()});
    app.state.active_xuid = app.state.profiles.back().xuid;
    app.new_profile[0] = 0;
    app.Save();
  }
  ImGui::EndDisabled();
  if (!name.empty() && !valid) TextColored(kWarn, L("Only letters, digits and single spaces.", "Solo letras, números y espacios simples."));
  else if (exists) TextColored(kWarn, L("A profile with that name already exists.", "Ya existe un perfil con ese nombre."));
  EndCard();

  if (app.open_delete_popup) {
    ImGui::OpenPopup("###delete_profile");
    app.open_delete_popup = false;
  }
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  const std::string popup_title = std::string(L("Delete profile", "Eliminar perfil")) + "###delete_profile";
  if (ImGui::BeginPopupModal(popup_title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (app.delete_candidate < 0 || app.delete_candidate >= int(app.state.profiles.size())) {
      ImGui::CloseCurrentPopup();
    } else {
      const Profile victim = app.state.profiles[app.delete_candidate];
      ImGui::Text(L("Delete profile \"%s\" and its saves?", "¿Eliminar el perfil \"%s\" y sus partidas?"),
                  victim.name.c_str());
      TextDim(L("Its saves are moved to the deleted-profiles folder in the user data, in case you change your mind.",
                "Sus partidas se mueven a la carpeta deleted-profiles de los datos de usuario, por si te arrepientes."));
      ImGui::Dummy(ImVec2(0, S(6)));
      ImGui::PushStyleColor(ImGuiCol_Button, kBad);
      if (ImGui::Button(L("Delete", "Eliminar"), ImVec2(S(130), 0))) {
        const fs::path trash = app.UserDir() / "deleted-profiles";
        const fs::path from = app.UserDir() / victim.xuid;
        std::error_code ec;
        if (fs::exists(from, ec)) {
          fs::create_directories(trash, ec);
          fs::rename(from, trash / fs::u8path(victim.name + "-" + victim.xuid + "-" + std::to_string(std::time(nullptr))), ec);
        }
        if (ec) {
          app.notice = std::string(L("Could not delete the profile: ", "No se pudo eliminar el perfil: ")) + ec.message();
        } else {
          app.state.profiles.erase(app.state.profiles.begin() + app.delete_candidate);
          app.Save();
        }
        app.delete_candidate = -1;
        ImGui::CloseCurrentPopup();
      }
      ImGui::PopStyleColor();
      ImGui::SameLine();
      if (ImGui::Button(L("Cancel", "Cancelar"), ImVec2(S(130), 0))) ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void TabGraphics(App& app) {
  Settings& s = app.settings;
  bool changed = false;

  BeginCard("##display", L("Display", "Pantalla"));
  if (BeginRows("##d")) {
    Row(L("Mode", "Modo"));
    int mode = s.fullscreen ? 1 : 0;
    if (Segmented<int>("mode", mode, {{1, L("Fullscreen", "Pantalla completa")}, {0, L("Window", "Ventana")}})) {
      s.fullscreen = mode == 1;
      changed = true;
    }
    Row(L("Monitor", "Monitor"));
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    std::vector<std::string> names;
    for (int i = 0; i < count; ++i) {
      const char* n = SDL_GetDisplayName(displays[i]);
      names.push_back(std::to_string(i + 1) + " · " + (n ? n : "Monitor"));
    }
    SDL_free(displays);
    std::vector<std::pair<int, const char*>> monitors{{0, L("Primary", "Principal")}};
    for (int i = 0; i < count; ++i) monitors.emplace_back(i + 1, names[i].c_str());
    changed |= Combo<int>("##monitor", s.monitor, monitors);
    if (!s.fullscreen) {
      Row(L("Window size", "Tamaño de ventana"));
      int size = s.window_width * 10000 + s.window_height;
      if (Combo<int>("##winsize", size,
                     {{12800720, "1280 x 720"},
                      {16000900, "1600 x 900"},
                      {19201080, "1920 x 1080"},
                      {25601440, "2560 x 1440"},
                      {38402160, "3840 x 2160"},
                      {25601080, "2560 x 1080 (21:9)"},
                      {34401440, "3440 x 1440 (21:9)"},
                      {38401080, "3840 x 1080 (32:9)"},
                      {51201440, "5120 x 1440 (32:9)"}})) {
        s.window_width = size / 10000;
        s.window_height = size % 10000;
        changed = true;
      }
    }
    // Wider than 16:9: the game widens its view (Hor+) and fills the window,
    // or keeps its 16:9 image with black bars, or stretches it.
    Row(L("Wide screens", "Pantallas panorámicas"));
    int wide = s.display_wide_view ? 0 : s.present_letterbox ? 1 : 2;
    if (Segmented<int>("##wide", wide,
                       {{0, L("Fill Window", "Llenar ventana")},
                        {1, L("Black bars", "Bandas negras")},
                        {2, L("Stretch", "Estirar")}})) {
      s.display_wide_view = wide == 0;
      s.present_letterbox = wide != 2;
      changed = true;
    }
    // With Fill Window: the HUD in the middle 16:9, or its sides at the
    // window's edges.
    ImGui::BeginDisabled(!s.display_wide_view);
    Row(L("HUD Position", "Posición del HUD"));
    if (Segmented<bool>("##hudedges", s.display_hud_edges,
                        {{false, L("Standard", "Estándar")}, {true, L("Edges", "Bordes")}}))
      changed = true;
    ImGui::EndDisabled();
    Row(L("Vertical sync (VSync)", "Sincronizacion vertical (VSync)"));
    changed |= Toggle("##vsync", &s.vsync);
    Row(L("Frame rate", "Fotogramas por segundo"));
    changed |= Segmented<bool>("##framerate",s.experimental_120fps,
        {{false,"60 FPS / 60 Hz"},{true,"120 FPS / 120 Hz"}});
    EndRows();
  }
  if(s.experimental_120fps) {
    TextColored(kWarn, "Experimental");
  }
  EndCard();

  BeginCard("##quality", L("Image quality", "Calidad de imagen"));
  int preset = s.graphics_preset;
  if (Segmented<int>("quality_preset", preset,
      {{0, L("Low", "Bajo")}, {1, L("Medium", "Medio")},
       {2, L("High", "Alto")}, {3, L("Custom", "Personalizado")}})) {
    s.ApplyGraphicsPreset(preset);
    changed = true;
  }
  const char* preset_details[] = {
      L("1x (720p) / Anisotropic filtering 4x / AMD FSR",
        "1x (720p) / Filtrado anisotropico 4x / AMD FSR"),
      L("2x (1440p) / Anisotropic filtering 8x / AMD FSR",
        "2x (1440p) / Filtrado anisotropico 8x / AMD FSR"),
      L("2x (1440p) / Anisotropic filtering 16x / Bilinear",
        "2x (1440p) / Filtrado anisotropico 16x / Bilineal"),
      L("Choose render scale, anisotropic filtering and output scaling below.",
        "Elige la escala, el filtrado anisotropico y el escalado a pantalla.")};
  TextColored(kBad, preset_details[std::clamp(s.graphics_preset, 0, 3)]);
  if (s.graphics_preset == 3 && BeginRows("##q")) {
    Row(L("Render scale", "Escala de renderizado"));
    changed |= Segmented<int>("scale", s.resolution_scale,
                              {{1, "1x 720p"}, {2, "2x 1440p"},
                               {3, "3x 4K"}, {4, "4x 5K"}});
    Row(L("Anisotropic filtering", "Filtrado anisotropico"));
    changed |= Combo<int>("##aniso", s.anisotropic_override,
        {{0, L("Off", "No")}, {1, "1x"}, {2, "2x"}, {3, "4x"}, {4, "8x"}, {5, "16x"}});
    Row(L("Output scaling", "Escalado a pantalla"));
    changed |= Segmented<std::string>("present", s.present_effect,
        {{"bilinear", L("Bilinear", "Bilineal")}, {"cas", "AMD CAS"}, {"fsr", "AMD FSR"}});
    EndRows();
  }
  EndCard();

  if (ImGui::Button(L("Restore defaults", "Restaurar valores predeterminados"))) {
    s.ResetDisplay();
    changed = true;
  }
  if (changed) app.Save();
}

// A card without a title, for cards whose header opens and closes them.
void BeginPanel(const char* id) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, kCard);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(12));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20), S(14)));
  ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
}

void DrawWheelLive(App& app, const wheel_ui::State& ui) {
  using namespace wheel_ui;
  const float height = S(210);
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p0, ImVec2(p0.x + width, p0.y + height), Col(ImVec4(0.07f, 0.075f, 0.085f, 1)), S(14));
  const ImU32 strong = Col(ImVec4(0.42f, 0.43f, 0.47f, 1)), track = Col(kControl), accent = Col(kAccent), sound = Col(kOk);

  // Wheel, turned as a 900-degree wheel would be.
  const Binding& steer = ui.rows[0];
  const float angle = steer.value * 450.0f * 3.14159265f / 180.0f;
  const ImVec2 c(p0.x + S(105), p0.y + S(95));
  const float r = S(78);
  auto rot = [&](float x, float y) {
    return ImVec2(c.x + x * std::cos(angle) - y * std::sin(angle), c.y + x * std::sin(angle) + y * std::cos(angle));
  };
  auto bar = [&](float x0, float y0, float x1, float y1, ImU32 col) {
    dl->AddQuadFilled(rot(x0, y0), rot(x1, y0), rot(x1, y1), rot(x0, y1), col);
  };
  dl->AddCircle(c, r, strong, 64, S(14));
  bar(-r, -S(6), r, S(6), strong);
  bar(-S(6), 0, S(6), r, strong);
  dl->AddCircleFilled(c, S(22), Col(kControl), 32);
  dl->AddCircle(c, S(22), strong, 32, S(3));
  bar(-S(4), -r - S(8), S(4), -r + S(8), accent);
  char text[48];
  std::snprintf(text, sizeof(text), "%s %s", L("Steering", "Dirección"), steer.inputs.empty() ? "\xE2\x80\x94" : steer.text.c_str());
  const ImVec2 ts = ImGui::CalcTextSize(text);
  dl->AddText(ImVec2(c.x - ts.x / 2, p0.y + height - S(28)), Col(kText), text);

  // Pedals.
  const char* pedals[] = {"CLUTCH", "BRAKE", "THROTTLE"};
  const char* names[] = {L("Clutch", "Embrague"), L("Brake", "Freno"), L("Throttle", "Acelerador")};
  for (int i = 0; i < 3; ++i) {
    const Binding* row = nullptr;
    for (const auto& rr : ui.rows) if (std::strcmp(rr.key, pedals[i]) == 0) row = &rr;
    const float x = p0.x + S(230) + i * S(62), top = p0.y + S(18), bottom = p0.y + height - S(54);
    dl->AddRectFilled(ImVec2(x, top), ImVec2(x + S(30), bottom), track, S(6));
    if (row && !row->inputs.empty() && row->value > 0)
      dl->AddRectFilled(ImVec2(x, bottom - (bottom - top) * row->value), ImVec2(x + S(30), bottom), i == 2 ? sound : accent, S(6));
    const ImVec2 ns = ImGui::CalcTextSize(names[i]);
    dl->AddText(ImVec2(x + S(15) - ns.x / 2, bottom + S(6)), Col(kDim), names[i]);
    const std::string value = row && !row->inputs.empty() ? row->text : "\xE2\x80\x94";
    const ImVec2 vs = ImGui::CalcTextSize(value.c_str());
    dl->AddText(ImVec2(x + S(15) - vs.x / 2, bottom + S(24)), Col(kText), value.c_str());
  }

  // Button chips.
  float x = p0.x + S(430), y = p0.y + S(16);
  const float chip_w = S(52), chip_h = S(24), right = p0.x + width - S(12);
  for (const auto& row : ui.rows) {
    if (row.kind != Kind::kButton || IsCamera(row)) continue;
    if (x + chip_w > right) { x = p0.x + S(430); y += chip_h + S(6); }
    const bool bound = !row.inputs.empty();
    const ImU32 fill = row.pressed ? sound : Col(kControl, bound ? 1.0f : 0.45f);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + chip_w, y + chip_h), fill, chip_h / 2);
    const ImVec2 cs = ImGui::CalcTextSize(row.chip);
    dl->AddText(ImVec2(x + (chip_w - cs.x) / 2, y + (chip_h - cs.y) / 2), Col(row.pressed ? ImVec4(0, 0, 0, 1) : kText, bound ? 1.0f : 0.5f), row.chip);
    x += chip_w + S(6);
  }
  ImGui::Dummy(ImVec2(width, height));
  (void)app;
}

// A small horizontal meter: centred for steering, filled from the left otherwise.
void Meter(float value, bool centred, float width) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float h = S(8), y = p.y + (ImGui::GetFrameHeight() - h) / 2;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + width, y + h), Col(kControl), h / 2);
  if (centred) {
    const float mid = p.x + width / 2, end = mid + value * width / 2;
    if (std::fabs(end - mid) >= 1) dl->AddRectFilled(ImVec2(std::min(mid, end), y), ImVec2(std::max(mid, end), y + h), Col(kAccent), h / 2);
  } else if (value > 0) {
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + width * std::clamp(value, 0.0f, 1.0f), y + h), Col(kAccent), h / 2);
  }
  ImGui::Dummy(ImVec2(width, ImGui::GetFrameHeight()));
}

bool CardHeader(const char* id, const char* title, const char* summary, bool& open) {
  ImGui::PushID(id);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float w = ImGui::GetContentRegionAvail().x;
  const bool clicked = ImGui::InvisibleButton("hdr", ImVec2(w, ImGui::GetTextLineHeight() * 1.4f));
  if (clicked) open = !open;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImGui::PushFont(g_bold, ImGui::GetStyle().FontSizeBase * 1.1f);
  dl->AddText(p, Col(kText), title);
  ImGui::PopFont();
  const ImVec2 ss = ImGui::CalcTextSize(summary);
  dl->AddText(ImVec2(p.x + w - ss.x - S(22), p.y + S(2)), Col(kDim), summary);
  dl->AddText(ImVec2(p.x + w - S(12), p.y + S(1)), Col(kDim), open ? "-" : "+");
  ImGui::PopID();
  return clicked;
}

bool Percent(const char* label, int* value, int lo, int hi, const char* hint = nullptr) {
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  if (hint && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hint);
  ImGui::SameLine(S(170));
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::PushID(label);
  const bool changed = ImGui::SliderInt("##v", value, lo, hi, "%d %%", ImGuiSliderFlags_AlwaysClamp);
  ImGui::PopID();
  return changed;
}

// A slider in degrees, in steps of 10; with off_label, values below 90 are 0
// ("off").
bool Degrees(const char* label, int* value, int lo, int hi, const char* off_label, const char* hint, float width) {
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  if (hint && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hint);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(width);
  ImGui::PushID(label);
  const bool off = off_label && *value <= 0;
  int shown = off ? lo : *value;
  const bool changed = ImGui::SliderInt("##v", &shown, lo, hi, off ? off_label : "%d\xC2\xB0", ImGuiSliderFlags_AlwaysClamp);
  if (hint && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hint);
  ImGui::PopID();
  if (!changed) return false;
  shown = (shown + 5) / 10 * 10;
  *value = off_label && shown < 90 ? 0 : std::max(shown, 90);
  return true;
}

void TabWheel(App& app) {
  namespace w = fm4::wheel;
  using namespace wheel_ui;
  auto& ui = app.wheel_ui;
  auto& c = app.settings.wheel;
  if (app.game) {
    BeginCard("##wheelbusy", L("Wheel", "Volante"));
    TextDim(L("The game has the wheel. Close the game to change bindings or test force feedback; changes apply from the next game start.",
              "El juego tiene el volante. Ciérralo para cambiar asignaciones o probar el force feedback; los cambios se aplican al volver a iniciarlo."));
    EndCard();
    return;
  }
  if (!app.wheel_monitor) app.wheel_monitor = std::make_unique<w::DeviceMonitor>(true);
  app.wheel_monitor->ConfigureForce(c);
  const auto snap = app.wheel_monitor->Read();
  const auto& devices = snap.devices;
  if (!ui.loaded) {
    Load(ui, c);
    std::snprintf(ui.target, sizeof(ui.target), "%s", c.telemetry_target.c_str());
    ui.loaded = true;
  }
  bool changed = false;
  auto commit = [&] { Store(ui, c); changed = true; };
  TrackCapture(ui, devices);
  UpdateLive(ui, c, devices);
  Capture& cap = ui.capture;
  auto end_capture = [&](bool finished) {
    const bool guided = cap.guided;
    cap = Capture{};
    if (finished && guided)
      ui.info = L("All set. Check the live view: turn the wheel, press the pedals and buttons. Bindings are saved.",
                  "Listo. Comprueba la vista en vivo: gira el volante, pisa los pedales y pulsa los botones. Asignaciones guardadas.");
  };
  auto advance = [&] {
    if (cap.guided && cap.row + 1 < int(ui.rows.size()) && !IsCamera(ui.rows[cap.row + 1]))
      BeginCapture(ui, cap.row + 1, true, devices);
    else end_capture(true);
  };
  auto commit_candidate = [&] {
    if (!cap.found) return;
    Binding& row = ui.rows[cap.row];
    if (row.kind == Kind::kButton) {
      for (auto& other : ui.rows) {
        if (&other == &row || other.kind != Kind::kButton) continue;
        other.inputs.erase(std::remove_if(other.inputs.begin(), other.inputs.end(),
                                          [&](const Input& i) { return i.Same(cap.candidate); }),
                           other.inputs.end());
      }
    }
    if (cap.adding && row.kind == Kind::kButton) {
      if (std::none_of(row.inputs.begin(), row.inputs.end(), [&](const Input& i) { return i.Same(cap.candidate); }))
        row.inputs.push_back(cap.candidate);
    } else {
      row.inputs = {cap.candidate};
    }
    commit();
    advance();
  };
  // A single Bind needs no Finish: a button binds as soon as it is pressed, an
  // axis once it has moved through its travel and come back.
  if (cap.row >= 0 && !cap.guided && cap.found) {
    if (cap.candidate.type != InputType::kAxis) commit_candidate();
    else if (CandidateReleased(ui, devices) && !RestsMidRange(ui)) {
      if (cap.released_at == Clock::time_point{}) cap.released_at = Clock::now();
      else if (Clock::now() - cap.released_at > std::chrono::milliseconds(300)) commit_candidate();
    } else cap.released_at = {};
  }
  if (cap.row >= 0 && !ImGui::GetIO().WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) && cap.found) commit_candidate();
    else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) end_capture(false);
  }

  // --- Main card: live view and the setup step, always in sight.
  BeginCard("##wheelmain", L("Wheel & Pedals", "Volante y pedales"));
  if (ImGui::BeginTable("##wheelon", 3, ImGuiTableFlags_None)) {
    const float toggle = ImGui::GetFrameHeight() * 0.9f * 1.85f;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("toggle", ImGuiTableColumnFlags_WidthFixed, toggle);
    ImGui::TableSetupColumn("rotation", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(L("Enable Wheel Support", "Activar volante"));
    ImGui::TableSetColumnIndex(1);
    if (Toggle("##wheelenabled", &c.enabled)) changed = true;
    // Match the game's steering wheel to the real one: full lock comes at the
    // game's rotation instead of the wheel's. Right-aligned.
    ImGui::TableSetColumnIndex(2);
    const float slider = S(150);
    const char* wheel_label = L("Wheel rotation", "Giro del volante");
    const char* game_label = L("In-game rotation", "Giro en el juego");
    const ImGuiStyle& style = ImGui::GetStyle();
    const float need = ImGui::CalcTextSize(wheel_label).x + ImGui::CalcTextSize(game_label).x + 2 * slider +
                       3 * style.ItemSpacing.x + S(24);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - need));
    changed |= Degrees(wheel_label, &c.wheel_rotation, 180, 1440, nullptr,
                       L("The rotation set in your wheel software (Fanatec, MOZA Pit House, ...), lock to lock.",
                         "El giro configurado en el software del volante, de tope a tope."),
                       slider);
    ImGui::SameLine(0, S(24));
    changed |= Degrees(game_label, &c.game_rotation, 0, 1080, L("Off", "No"),
                       L("How far the game's steering wheel turns lock to lock. Set it and the car reaches full lock when your wheel has turned that far, so the wheel in the cockpit follows yours. Off: your wheel's full rotation is full lock.\n\nIn cockpit view, turn your wheel: if the game's wheel turns further than yours, lower it; if less, raise it.",
                         "Cuánto gira el volante del juego de tope a tope. Con un valor, el coche llega al tope cuando tu volante ha girado eso y el volante de la cabina sigue al tuyo. No: todo el giro de tu volante es el tope.\n\nEn la vista de cabina, gira el volante: si el del juego gira más que el tuyo, bájalo; si gira menos, súbelo."),
                       slider);
    ImGui::EndTable();
  }
  DrawWheelLive(app, ui);
  ImGui::Dummy(ImVec2(0, S(6)));
  // The guided setup is hidden for now; Bind on each row still works.
  constexpr bool kGuidedSetup = false;
  if (cap.row < 0) {
    if (kGuidedSetup && PrimaryButton(L("Start guided setup", "Iniciar configuración guiada"), ImVec2(S(240), S(36)))) {
      ui.info.clear();
      BeginCapture(ui, 0, true, devices);
    }
  } else if (ImGui::Button(cap.guided ? L("Stop setup", "Detener configuración") : L("Cancel", "Cancelar"), ImVec2(S(240), S(36)))) {
    end_capture(false);
  }
  if (!ui.info.empty()) { ImGui::SameLine(); ImGui::PushTextWrapPos(); TextColored(kOk, ui.info.c_str()); ImGui::PopTextWrapPos(); }

  if (cap.row >= 0) {
    Binding& row = ui.rows[cap.row];
    ImGui::Dummy(ImVec2(0, S(6)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.13f, 0.135f, 0.155f, 1));
    ImGui::PushStyleColor(ImGuiCol_Border, kAccent);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(8));
    ImGui::BeginChild("##step", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    Title(L(row.label, row.label_es), 1.2f);
    if (cap.guided) {
      ImGui::SameLine();
      char step[48];
      std::snprintf(step, sizeof(step), L("Step %d of %d", "Paso %d de %d"), cap.row + 1, int(ui.rows.size()));
      TextDim(step);
    }
    ImGui::TextWrapped("%s", L(row.prompt, row.prompt_es));
    if (!cap.guided)
      TextDim(row.kind == Kind::kButton ? L("It binds as soon as you press it.", "Se asigna en cuanto lo pulses.")
                                        : L("It binds when you let go.", "Se asigna al soltarlo."));
    if (!cap.found) {
      const float t = float(ImGui::GetTime());
      StatusDot(ImVec4(kOk.x, kOk.y, kOk.z, 0.4f + 0.6f * std::fabs(std::sin(t * 2.2f))));
      ImGui::TextUnformatted(row.kind == Kind::kSteering ? L("Listening - turn the wheel left...", "Escuchando: gira el volante a la izquierda...")
                             : row.kind == Kind::kPedal  ? L("Listening - press the pedal...", "Escuchando: pisa el pedal...")
                                                         : L("Listening - press a button...", "Escuchando: pulsa un botón..."));
    } else {
      StatusDot(kOk);
      std::string where = std::string(L("Detected: ", "Detectado: ")) + DeviceName(devices, cap.candidate.device) + " \xC2\xB7 " + Describe(cap.candidate);
      if (cap.candidate.type == InputType::kAxis) where += std::string(L(" \xC2\xB7 rests at ", " \xC2\xB7 reposo ")) + std::to_string(cap.rest);
      ImGui::TextWrapped("%s", where.c_str());
      if (const auto* d = Resolve(devices, cap.candidate.device)) {
        float v; bool p; std::string t;
        Evaluate(ui, c, row, cap.candidate, *d, v, p, t);
        Meter(row.kind == Kind::kSteering ? v : v, row.kind == Kind::kSteering, ImGui::GetContentRegionAvail().x - S(200));
        ImGui::SameLine();
        ImGui::TextUnformatted(cap.candidate.type == InputType::kAxis ? t.c_str() : p ? L("pressed", "pulsado") : L("released", "suelto"));
        if (cap.candidate.type == InputType::kAxis) {
          ImGui::SameLine();
          if (ImGui::Checkbox(L("Invert##step", "Invertir##step"), &cap.candidate.inverted)) cap.user_invert = true;
        }
      }
      if (RestsMidRange(ui))
        TextColored(kWarn, L("This axis rests in the middle of its range, so the game would read it as half pressed. Calibrate it to its full range in the device's own software, or Rebind.",
                             "Este eje reposa a media carrera, así que el juego lo leería medio pisado. Calíbralo en el programa del dispositivo o vuelve a asignarlo."));
      else if (row.kind == Kind::kButton) {
        for (const auto& other : ui.rows) {
          if (&other == &row || other.kind != Kind::kButton) continue;
          if (std::any_of(other.inputs.begin(), other.inputs.end(), [&](const Input& i) { return i.Same(cap.candidate); })) {
            TextColored(kWarn, (std::string(L("This is also bound to ", "También está asignado a ")) + L(other.label, other.label_es) +
                                L(". Next moves it here.", ". Siguiente lo mueve aquí.")).c_str());
            break;
          }
        }
      }
    }
    ImGui::Dummy(ImVec2(0, S(4)));
    if (cap.guided) {
      ImGui::BeginDisabled(cap.row == 0);
      if (ImGui::Button(L("Back", "Atrás"))) BeginCapture(ui, cap.row - 1, true, devices);
      ImGui::EndDisabled();
      ImGui::SameLine();
    }
    if (ImGui::Button(L("Leave unbound", "Dejar sin asignar"))) { row.inputs.clear(); commit(); advance(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L("This control has nothing on your rig", "Este control no existe en tu equipo"));
    ImGui::SameLine();
    if (ImGui::Button(cap.guided ? L("Skip", "Omitir") : L("Cancel##step", "Cancelar##step"))) {
      // No H-shifter: skipping 1st gear while it is unbound skips the others too.
      if (cap.guided && std::strcmp(row.key, "GEAR1") == 0 && row.inputs.empty()) end_capture(true);
      else if (cap.guided) advance();
      else end_capture(false);
    }
    if (cap.row >= 0 && cap.found) {
      ImGui::SameLine();
      if (ImGui::Button(L("Rebind", "Reasignar"))) BeginCapture(ui, cap.row, cap.guided, devices);
      if (cap.guided) {
        ImGui::SameLine();
        if (PrimaryButton(cap.row + 1 == int(ui.rows.size()) ? L("Finish", "Terminar") : L("Next", "Siguiente"), ImVec2(S(120), 0)))
          commit_candidate();
      }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
  }

  EndCard();

  // The cards below scroll under the main card, which stays fixed so the
  // live view is in sight while they change.
  ImGui::BeginChild("##wheelcards", ImVec2(0, 0));

  // --- Bindings: collapsible, so the live view above stays in sight while
  // the cards below change dead zones and the like.
  BeginPanel("##wheelbindings");
  int bound = 0;
  for (const auto& r : ui.rows) bound += r.inputs.empty() ? 0 : 1;
  char bindings_summary[48];
  std::snprintf(bindings_summary, sizeof(bindings_summary), L("%d of %d bound", "%d de %d asignados"), bound, int(ui.rows.size()));
  CardHeader("bindings", L("Bindings", "Asignaciones"), bindings_summary, ui.open_bindings);
  if (ui.open_bindings || cap.row >= 0) {
  ImGui::Dummy(ImVec2(0, S(4)));
  if (ImGui::BeginTable("##bindings", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn(L("Control", "Control"), ImGuiTableColumnFlags_WidthFixed, S(150));
    ImGui::TableSetupColumn(L("Bound to", "Asignado a"), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(L("Live", "En vivo"), ImGuiTableColumnFlags_WidthFixed, S(110));
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(72));
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(46));
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(58));
    ImGui::TableHeadersRow();
    for (int i = 0; i < int(ui.rows.size()); ++i) {
      Binding& row = ui.rows[i];
      if (IsCamera(row) && (i == 0 || !IsCamera(ui.rows[i - 1]))) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Dummy(ImVec2(0, S(4)));
        TextColored(kAccent, L("Camera", "Cámara"));
        ImGui::TableSetColumnIndex(1);
        ImGui::Dummy(ImVec2(0, S(4)));
        TextDim(L("In game: field of view and seat position, for each camera view. Keys for these are in Controls.",
                  "En el juego: campo de visión y posición del asiento, por vista. Las teclas están en Controles."));
      }
      ImGui::PushID(i);
      ImGui::TableNextRow();
      if (cap.row == i) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, Col(kAccent, 0.25f));
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(L(row.label, row.label_es));
      ImGui::TableSetColumnIndex(1);
      std::string bound;
      for (const auto& in : row.inputs) bound += (bound.empty() ? "" : "  +  ") + DeviceName(devices, in.device) + " \xC2\xB7 " + Describe(in);
      ImGui::AlignTextToFramePadding();
      if (bound.empty()) TextDim(L("Not bound", "Sin asignar"));
      else {
        ImGui::TextUnformatted(bound.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", bound.c_str());
      }
      ImGui::TableSetColumnIndex(2);
      if (row.kind == Kind::kButton) {
        StatusDot(row.pressed ? kOk : kControl);
        ImGui::TextUnformatted(row.text.c_str());
      } else {
        Meter(row.value, row.kind == Kind::kSteering, S(56));
        ImGui::SameLine();
        ImGui::TextUnformatted(row.text.c_str());
      }
      ImGui::TableSetColumnIndex(3);
      if (row.kind != Kind::kButton && !row.inputs.empty()) {
        if (ImGui::Checkbox(L("Invert", "Invertir"), &row.inputs[0].inverted)) commit();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L("Reverse this axis. Use it if the live bar moves the wrong way.", "Invierte este eje si la barra se mueve al revés."));
      }
      ImGui::TableSetColumnIndex(4);
      ImGui::BeginDisabled(cap.row >= 0 && cap.guided);
      if (ImGui::SmallButton(L("Bind", "Asignar"))) { ui.info.clear(); BeginCapture(ui, i, false, devices); }
      ImGui::EndDisabled();
      ImGui::TableSetColumnIndex(5);
      if (row.kind == Kind::kButton && !row.inputs.empty()) {
        if (ImGui::SmallButton("+")) {
          BeginCapture(ui, i, false, devices);
          cap.adding = true;
          ui.info = L("Press another input for this button; it keeps the ones it has.", "Pulsa otra entrada para este botón; conserva las que tiene.");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L("Add another input that presses this same button (e.g. a handbrake lever and a rim button)", "Añade otra entrada que pulse este mismo botón"));
        ImGui::SameLine(0, S(4));
      }
      if (!row.inputs.empty() && ImGui::SmallButton("x")) { row.inputs.clear(); commit(); }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  TextDim(L("Enter: next step \xC2\xB7 Esc: stop the setup \xC2\xB7 Bind on any row binds just that control.",
            "Intro: siguiente paso \xC2\xB7 Esc: detener \xC2\xB7 Asignar en una fila asigna solo ese control."));
  }
  EndCard();

  // --- Force feedback.
  BeginPanel("##wheelffb");
  const w::FfbMode mode = w::ParseFfbMode(c.ffb_mode);
  char ffb_summary[64];
  std::snprintf(ffb_summary, sizeof(ffb_summary), "%s \xC2\xB7 %d %%",
                mode == w::FfbMode::kEffects ? L("On", "Activado") : mode == w::FfbMode::kRumble ? L("Rumble only", "Solo vibración") : L("Off", "Desactivado"),
                c.ffb_strength);
  CardHeader("ffb", L("Force Feedback", "Force feedback"), ffb_summary, ui.open_ffb);
  if (ui.open_ffb) {
    TextDim(L("Device that receives the forces", "Dispositivo que recibe las fuerzas"));
    const std::string steer_device = Get(ui, "STEER").inputs.empty() ? std::string() : Get(ui, "STEER").inputs[0].device;
    std::string preview = c.ffb_device.empty()
        ? std::string(L("The wheel base", "La base del volante")) + (steer_device.empty() ? "" : " (" + DeviceName(devices, steer_device) + ")")
        : DeviceName(devices, c.ffb_device);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - S(110));
    if (ImGui::BeginCombo("##ffbdevice", preview.c_str())) {
      if (ImGui::Selectable(L("The wheel base", "La base del volante"), c.ffb_device.empty())) { c.ffb_device.clear(); changed = true; }
      for (const auto& d : devices) {
        ImGui::PushID(int(d.id));
        const std::string match = w::DeviceMatch(d, devices);
        if (ImGui::Selectable(d.name.c_str(), c.ffb_device == match)) { c.ffb_device = match; changed = true; }
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(snap.test_busy);
    if (ImGui::Button(L("Test", "Probar"), ImVec2(S(100), 0))) {
      app.wheel_monitor->ConfigureForce(c);
      app.wheel_monitor->TestHaptics();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L("Pulls the wheel LEFT, then RIGHT, as hard as Strength: 100 % is half the base's maximum, 200 % all of it", "Tira del volante a la IZQUIERDA y luego a la DERECHA, según Fuerza: 100 % es la mitad del máximo de la base, 200 % todo"));
    if (!snap.haptic_status.empty()) TextDim(snap.haptic_status.c_str());
    TextDim(L("Pick your wheel base. Test pulls the wheel LEFT, then RIGHT. If it pulls right first, tick Reverse force direction. A base can appear twice; if Test does not move the wheel, pick the other entry.",
              "Elige tu base. Probar tira a la IZQUIERDA y luego a la DERECHA. Si tira primero a la derecha, marca Invertir dirección. Una base puede aparecer dos veces; si no se mueve, elige la otra."));
    ImGui::Dummy(ImVec2(0, S(4)));
    if (BeginRows("##ffbmode")) {
      Row(L("Force feedback", "Force feedback"));
      if (Combo<std::string>("##mode", c.ffb_mode, {{"on", L("On: the game's own wheel effects", "Activado: los efectos del propio juego")},
                                                     {"rumble", L("Rumble only", "Solo vibración")},
                                                     {"off", L("Off", "Desactivado")}}))
        changed = true;
      EndRows();
    }
    changed |= Percent(L("Strength", "Fuerza"), &c.ffb_strength, 0, 200,
                       L("Overall force feedback strength. Above 100 % the game's strongest forces clip at the base's maximum.", "Fuerza general. Por encima del 100 % las fuerzas más fuertes se recortan al máximo de la base."));
    changed |= Percent(L("Minimum force", "Fuerza mínima"), &c.ffb_min_force, 0, 30,
                       L("Lifts small forces past a geared or belted wheel's friction. A direct-drive base needs 0.", "Eleva las fuerzas pequeñas en volantes de engranajes o correa. Una base direct drive necesita 0."));
    changed |= Percent(L("Spring", "Muelle"), &c.ffb_spring, 0, 200, L("The game's centring spring, which stiffens with speed", "El muelle de centrado del juego"));
    changed |= Percent(L("Damper", "Amortiguador"), &c.ffb_damper, 0, 200, L("The game's damper (resists fast wheel movement)", "El amortiguador del juego"));
    changed |= Percent(L("Friction", "Fricción"), &c.ffb_friction, 0, 200, L("The game's friction effect", "La fricción del juego"));
    changed |= Percent(L("Inertia", "Inercia"), &c.ffb_inertia, 0, 200, L("The game's inertia effect", "La inercia del juego"));
    changed |= Percent(L("Rumble", "Vibración"), &c.ffb_rumble, 0, 100, L("The game's controller rumble, played as a vibration on the wheel", "La vibración del juego, en el volante"));
    changed |= Percent(L("Engine rumble", "Vibración motor"), &c.ffb_rumble_engine, 0, 100, L("Share for the engine (right) motor", "Parte del motor derecho (motor)"));
    changed |= Percent(L("Road rumble", "Vibración firme"), &c.ffb_rumble_road, 0, 100, L("Share for the bumps, kerbs and impacts (left) motor", "Parte del motor izquierdo (baches, pianos, golpes)"));
    changed |= ImGui::Checkbox(L("Reverse force direction", "Invertir dirección de la fuerza"), &c.ffb_invert);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L("Leave off unless the wheel pushes away from centre in corners", "Déjalo desactivado salvo que el volante empuje hacia fuera en las curvas"));
    TextDim(L("Start low on a direct-drive base. FM4 sends its own spring, damper, friction, inertia and constant-force effects; these sliders scale them.",
              "Empieza bajo en una base direct drive. FM4 envía sus propios efectos; estos controles los escalan."));
  }
  EndCard();

  // --- Telemetry.
  BeginPanel("##wheeltelemetry");
  CardHeader("telemetry", L("Telemetry", "Telemetría"), c.telemetry_udp ? c.telemetry_target.c_str() : L("Off", "Desactivada"), ui.open_telemetry);
  if (ui.open_telemetry) {
    if (BeginRows("##telem")) {
      Row(L("Send telemetry", "Enviar telemetría"),
          L("For SimHub, dashboards and shakers: RPM, gear, speed, pedals, wheel slip and suspension in Forza's Data Out format. Menus and pause send race off.",
            "Para SimHub, paneles y shakers: RPM, marcha, velocidad, pedales, deslizamiento y suspensión en el formato Data Out de Forza."));
      changed |= Toggle("##telemetry", &c.telemetry_udp);
      Row(L("Game in SimHub", "Juego en SimHub"));
      const std::string before = c.telemetry_format;
      if (Combo<std::string>("##format", c.telemetry_format, {{"fm7", "Forza Motorsport 7"}, {"horizon", "Forza Horizon 4 / 5"}})) {
        // Follow the port to the chosen game's usual one, unless it was changed.
        const std::string fm7 = "127.0.0.1:5300", horizon = "127.0.0.1:8000";
        if (c.telemetry_target == (before == "horizon" ? horizon : fm7))
          c.telemetry_target = c.telemetry_format == "horizon" ? horizon : fm7;
        std::snprintf(ui.target, sizeof(ui.target), "%s", c.telemetry_target.c_str());
        changed = true;
      }
      Row(L("Send to", "Enviar a"), L("host:port", "host:puerto"));
      if (ImGui::InputText("##target", ui.target, sizeof(ui.target)) && std::strchr(ui.target, ':')) {
        c.telemetry_target = ui.target;
        changed = true;
      }
      Row(L("Only while the car moves", "Solo con el coche en marcha"),
          L("Silent while standing still too, not just in menus.", "En silencio también parado, no solo en menús."));
      changed |= Toggle("##moving", &c.telemetry_moving_only);
      EndRows();
    }
    TextDim(L("In SimHub pick the same game as above and set its UDP port to match. The game log lists the values sent every few seconds (doc/telemetry.md).",
              "En SimHub elige el mismo juego y el mismo puerto UDP. El registro del juego muestra los valores enviados (doc/telemetry.md)."));
  }
  EndCard();

  // --- Deadzones (and steering sensitivity, clutch button).
  BeginPanel("##wheelsteer");
  char steer_summary[64];
  std::snprintf(steer_summary, sizeof(steer_summary), "%d %% \xC2\xB7 %s %s", c.steering_scale, L("clutch", "embrague"), c.clutch_button.c_str());
  CardHeader("steer", L("Deadzones", "Zonas muertas"), steer_summary, ui.open_steering);
  if (ui.open_steering) {
    // What the game reads with these values, beside the sliders that change it.
    auto live = [&](const char* key) {
      const Binding& r = Get(ui, key);
      ImGui::AlignTextToFramePadding();
      TextDim(L("Live", "En vivo"));
      ImGui::SameLine(S(170));
      Meter(r.value, r.kind == Kind::kSteering, ImGui::GetContentRegionAvail().x - S(90));
      ImGui::SameLine();
      if (r.inputs.empty()) TextDim(L("not bound", "sin asignar"));
      else ImGui::TextUnformatted(r.text.c_str());
    };
    changed |= Percent(L("Steering sensitivity", "Sensibilidad"), &c.steering_scale, 50, 450,
                       L("200 % reaches full lock at half your wheel's rotation. Set the rotation in your wheel software.", "200 % llega al tope con la mitad del giro del volante."));
    changed |= Percent(L("Steering deadzone", "Zona muerta dirección"), &c.steering_deadzone, 0, 20);
    live("STEER");
    ImGui::Dummy(ImVec2(0, S(4)));
    Title(L("Pedal dead zones", "Zonas muertas de pedales"), 1.0f);
    TextDim(L("Bottom: travel at rest the game ignores. Top: the last part of travel that already counts as 100 %.",
              "Abajo: recorrido en reposo que se ignora. Arriba: el último tramo que ya cuenta como 100 %."));
    const char* pedal_names[] = {L("Throttle", "Acelerador"), L("Brake", "Freno"), L("Clutch", "Embrague")};
    for (int i = 0; i < 3; ++i) {
      ImGui::PushID(i);
      int bottom = c.deadzone_bottom[i] < 0 ? c.pedal_deadzone : c.deadzone_bottom[i];
      if (Percent((std::string(pedal_names[i]) + L(" bottom", " abajo")).c_str(), &bottom, 0, 25)) { c.deadzone_bottom[i] = bottom; changed = true; }
      changed |= Percent((std::string(pedal_names[i]) + L(" top", " arriba")).c_str(), &c.deadzone_top[i], 0, 25);
      live(i == 0 ? "THROTTLE" : i == 1 ? "BRAKE" : "CLUTCH");
      ImGui::Dummy(ImVec2(0, S(4)));
      ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, S(4)));
    if (BeginRows("##clutchrows")) {
      Row(L("Clutch pedal presses", "El embrague pulsa"),
          L("FM4's clutch is a button: match it to the game's controls. The pedal presses it past half travel.",
            "El embrague de FM4 es un botón: hazlo coincidir con los controles del juego."));
      if (Combo<std::string>("##clutchbtn", c.clutch_button, {{"A", "A"}, {"B", "B"}, {"X", "X"}, {"Y", "Y"}, {"LB", "LB"}, {"RB", "RB"}, {"LS", "LS"}, {"RS", "RS"}, {"", L("Nothing", "Nada")}}))
        changed = true;
      Row(L("Analog clutch (experimental)", "Embrague analógico (experimental)"),
          L("Also sends the pedal as the right stick's Y axis, where Forza Horizon reads a clutch. Not confirmed in FM4.",
            "También envía el pedal como eje Y del stick derecho, como Forza Horizon. No confirmado en FM4."));
      changed |= Toggle("##clutchanalog", &c.clutch_analog);
      Row(L("Log raw inputs", "Registrar entradas"), L("Writes every axis, button and hat change to the game log.", "Escribe cada cambio de eje, botón y cruceta en el registro."));
      changed |= Toggle("##loginputs", &c.log_inputs);
      EndRows();
    }
  }
  EndCard();

  // --- Devices.
  BeginPanel("##wheeldevices");
  char summary[64];
  std::snprintf(summary, sizeof(summary), L("%d connected", "%d conectados"), int(devices.size()));
  CardHeader("devices", L("Devices", "Dispositivos"), summary, ui.open_devices);
  if (ui.open_devices) {
    if (devices.empty())
      TextDim(L("No game controllers found. Connect your wheel, pedals and other devices; the list updates by itself.",
                "No hay mandos. Conecta volante, pedales y otros dispositivos; la lista se actualiza sola."));
    const auto now = Clock::now();
    for (const auto& d : devices) {
      auto& [last, at] = ui.activity[d.id];
      bool moved = last.size() != d.axes.size();
      for (size_t i = 0; !moved && i < d.axes.size(); ++i) moved = std::abs(d.axes[i] - last[i]) > 1500;
      moved = moved || std::any_of(d.buttons.begin(), d.buttons.end(), [](bool b) { return b; }) ||
              std::any_of(d.hats.begin(), d.hats.end(), [](uint8_t h) { return h != 0; });
      last = d.axes;
      if (moved) at = now;
      const bool active = now - at < std::chrono::milliseconds(400);
      StatusDot(active ? kOk : kControl);
      ImGui::TextUnformatted(d.name.c_str());
      char line[160];
      std::snprintf(line, sizeof(line), "%04x:%04x \xC2\xB7 %zu %s, %zu %s, %zu %s%s", d.vendor, d.product, d.axes.size(),
                    L("axes", "ejes"), d.buttons.size(), L("buttons", "botones"), d.hats.size(), L("hats", "crucetas"),
                    d.wheel ? L(" \xC2\xB7 wheel", " \xC2\xB7 volante") : "");
      ImGui::Indent(S(16));
      TextDim(line);
      ImGui::Unindent(S(16));
    }
    TextDim(L("A device lights up while you move it, so you can tell which is which.",
              "Un dispositivo se ilumina al moverlo, para distinguirlos."));
  }
  EndCard();
  ImGui::EndChild();

  if (changed) app.Save();
}

void TabControls(App& app) {
  Settings& s = app.settings;
  bool changed = false;
  BeginCard("##wheelnote", L("Racing wheel", "Volante"));
  TextDim(L("Wheel, pedals, buttons, force feedback and SimHub telemetry are in the Wheel tab.",
            "Volante, pedales, botones, force feedback y telemetría para SimHub están en la pestaña Volante."));
  if (ImGui::Button(L("Open the Wheel tab", "Abrir la pestaña Volante"))) app.tab = Tab::kWheel;
  EndCard();

  BeginCard("##pad", L("Controller", "Mando"));
  if (BeginRows("##p")) {
    Row(L("Input system", "Sistema de entrada"));
    changed |= Segmented<std::string>("backend", s.input_backend, {{"sdl", "SDL"}, {"xinput", "XInput"}});
    EndRows();
  }
  EndCard();

  BeginCard("##kb", L("Keyboard and mouse", "Teclado y ratón"));
  if (BeginRows("##k")) {
    Row(L("Use the keyboard as a controller", "Usar el teclado como mando"));
    changed |= Toggle("##mnk", &s.mnk_mode);
    if (s.mnk_mode) {
      Row(L("Mouse as right stick", "Ratón como stick derecho"));
      changed |= Toggle("##mouse", &s.mnk_mouse);
      if (s.mnk_mouse) {
        Row(L("Mouse sensitivity", "Sensibilidad del ratón"));
        float sens = float(s.mnk_sensitivity);
        if (ImGui::SliderFloat("##sens", &sens, 0.1f, 5.0f, "%.1f")) {
          s.mnk_sensitivity = sens;
          changed = true;
        }
      }
    }
    EndRows();
  }
  EndCard();

  // One card of key bindings: the keyboard pad's, or the camera's.
  auto bind_rows = [&](bool camera) {
    if (app.capture_index >= 0 && s.binds[app.capture_index].camera == camera) {
      TextColored(kAccent, L("Press a key to assign it  ·  Esc cancels", "Pulsa una tecla para asignarla  ·  Esc cancela"));
    }
    const char* group = "";
    for (int i = 0; i < int(s.binds.size()); ++i) {
      auto& b = s.binds[i];
      if (b.camera != camera) continue;
      if (std::strcmp(group, b.group) != 0) {
        group = b.group;
        ImGui::Dummy(ImVec2(0, S(6)));
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
        ImGui::PushFont(g_bold, ImGui::GetStyle().FontSizeBase * 0.9f);
        ImGui::TextUnformatted(L(b.group, b.group_es));
        ImGui::PopFont();
        ImGui::PopStyleColor();
      }
      ImGui::PushID(i);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(L(b.label, b.label_es));
      ImGui::SameLine(S(230));
      if (app.capture_index == i) {
        Chip("...", kAccent);
      } else if (b.keys.empty()) {
        Chip(L("unassigned", "sin asignar"), kControl, kDim);
      } else {
        size_t start = 0;
        bool first = true;
        while (start <= b.keys.size()) {
          const size_t comma = b.keys.find(',', start);
          const std::string key = b.keys.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
          if (!first) ImGui::SameLine(0, S(6));
          Chip(KeyLabel(key).c_str(), kControl);
          first = false;
          if (comma == std::string::npos) break;
          start = comma + 1;
        }
      }
      const float buttons_w = S(250);
      ImGui::SameLine(ImGui::GetContentRegionMax().x - buttons_w);
      if (ImGui::Button(L("Change", "Cambiar"), ImVec2(S(80), 0))) app.capture_index = i, app.capture_append = false;
      ImGui::SameLine(0, S(4));
      if (ImGui::Button(L("Add", "Añadir"), ImVec2(S(60), 0))) app.capture_index = i, app.capture_append = true;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L("Add another key for this", "Añade otra tecla para esto"));
      ImGui::SameLine(0, S(4));
      if (ImGui::Button(L("Clear", "Quitar"), ImVec2(S(64), 0))) b.keys.clear(), changed = true;
      ImGui::SameLine(0, S(4));
      if (ImGui::Button("Reset", ImVec2(S(60), 0))) b.keys = b.default_keys, changed = true;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip(L("Default key: %s", "Tecla por defecto: %s"), b.default_keys);
      ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, S(8)));
    if (ImGui::Button(L("Restore all keys", "Restaurar todas las teclas"))) {
      s.ResetBinds(camera);
      changed = true;
    }
  };

  if (s.mnk_mode) {
    BeginCard("##binds", L("Key bindings", "Asignación de teclas"));
    bind_rows(false);
    EndCard();
  }

  // The camera keys work in game with or without the keyboard pad; the wheel's
  // buttons for the same are in the Wheel tab.
  BeginCard("##camerakeys", L("Camera", "Cámara"),
            L("In game, change the field of view and move the seat; hold a key to keep going. Each camera view keeps its own, saved as you go.",
              "En el juego, cambia el campo de visión y mueve el asiento; mantén la tecla para seguir. Cada vista de cámara guarda los suyos."));
  bind_rows(true);
  const fs::path camera_file = app.exe_dir / "fm4.camera.toml";
  std::error_code ec;
  const bool saved = fs::exists(camera_file, ec);
  ImGui::SameLine();
  ImGui::BeginDisabled(!saved || app.game != nullptr);
  if (ImGui::Button(L("Reset all camera views", "Restablecer todas las vistas"))) fs::remove(camera_file, ec);
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("%s", app.game ? L("Close the game first", "Cierra el juego primero")
                                     : L("Back to the game's own field of view and seat in every view", "Vuelve al campo de visión y asiento del juego en todas las vistas"));
  EndCard();
  if (changed) app.Save();
}

void TabAudio(App& app) {
  Settings& s = app.settings;
  bool changed = false;
  BeginCard("##lang", L("Language", "Idioma"));
  if (BeginRows("##l")) {
    Row(L("Game language", "Idioma del juego"));
    changed |= Combo<int>("##language", s.user_language,
                          {{5, "Español"},
                           {1, "English"},
                           {4, "Français"},
                           {3, "Deutsch"},
                           {6, "Italiano"},
                           {11, "Polski"},
                           {12, L("Russian", "Ruso")},
                           {2, L("Japanese", "Japonés")},
                           {7, L("Korean", "Coreano")},
                           {8, L("Chinese", "Chino")}});
    EndRows();
  }
  EndCard();
  BeginCard("##sound", L("Sound", "Sonido"));
  if (BeginRows("##s")) {
    Row(L("Output mode", "Modo de salida"));
    changed |= Combo<std::string>("##audio_mode", s.audio_output_mode,
        {{"auto", L("Automatic", "Automatico")}, {"stereo", L("Stereo", "Estereo")},
         {"surround", "5.1 Surround (PCM)"}});
    Row(L("Mute the game", "Silenciar el juego"));
    changed |= Toggle("##mute", &s.audio_mute);
    EndRows();
  }
  if (ImGui::Button(L("Windows sound / Dolby...", "Sonido de Windows / Dolby...")))
    SDL_OpenURL("ms-settings:sound");
  TextDim(L("Dolby is configured in Windows for compatible devices. Restart the game after changing output mode.",
      "Dolby se configura en Windows para dispositivos compatibles. Reinicia el juego al cambiar el modo de salida."));
  EndCard();
  if (changed) app.Save();
}

void CardTask(App& app);

// Opens a .zip picker; the result lands in app.*slot (guarded by dialog_mutex).
// Progress / result of the running or last task.
void CardTask(App& app) {
  const auto task_state = app.task.state.load();
  if (task_state == TaskProgress::State::kIdle) return;
  BeginCard("##task", app.task_title.c_str());
  if (task_state == TaskProgress::State::kRunning) {
    const uint64_t total = app.task.total, done = app.task.done;
    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%.1f / %.1f GB", double(done) / 1e9, double(total) / 1e9);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kAccent);
    ImGui::ProgressBar(total ? float(double(done) / double(total)) : 0.0f, ImVec2(-1, S(10)), "");
    ImGui::PopStyleColor();
    ImGui::Text("%s", total ? overlay : L("Preparing...", "Preparando..."));
    TextDim(app.task.Status().c_str());
    if (app.task_kind!=App::TaskKind::kRemoveForzaPlus && ImGui::Button(L("Cancel", "Cancelar"))) app.task.cancel = true;
  } else {
    TextColored(task_state == TaskProgress::State::kDone        ? kOk
                : task_state == TaskProgress::State::kCancelled ? kWarn
                                                                : kBad,
                app.task.Status().c_str());
  }
  EndCard();
}

// Opens the .iso picker; the install task starts from the main loop.
void PickIso(App& app) {
  // SDL reads the filters until the callback runs, so they must outlive this frame.
  static const SDL_DialogFileFilter filters_en[] = {{"Xbox 360 disc image", "iso"}, {"All files", "*"}};
  static const SDL_DialogFileFilter filters_es[] = {{"Imagen de disco de Xbox 360", "iso"},
                                                    {"Todos los archivos", "*"}};
  const SDL_DialogFileFilter* filters = g_spanish ? filters_es : filters_en;
  SDL_ShowOpenFileDialog(
      [](void* user, const char* const* files, int) {
        auto* a = static_cast<App*>(user);
        if (files && files[0]) {
          std::lock_guard lock(a->dialog_mutex);
          a->picked_iso = files[0];
        }
      },
      &app, app.window, filters, 2, nullptr, false);
}

void PickFolder(App& app, std::optional<std::string> App::*slot) {
  struct Request {
    App* app;
    std::optional<std::string> App::*slot;
  };
  SDL_ShowOpenFolderDialog(
      [](void* user, const char* const* files, int) {
        std::unique_ptr<Request> request(static_cast<Request*>(user));
        if (files && files[0]) {
          std::lock_guard lock(request->app->dialog_mutex);
          request->app->*(request->slot) = files[0];
        }
      },
      new Request{&app, slot}, app.window, nullptr, false);
}

void PickPlusZip(App& app, bool update) {
  struct Request { App* app; bool update; };
  static const SDL_DialogFileFilter filter[] = {{"Project Forza Plus 4 ZIP", "zip"}};
  SDL_ShowOpenFileDialog([](void* data, const char* const* files, int) {
    std::unique_ptr<Request> r(static_cast<Request*>(data));
    if (files && files[0]) {
      std::lock_guard lock(r->app->dialog_mutex);
      (r->update ? r->app->picked_plus_update : r->app->picked_plus_zip) = files[0];
    }
  }, new Request{&app, update}, app.window, filter, 1, nullptr, false);
}

// One step of the install wizard: number, title, state and its button.
void WizardStep(App& app, int number, const char* title, const char* subtitle, bool done, bool enabled,
                const char* state_text, const char* button) {
  const std::string id = "##step" + std::to_string(number);
  BeginCard(id.c_str(), title, subtitle);
  StatusDot(done ? kOk : (enabled ? kWarn : kDim));
  ImGui::TextUnformatted(state_text);
  ImGui::Dummy(ImVec2(0, S(2)));
  ImGui::BeginDisabled(!enabled || app.TaskRunning() || app.game || app.PlusActive());
  ImGui::PushID(number);
  if (done ? ImGui::Button(button, ImVec2(S(280), S(36))) : PrimaryButton(button, ImVec2(S(280), S(40)))) {
    PickIso(app);
  }
  ImGui::PopID();
  ImGui::EndDisabled();
  EndCard();
}

void TabInstall(App& app) {
  CardTask(app);

  // Step 1: Disc 1 (Play Disc) -> game folder.
  const char* disc1_state = "";
  switch (app.game_state) {
    case App::GameState::kVerified:
    case App::GameState::kUnverified: disc1_state = L("Installed", "Instalado"); break;
    case App::GameState::kIncomplete: disc1_state = L("Incomplete install", "Instalación incompleta"); break;
    default: disc1_state = L("Not installed", "No instalado"); break;
  }
  WizardStep(app, 1, L("Step 1 · Disc 1 (Play Disc)", "Paso 1 · Disco 1 (Play Disc)"),
             L("Pick the disc image (.iso) of Disc 1 of your Forza Motorsport 4 USA Rev 1. About "
               "8 GB are extracted and every file is checked.",
               "Elige la imagen de disco (.iso) del Disco 1 de tu Forza Motorsport 4 USA Rev 1. Se "
               "extraen unos 8 GB y se comprueba cada archivo."),
             app.Installed(), true, disc1_state,
             app.Installed() ? L("Reinstall Disc 1...", "Reinstalar Disco 1...")
                             : L("Choose Disc 1 ISO and install", "Elegir ISO del Disco 1 e instalar"));

  const bool content_done = app.ContentInstalled();
  char content_state[128];
  std::snprintf(content_state, sizeof(content_state),
      L("%zu of %zu content packs installed", "%zu de %zu packs de contenido instalados"),
      app.content_packages, kContentPackages);
  WizardStep(app, 2, L("Step 2 - Disc 2 (Content Install Disc)", "Paso 2 - Disco 2 (Content Install Disc)"),
      L("Install the four car and track packs (about 3 GB).", "Instala los cuatro packs de coches y circuitos (unos 3 GB)."),
      content_done, app.Installed(),
      app.Installed() ? content_state : L("Install Disc 1 first", "Instala antes el Disco 1"),
      content_done ? L("Reinstall Disc 2...", "Reinstalar Disco 2...")
                   : L("Choose Disc 2 ISO and install", "Elegir ISO del Disco 2 e instalar"));

  if (!UserDirFitsContent(app.UserDir())) {
    TextColored(kWarn, L("The user data folder path is too long for the Disc 2 files: choose a shorter one below.",
                         "La ruta de la carpeta de datos de usuario es demasiado larga para los archivos del "
                         "Disco 2: elige una más corta abajo."));
    ImGui::Dummy(ImVec2(0, S(6)));
  }
  BeginCard("##forzaplus", L("Step 3 - Project Forza Plus 4 (optional)", "Paso 3 - Project Forza Plus 4 (opcional)"),
      L("Separate installation and saves. Requires extra disk space for a copy of Disc 1 and installed content. Native data adaptation; gameplay compatibility is still being tested.",
        "Instalacion y partidas separadas. Requiere espacio adicional para copiar el Disco 1 y el contenido instalado. Adaptacion de datos al port; compatibilidad en pruebas."));
  ImGui::BeginDisabled(app.TaskRunning() || app.game);
  const auto base_game = app.BaseGameDir(), base_user = app.BaseUserDir();
  const bool ready = fs::exists(base_game / ".fm4-plus/.fm4-plus-ready") && fs::exists(base_user / ".fm4-plus/.fm4-plus-ready");
  ImGui::BeginDisabled(ready);
  if (ImGui::Button(L("Choose base ZIP...", "Elegir ZIP base..."))) PickPlusZip(app, false);
  if (!app.plus_zip.empty()) TextDim(fs::u8path(app.plus_zip).filename().u8string().c_str());
  if (ImGui::Button(L("Choose update ZIP...", "Elegir ZIP de actualizacion..."))) PickPlusZip(app, true);
  ImGui::SameLine();
  if (ImGui::SmallButton(L("Clear update", "Quitar actualizacion"))) app.plus_update.clear();
  if (!app.plus_update.empty()) TextDim(fs::u8path(app.plus_update).filename().u8string().c_str());
  const char* modes[] = {"Normal", "Hard", "Sandbox", "Vanilla"};
  ImGui::Combo(L("Career mode", "Modo de carrera"), &app.plus_mode, modes, 4);
  ImGui::EndDisabled();
  TextDim(L("Only DLC packages already installed with their headers are modified. The original executable and the port's render settings are preserved.",
      "Solo se modifican los DLC ya instalados con sus cabeceras. Se conservan el ejecutable original y los ajustes de renderizado del port."));
  if (ready) {
    std::ifstream info(base_game / ".fm4-plus/.fm4-plus-ready");
    std::string mode; std::getline(info, mode);
    ImGui::Text("%s: %s", L("Installed", "Instalado"), mode.c_str());
  }
  ImGui::BeginDisabled(app.plus_zip.empty() || !app.Installed() || app.PlusActive() || ready);
  if (ImGui::Button(L("Install Forza Plus", "Instalar Forza Plus"))) {
    const auto zip = fs::u8path(app.plus_zip), update = fs::u8path(app.plus_update);
    const std::string mode = modes[app.plus_mode];
    app.StartTask("Project Forza Plus 4", App::TaskKind::kForzaPlus,
        [&app, zip, update, base_game, base_user, mode] {
          if (!app.RunVerify(base_game)) return false;
          app.task.done = 0; app.task.total = 0;
          return InstallForzaPlus(app.exe_dir / "install-forza-plus.ps1", zip, update,
                                  base_game, base_user, mode, app.task);
        });
  }
  ImGui::EndDisabled();
  if (ready) {
    ImGui::SameLine();
    if (ImGui::Button(app.PlusActive() ? L("Use original game", "Usar juego original") : L("Use Forza Plus", "Usar Forza Plus")))
      app.SelectPlus(!app.PlusActive());
    if(ImGui::Button(L("Uninstall Forza Plus", "Desinstalar Forza Plus"))) {
      if(app.PlusActive()) app.SelectPlus(false);
      app.StartTask(L("Uninstall Forza Plus", "Desinstalar Forza Plus"),App::TaskKind::kRemoveForzaPlus,
          [&app,base_game,base_user] {return UninstallForzaPlus(app.exe_dir/"uninstall-forza-plus.ps1",base_game,base_user,app.task);});
    }
    ImGui::TextWrapped("%s",L("Uninstall preserves saves and the original game. Reinstall afterwards to choose another career mode.",
        "Desinstalar conserva las partidas y el juego original. Despues puedes reinstalar eligiendo otro modo de carrera."));
  }
  TextDim(app.PlusActive() ? L("Active: Forza Plus (separate saves)", "Activo: Forza Plus (partidas separadas)")
                          : L("Active: original game", "Activo: juego original"));
  ImGui::EndDisabled();
  EndCard();
  BeginCard("##folders", L("Folders", "Carpetas"),
            L("Game files (Disc 1) and user data (saves, Disc 2 content). By default, \"game\" and \"user\" "
              "next to the launcher.",
              "Archivos del juego (Disco 1) y datos de usuario (partidas, contenido del Disco 2). Por defecto, "
              "\"game\" y \"user\" junto al launcher."));
  ImGui::BeginDisabled(app.TaskRunning() || app.game);
  if (BeginRows("##f")) {
    Row(L("Game", "Juego"));
    std::string dir = app.GameDir().u8string();
    ImGui::InputText("##gamedir", dir.data(), dir.size() + 1, ImGuiInputTextFlags_ReadOnly);
    Row(L("User data", "Datos de usuario"));
    std::string udir = app.UserDir().u8string();
    ImGui::InputText("##userdir", udir.data(), udir.size() + 1, ImGuiInputTextFlags_ReadOnly);
    EndRows();
  }
  ImGui::BeginDisabled(app.PlusActive());
  if (ImGui::Button(L("Change game folder", "Cambiar carpeta del juego"), ImVec2(S(220), 0))) {
    PickFolder(app, &App::picked_game_folder);
  }
  ImGui::SameLine();
  if (ImGui::Button(L("Change user folder", "Cambiar carpeta de usuario"), ImVec2(S(220), 0))) {
    PickFolder(app, &App::picked_user_folder);
  }
  ImGui::EndDisabled();
  if (app.Installed()) {
    ImGui::SameLine();
    if (ImGui::Button(L("Verify files", "Verificar archivos"), ImVec2(S(170), 0))) {
      const fs::path game_dir = app.GameDir();
      app.StartTask(L("Verification", "Verificación"), App::TaskKind::kVerify,
                    [&app, game_dir] { return app.RunVerify(game_dir); });
    }
  }
  ImGui::EndDisabled();
  EndCard();
}

constexpr const char* kKofiUrl = "https://ko-fi.com/post/Forza-Motorsport-4-on-PC-Native-Port-Moza-R3-I3B028W026";

void TabSupport() {
  BeginCard("##support", L("Support the project", "Apoya el proyecto"));
  TextDim(L("These ports are free projects made in my spare time. If you enjoy playing Forza Motorsport "
            "natively on PC and want it to keep improving, you can support it on Ko-fi. Every "
            "contribution helps me put more hours into it.",
            "Estos ports son proyectos gratuitos hechos en mi tiempo libre. Si disfrutas jugando a Forza "
            "Motorsport nativo en PC y quieres que siga mejorando, puedes apoyarlo en Ko-fi. Cada "
            "aportación me ayuda a dedicarle más horas."));
  ImGui::Dummy(ImVec2(0, S(8)));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(10));
  ImGui::PushFont(g_bold, ImGui::GetStyle().FontSizeBase * 1.15f);
  if (PrimaryButton(L("Support on Ko-fi", "Apoyar en Ko-fi"), ImVec2(S(260), S(48)))) {
    SDL_OpenURL(kKofiUrl);
  }
  ImGui::PopFont();
  ImGui::PopStyleVar();
  ImGui::Dummy(ImVec2(0, S(4)));
  TextDim(kKofiUrl);
  ImGui::Dummy(ImVec2(0, S(4)));
  TextDim(L("Thank you!", "¡Gracias!"));
  EndCard();
}

// Underlined text tabs across the top of the settings column.
void TabBar(App& app) {
  const std::pair<Tab, const char*> tabs[] = {{Tab::kInstructions, L("Instructions", "Instrucciones")},
                                              {Tab::kProfiles, L("Profiles", "Perfiles")},
                                              {Tab::kGraphics, L("Graphics", "Gráficos")},
                                              {Tab::kControls, L("Controls", "Controles")},
                                              {Tab::kWheel, L("Wheel", "Volante")},
                                              {Tab::kAudio, L("Language & sound", "Idioma y sonido")},
                                              {Tab::kInstall, L("Install", "Instalación")},
                                              {Tab::kSupport, L("Support", "Apoyar")}};
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImGui::PushFont(g_bold, ImGui::GetStyle().FontSizeBase * 1.05f);
  for (const auto& [tab, label] : tabs) {
    const bool active = app.tab == tab;
    const ImVec2 sz = ImGui::CalcTextSize(label);
    if(ImGui::GetCursorPosX()+sz.x+S(8)>ImGui::GetContentRegionMax().x-S(100)) ImGui::NewLine();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    if (ImGui::InvisibleButton("tab", ImVec2(sz.x + S(8), sz.y + S(16)))) {
      app.tab = tab;
      app.capture_index = -1;
    }
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    dl->AddText(ImVec2(p.x + S(4), p.y + S(4)), Col(active || hovered ? kText : kDim), label);
    if (active) {
      dl->AddRectFilled(ImVec2(p.x + S(4), p.y + sz.y + S(10)), ImVec2(p.x + sz.x + S(4), p.y + sz.y + S(13)),
                        Col(kAccent), S(2));
    }
    ImGui::SameLine(0, S(8));
  }
  ImGui::PopFont();

  // Launcher language, right-aligned: EN | ES.
  const float pill_w = S(42);
  ImGui::SameLine(ImGui::GetContentRegionMax().x - pill_w * 2 - S(4));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(7));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(4), 0));
  for (const auto& [spanish, code] : {std::pair{false, "EN"}, std::pair{true, "ES"}}) {
    const bool active = g_spanish == spanish;
    ImGui::PushStyleColor(ImGuiCol_Button, active ? kAccent : kControl);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? kAccentHover : ImVec4(0.22f, 0.23f, 0.26f, 1));
    if (ImGui::Button(code, ImVec2(pill_w, 0)) && !active) {
      g_spanish = spanish;
      app.state.spanish = spanish;
      app.Save();
      app.CheckGame();  // status texts are localized
    }
    ImGui::PopStyleColor(2);
    if (!spanish) ImGui::SameLine();
  }
  ImGui::PopStyleVar(2);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  dl->AddLine(ImVec2(p.x, p.y - S(2)), ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y - S(2)),
              Col(ImVec4(1, 1, 1, 0.07f)));
  ImGui::Dummy(ImVec2(0, S(10)));
}

void ApplyStyle() {
  ImGuiStyle& style = ImGui::GetStyle();
  ImGui::StyleColorsDark();
  style.WindowRounding = 0;
  style.ChildRounding = S(12);
  style.FrameRounding = S(8);
  style.PopupRounding = S(10);
  style.GrabRounding = S(8);
  style.ScrollbarRounding = S(8);
  style.TabRounding = S(8);
  style.FramePadding = ImVec2(S(12), S(8));
  style.ItemSpacing = ImVec2(S(10), S(10));
  style.WindowPadding = ImVec2(S(0), S(0));
  style.ScrollbarSize = S(10);
  style.WindowBorderSize = 0;
  style.ChildBorderSize = 0;
  style.PopupBorderSize = 0;
  style.FrameBorderSize = 0;
  ImVec4* c = style.Colors;
  c[ImGuiCol_Text] = kText;
  c[ImGuiCol_TextDisabled] = kDim;
  c[ImGuiCol_WindowBg] = kBg;
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.125f, 0.14f, 0.98f);
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f);
  c[ImGuiCol_FrameBg] = kControl;
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.21f, 0.22f, 0.25f, 1);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.25f, 0.28f, 1);
  c[ImGuiCol_Button] = kControl;
  c[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.23f, 0.26f, 1);
  c[ImGuiCol_ButtonActive] = kAccent;
  c[ImGuiCol_Header] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.30f);
  c[ImGuiCol_HeaderHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.45f);
  c[ImGuiCol_HeaderActive] = kAccent;
  c[ImGuiCol_CheckMark] = kAccent;
  c[ImGuiCol_SliderGrab] = kAccent;
  c[ImGuiCol_SliderGrabActive] = kAccentHover;
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = ImVec4(1, 1, 1, 0.12f);
  c[ImGuiCol_Separator] = ImVec4(1, 1, 1, 0.07f);
  c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] = ImVec4(0, 0, 0, 0);
}

// Headless mode for scripts and tests:
//   fm4-launcher --import-iso <iso> --game-dir <dir> --user-dir <dir> [--log <file>]
//     Disc 1: extract + full verification. Disc 2: content install (needs Disc 1
//     in --game-dir). The disc is detected from the image.
//   fm4-launcher --verify --game-dir <dir> [--log <file>]
//                --game-dir <dir> --user-dir <dir> [--log <file>]
// Same code paths as the UI; prints progress to the log, exit code 0 = ok.
int RunHeadless(int argc, char** argv) {
  std::string iso, dir, user_dir, log_path;
  bool verify_only = false;
  int race_shadows = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--import-iso" && i + 1 < argc) iso = argv[++i];
    else if (a == "--game-dir" && i + 1 < argc) dir = argv[++i];
    else if (a == "--user-dir" && i + 1 < argc) user_dir = argv[++i];
    else if (a == "--log" && i + 1 < argc) log_path = argv[++i];
    else if (a == "--verify") verify_only = true;
    else if (a == "--race-shadows" && i + 1 < argc) {
      const std::string mode = argv[++i];
      if (mode != "on" && mode != "off") return 2;
      race_shadows = mode == "on" ? 1 : -1;
    }
  }
  std::ofstream log(log_path.empty() ? "launcher-headless.log" : log_path);
  const fs::path exe_dir = fs::u8path(SDL_GetBasePath());
  const auto manifest = LoadManifest(exe_dir / "game_files.sha256");
  if (manifest.empty() || dir.empty()) {
    log << "error: missing game_files.sha256 or --game-dir\n";
    return 2;
  }
  if (race_shadows) {
    const auto error = SetRaceShadowWorkaround(fs::u8path(dir), race_shadows > 0);
    log << "race-shadows: " << (error.empty() ? "ok" : error) << std::endl;
    return error.empty() ? 0 : 1;
  }
  TaskProgress progress;
  std::atomic<bool> finished{false};
  std::thread reporter([&] {
    while (!finished) {
      std::this_thread::sleep_for(std::chrono::seconds(5));
      log << progress.done / 1000000 << "/" << progress.total / 1000000 << " MB  " << progress.Status()
          << std::endl;
    }
  });
  const auto start = std::chrono::steady_clock::now();
  bool ok = true;
  bool verify = verify_only;
  if (!verify_only) {
    const DiscKind kind = IdentifyIso(fs::u8path(iso));
    log << "disc: " << (kind == DiscKind::kPlay ? "play" : kind == DiscKind::kContent ? "content" : "unknown")
        << std::endl;
    if (kind == DiscKind::kPlay) {
      ok = ExtractPlayDisc(fs::u8path(iso), fs::u8path(dir), progress);
      verify = ok;
      log << "extract: " << (ok ? "ok" : "FAILED") << " - " << progress.Status() << std::endl;
    } else if (kind == DiscKind::kContent) {
      if (user_dir.empty()) {
        log << "error: --user-dir is required for Disc 2\n";
        ok = false;
      } else {
        ok = InstallContentDisc(fs::u8path(iso), fs::u8path(dir), fs::u8path(user_dir),
                                exe_dir / "fm4tool.exe", progress);
        log << "content: " << (ok ? "ok" : "FAILED") << " - " << progress.Status() << std::endl;
        log << "packages: " << CountContentPackages(fs::u8path(user_dir)) << "/" << kContentPackages << std::endl;
      }
    } else {
      ok = false;
    }
  }
  if (ok && verify) {
    App verifier;
    verifier.manifest = manifest;
    verifier.settings.game_data_root = dir;
    verifier.settings.user_data_root = user_dir;
    const VerifyReport report = VerifyGame(
        fs::u8path(dir), verifier.VerifyManifest(fs::u8path(dir)), true,
        progress);
    ok = report.ok();
    log << "verify: " << (ok ? "ok" : "FAILED") << " missing=" << report.missing.size()
        << " size=" << report.wrong_size.size() << " hash=" << report.wrong_hash.size() << std::endl;
    for (const auto* list : {&report.missing, &report.wrong_size, &report.wrong_hash}) {
      for (const auto& p : *list) log << "  " << p << "\n";
    }
    if (fs::exists(fs::u8path(dir) / ".fm4-plus-ready")) {
      const auto receipt = fs::u8path(user_dir) / ".fm4-plus.sha256";
      const auto dlc = LoadManifest(receipt);
      const auto content = VerifyGame(fs::u8path(user_dir), dlc, true, progress);
      if (user_dir.empty() || !fs::exists(receipt) || !content.ok()) ok = false;
      log << "Forza Plus content: " << (content.ok() ? "ok" : "FAILED") << " entries=" << dlc.size() << std::endl;
    }
    if (ok) {
      const auto fix_error = SetRaceShadowWorkaround(fs::u8path(dir), true);
      if (!fix_error.empty()) { log << "compatibility: " << fix_error << std::endl; ok = false; }
    }
    if (ok) std::ofstream(VerifiedMarker(fs::u8path(dir))) << "verified against game_files.sha256\n";
  }
  finished = true;
  reporter.join();
  log << "seconds: "
      << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count()
      << std::endl;
  return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  fm4::InstallCrashHandler(fs::u8path(SDL_GetBasePath()) / "logs");
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--import-iso" || a == "--verify" || a == "--race-shadows") {
      return RunHeadless(argc, argv);
    }
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "FM4 Launcher", SDL_GetError(), nullptr);
    return 1;
  }
  App app;
  app.exe_dir = fs::u8path(SDL_GetBasePath());
  app.user_config = app.exe_dir / "fm4.user.toml";
  app.port_config = app.exe_dir / "fm4.toml";
  app.launcher_config = app.exe_dir / "launcher.toml";
  app.manifest_path = app.exe_dir / "game_files.sha256";
  app.game_exe = app.exe_dir / "fm4.exe";
  app.tool_exe = app.exe_dir / "fm4tool.exe";
  app.cover_path = app.exe_dir / "cover.jpg";
  app.Load();
  app.manifest = LoadManifest(app.manifest_path);
  if (app.manifest.empty()) {
    app.notice = L("game_files.sha256 is missing next to the launcher: the game cannot be verified.",
                   "Falta game_files.sha256 junto al launcher: no se puede verificar el juego.");
  }
  app.CheckGame();
  if (!app.Installed()) app.tab = Tab::kInstall;

  g_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  if (g_scale <= 0) g_scale = 1.0f;
  app.window = SDL_CreateWindow("Forza Motorsport 4", int(CoverWidth() + SettingsWidth()), int(WindowHeight()),
                                SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  // The SDK's SDL3 is built without the 2D renderer; SDL_GPU (D3D12 on
  // Windows, Vulkan elsewhere) is what it has.
  SDL_GPUDevice* gpu = SDL_CreateGPUDevice(
      SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
  if (!gpu || !SDL_ClaimWindowForGPUDevice(gpu, app.window)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "FM4 Launcher",
                             (std::string(L("Could not start the GPU: ", "No se pudo iniciar la GPU: ")) + SDL_GetError()).c_str(),
                             app.window);
    return 1;
  }
  app.gpu = gpu;
  SDL_SetGPUSwapchainParameters(gpu, app.window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);
  const SDL_GPUTextureFormat swap_format = SDL_GetGPUSwapchainTextureFormat(gpu, app.window);
  if (fs::exists(app.cover_path)) LoadCover(gpu, app.cover_path, app.cover);
  app.reload_cover = false;
  SDL_SetWindowPosition(app.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(app.window);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  ApplyStyle();
  const float font_size = 17.0f * g_scale;
  const char* regular = "C:\\Windows\\Fonts\\segoeui.ttf";
  const char* bold = "C:\\Windows\\Fonts\\seguisb.ttf";
  // Segoe UI lacks arrows and a few symbols (â†º â† â†‘); merge Segoe UI Symbol in.
  const char* symbols = "C:\\Windows\\Fonts\\seguisym.ttf";
  ImFontConfig merge;
  merge.MergeMode = true;
  if (fs::exists(regular)) {
    io.Fonts->AddFontFromFileTTF(regular, font_size);
    if (fs::exists(symbols)) io.Fonts->AddFontFromFileTTF(symbols, font_size, &merge);
  }
  if (fs::exists(bold)) {
    g_bold = io.Fonts->AddFontFromFileTTF(bold, font_size);
    if (fs::exists(symbols)) io.Fonts->AddFontFromFileTTF(symbols, font_size, &merge);
  }
  ImGui::GetStyle().FontSizeBase = font_size;
  ImGui_ImplSDL3_InitForSDLGPU(app.window);
  ImGui_ImplSDLGPU3_InitInfo gpu_info = {};
  gpu_info.Device = gpu;
  gpu_info.ColorTargetFormat = swap_format;
  gpu_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
  ImGui_ImplSDLGPU3_Init(&gpu_info);

  // --screenshot <file.bmp> [--tab profiles|graphics|controls|audio|install]:
  // render a few frames, save the window and exit (docs and UI checks).
  std::string screenshot;
  int screenshot_frame = 4;
  for (int i = 1; i + 1 < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--screenshot") screenshot = argv[i + 1];
    if (a == "--screenshot-frame") screenshot_frame = std::max(4, std::atoi(argv[i + 1]));
    if (a == "--tab") {
      const std::string t = argv[i + 1];
      app.tab = t == "instructions" ? Tab::kInstructions
                : t == "support" ? Tab::kSupport
                : t == "graphics" ? Tab::kGraphics
                : t == "controls" ? Tab::kControls
                : t == "wheel"    ? Tab::kWheel
                : t == "audio"    ? Tab::kAudio
                : t == "install"  ? Tab::kInstall
                                  : Tab::kProfiles;
    }
    if (a == "--lang") {
      g_spanish = std::string(argv[i + 1]) == "es";
    }
  }
  int frames = 0;

  // --play: press PLAY right away (desktop shortcuts straight into the game).
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--play" && app.Installed()) app.LaunchGame();
    // --start-import <iso>: same as picking the ISO in the Install tab (tests).
    if (a == "--start-import" && i + 1 < argc) {
      std::lock_guard lock(app.dialog_mutex);
      app.picked_iso = argv[i + 1];
    }
  }

  bool running = true;
  while (running) {
    const bool capture_now = !screenshot.empty() && ++frames == screenshot_frame;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (app.capture_index >= 0 && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        auto& bind = app.settings.binds[app.capture_index];
        if (event.key.scancode != SDL_SCANCODE_ESCAPE) {
          std::string name = KeyName(event.key.scancode);
          if (!name.empty()) {
            if ((event.key.mod & SDL_KMOD_SHIFT) && name != "Shift") name = "Shift+" + name;
            bind.keys = app.capture_append && !bind.keys.empty() ? bind.keys + "," + name : name;
            app.Save();
          }
        }
        app.capture_index = -1;
        continue;  // don't let ImGui see the captured key
      }
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) running = false;
    }
    if (app.wheel_monitor) {
      app.wheel_monitor->SetActive(
          app.tab == Tab::kWheel && !app.game && (SDL_GetWindowFlags(app.window) & SDL_WINDOW_INPUT_FOCUS) != 0);
      app.wheel_monitor->Pump();
    }
    if (SDL_GetWindowFlags(app.window) & SDL_WINDOW_MINIMIZED) {
      app.PollGame();
      SDL_Delay(100);
      continue;
    }

    // Results from file dialogs.
    {
      std::lock_guard lock(app.dialog_mutex);
      if (app.picked_game_folder) {
        app.settings.game_data_root = *app.picked_game_folder;
        app.picked_game_folder.reset();
        app.Save();
        app.CheckGame();
      }
      if (app.picked_user_folder) {
        app.settings.user_data_root = *app.picked_user_folder;
        app.picked_user_folder.reset();
        app.Save();
        app.CheckGame();
      }

      if (app.picked_iso) {
        const fs::path iso = fs::u8path(*app.picked_iso);
        app.picked_iso.reset();
        app.StartImport(iso);
      }
      if (app.picked_plus_zip) { app.plus_zip = *app.picked_plus_zip; app.picked_plus_zip.reset(); }
      if (app.picked_plus_update) { app.plus_update = *app.picked_plus_update; app.picked_plus_update.reset(); }
    }
    static TaskProgress::State last_state = TaskProgress::State::kIdle;
    if (last_state == TaskProgress::State::kRunning && !app.TaskRunning()) {
      if (app.task_kind == App::TaskKind::kForzaPlus && app.task.state == TaskProgress::State::kDone)
        app.SelectPlus(true);
      app.CheckGame();
    }
    last_state = app.task.state;
    if (app.reload_cover) {
      app.reload_cover = false;
      ReleaseCover(gpu, app.cover);
      LoadCover(gpu, app.cover_path, app.cover);
    }
    app.PollGame();

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoScrollWithMouse);

    // Left: the cover column (fixed width, the cover shown whole).
    const float cover_w = CoverWidth();
    ImGui::BeginChild("##cover", ImVec2(cover_w, vp->Size.y), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    CoverPanel(app, ImVec2(cover_w, vp->Size.y));
    ImGui::EndChild();
    ImGui::SameLine(0, 0);

    // Right: tabs + content.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(34), S(26)));
    ImGui::BeginChild("##settings", ImVec2(0, vp->Size.y), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    TabBar(app);
    if (!app.notice.empty()) {
      TextColored(kWarn, app.notice.c_str());
      ImGui::SameLine();
      if (ImGui::SmallButton(L("Dismiss", "Cerrar"))) app.notice.clear();
    }
    ImGui::BeginChild("##content", ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20), S(16)));
    switch (app.tab) {
      case Tab::kInstructions: TabInstructions(app); break;
      case Tab::kProfiles: TabProfiles(app); break;
      case Tab::kGraphics: TabGraphics(app); break;
      case Tab::kControls: TabControls(app); break;
      case Tab::kWheel: TabWheel(app); break;
      case Tab::kAudio: TabAudio(app); break;
      case Tab::kInstall: TabInstall(app); break;
      case Tab::kSupport: TabSupport(); break;
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::End();

    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu);
    SDL_GPUTexture* swapchain = nullptr;
    Uint32 width = 0, height = 0;
    SDL_WaitAndAcquireGPUSwapchainTexture(cmd, app.window, &swapchain, &width, &height);
    // Screenshot mode draws into an offscreen copy that can be downloaded.
    SDL_GPUTexture* target = swapchain;
    SDL_GPUTexture* offscreen = nullptr;
    if (capture_now && swapchain) {
      SDL_GPUTextureCreateInfo info = {};
      info.type = SDL_GPU_TEXTURETYPE_2D;
      info.format = swap_format;
      info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
      info.width = width;
      info.height = height;
      info.layer_count_or_depth = 1;
      info.num_levels = 1;
      offscreen = SDL_CreateGPUTexture(gpu, &info);
      target = offscreen;
    }
    if (target && draw_data->DisplaySize.x > 0) {
      ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, cmd);
      SDL_GPUColorTargetInfo color = {};
      color.texture = target;
      color.clear_color = SDL_FColor{kBg.x, kBg.y, kBg.z, 1.0f};
      color.load_op = SDL_GPU_LOADOP_CLEAR;
      color.store_op = SDL_GPU_STOREOP_STORE;
      SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color, 1, nullptr);
      ImGui_ImplSDLGPU3_RenderDrawData(draw_data, cmd, pass);
      SDL_EndGPURenderPass(pass);
    }
    if (offscreen) {
      SDL_GPUTransferBufferCreateInfo tb_info = {};
      tb_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
      tb_info.size = width * height * 4;
      SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(gpu, &tb_info);
      SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
      SDL_GPUTextureRegion region = {};
      region.texture = offscreen;
      region.w = width;
      region.h = height;
      region.d = 1;
      SDL_GPUTextureTransferInfo dst = {};
      dst.transfer_buffer = tb;
      SDL_DownloadFromGPUTexture(copy, &region, &dst);
      SDL_EndGPUCopyPass(copy);
      SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
      SDL_WaitForGPUFences(gpu, true, &fence, 1);
      SDL_ReleaseGPUFence(gpu, fence);
      void* pixels = SDL_MapGPUTransferBuffer(gpu, tb, false);
      SDL_Surface* shot = SDL_CreateSurfaceFrom(int(width), int(height),
                                                SDL_GetPixelFormatFromGPUTextureFormat(swap_format), pixels,
                                                int(width * 4));
      if (!shot || !SDL_SaveBMP(shot, screenshot.c_str())) {
        std::ofstream(screenshot + ".txt") << "screenshot failed: " << SDL_GetError() << "\n";
      }
      SDL_DestroySurface(shot);
      SDL_UnmapGPUTransferBuffer(gpu, tb);
      SDL_ReleaseGPUTransferBuffer(gpu, tb);
      SDL_ReleaseGPUTexture(gpu, offscreen);
      running = false;
    } else {
      SDL_SubmitGPUCommandBuffer(cmd);
    }
  }

  app.task.cancel = true;
  app.wheel_monitor.reset();
  if (app.worker.joinable()) app.worker.join();
  SDL_WaitForGPUIdle(gpu);
  ImGui_ImplSDL3_Shutdown();
  ImGui_ImplSDLGPU3_Shutdown();
  ImGui::DestroyContext();
  ReleaseCover(gpu, app.cover);
  SDL_ReleaseWindowFromGPUDevice(gpu, app.window);
  SDL_DestroyGPUDevice(gpu);
  SDL_DestroyWindow(app.window);
  SDL_Quit();
  return 0;
}






