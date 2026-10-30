// The in-game camera, adjusted live: a narrower or wider field of view and the
// seat (the eye) moved up, down, forward or back, from keys (keybind_camera_*)
// and wheel buttons (CAM_* in wheel_buttons). Each camera view keeps its own
// adjustments, saved to fm4.camera.toml next to the exe as they change.
//
// The main view's setup (sub_822CA758, see wide_view.cpp) reads its camera
// from a description (r31 + 4): sub_82390070 returns its camera-to-world
// matrix (+0xC0: right, up, the view axis, position; the projection builder
// sub_822C0BA8 inverts it), sub_822EC390 its vertical field of view in
// radians (+0x100), and +8 tells the views apart. Both are read twice, for
// the camera (vtable 0x78 and 0x30) and for the culling view; the hooks change
// what those two callers get, the 1280x720 main viewport only, so the game's
// own camera is untouched.
//
// Seat forward is the matrix axis (a row or a column: which is not certain)
// that stays in line with the car's motion while driving, learned and saved.
#include "camera_view.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include "generated/default/fm4_pch.h"
#include "wheel_input.h"
#include "wheel_mapping.h"

REXCVAR_DEFINE_BOOL(camera_adjust, true, "FM4/Camera",
                    "Field of view and seat position keys (keybind_camera_*) and wheel buttons (CAM_*)");
REXCVAR_DEFINE_STRING(keybind_camera_fov_in, "Plus,NumpadPlus", "FM4/Camera", "Narrower field of view");
REXCVAR_DEFINE_STRING(keybind_camera_fov_out, "Minus,NumpadMinus", "FM4/Camera", "Wider field of view");
REXCVAR_DEFINE_STRING(keybind_camera_fov_reset, "0,Numpad0", "FM4/Camera", "The game's field of view");
REXCVAR_DEFINE_STRING(keybind_camera_seat_up, "PageUp", "FM4/Camera", "Seat up");
REXCVAR_DEFINE_STRING(keybind_camera_seat_down, "PageDown", "FM4/Camera", "Seat down");
REXCVAR_DEFINE_STRING(keybind_camera_seat_forward, "Home", "FM4/Camera", "Seat forward");
REXCVAR_DEFINE_STRING(keybind_camera_seat_back, "End", "FM4/Camera", "Seat back");
REXCVAR_DEFINE_STRING(keybind_camera_seat_reset, "Insert", "FM4/Camera", "The game's seat position");
REXCVAR_DEFINE_STRING(camera_views, "", "FM4/Camera",
                      "Adjustments per camera view, view=fov degrees,height m,forward m;... "
                      "(written to fm4.camera.toml by the game)");
REXCVAR_DEFINE_INT32(camera_view_axis, 0, "FM4/Camera",
                     "The seat's forward axis in the camera matrix: 1 row 0, 2 row 2, 3 column 0, "
                     "4 column 2, negative backwards; 0 learns it from the car's motion");

namespace fm4::camera {
namespace {
using Clock = std::chrono::steady_clock;
using wheel::CameraAction;
constexpr size_t kActions = size_t(CameraAction::kCount);

// The steps and ranges either side of the game's own camera.
constexpr float kFovStep = 1.0f, kFovRange = 30.0f, kFovMin = 10.0f, kFovMax = 120.0f;
constexpr float kSeatStep = 0.01f, kHeightRange = 0.30f, kForwardRange = 0.50f;
constexpr auto kRepeatDelay = std::chrono::milliseconds(350), kRepeatEvery = std::chrono::milliseconds(50);

// --- Keys ---------------------------------------------------------------------

std::array<std::atomic<bool>, 256> g_key_down{};

class KeyListener final : public rex::ui::WindowInputListener {
 public:
  void OnKeyDown(rex::ui::KeyEvent& e) override { Set(e, true); }
  void OnKeyUp(rex::ui::KeyEvent& e) override { Set(e, false); }

 private:
  static void Set(rex::ui::KeyEvent& e, bool down) {
    const auto key = uint16_t(e.virtual_key());
    if (key < g_key_down.size()) g_key_down[key].store(down, std::memory_order_relaxed);
  }
};
KeyListener g_listener;

bool KeyDown(rex::ui::VirtualKey key) {
  return uint16_t(key) < g_key_down.size() && g_key_down[uint16_t(key)].load(std::memory_order_relaxed);
}

// The modifiers a binding names must be exactly the ones held, as for the
// keyboard pad (mnk_input_driver.cpp): "Up" stays silent with Shift held.
uint8_t LiveModifiers() {
  return (KeyDown(rex::ui::VirtualKey::kShift) ? 1 : 0) | (KeyDown(rex::ui::VirtualKey::kControl) ? 2 : 0) |
         (KeyDown(rex::ui::VirtualKey::kMenu) ? 4 : 0);
}

bool BindingHeld(std::string_view keys, uint8_t mods) {
  while (!keys.empty()) {
    const size_t comma = keys.find(',');
    std::string_view token = keys.substr(0, comma);
    keys = comma == std::string_view::npos ? std::string_view() : keys.substr(comma + 1);
    while (!token.empty() && token.front() == ' ') token.remove_prefix(1);
    while (!token.empty() && token.back() == ' ') token.remove_suffix(1);
    uint8_t want = 0;
    for (;;) {
      const size_t plus = token.find('+');
      if (plus == std::string_view::npos || plus == 0) break;
      const std::string_view head = token.substr(0, plus);
      const uint8_t bit = head == "Shift" ? 1 : head == "Ctrl" || head == "Control" ? 2 : head == "Alt" ? 4 : 0;
      if (!bit) break;
      want |= bit;
      token.remove_prefix(plus + 1);
    }
    if (want != mods) continue;
    const auto key = rex::ui::ParseVirtualKey(token);
    if (key != rex::ui::VirtualKey::kNone && KeyDown(key)) return true;
  }
  return false;
}

uint32_t KeysHeld() {
  const std::string binds[kActions] = {
      REXCVAR_GET(keybind_camera_fov_in),     REXCVAR_GET(keybind_camera_fov_out),
      REXCVAR_GET(keybind_camera_fov_reset),  REXCVAR_GET(keybind_camera_seat_up),
      REXCVAR_GET(keybind_camera_seat_down),  REXCVAR_GET(keybind_camera_seat_forward),
      REXCVAR_GET(keybind_camera_seat_back),  REXCVAR_GET(keybind_camera_seat_reset)};
  const uint8_t mods = LiveModifiers();
  uint32_t held = 0;
  for (size_t i = 0; i < kActions; ++i) {
    if (BindingHeld(binds[i], mods)) held |= 1u << i;
  }
  return held;
}

// --- Adjustments ------------------------------------------------------------

struct Adjust {
  float fov = 0, height = 0, forward = 0;  // degrees, metres
  bool Zero() const { return fov == 0 && height == 0 && forward == 0; }
};

std::mutex g_mutex;
std::map<uint32_t, Adjust> g_views;  // by the description's +8
bool g_loaded = false, g_dirty = false;
uint32_t g_view = ~0u;  // this frame's
Clock::time_point g_changed;
std::array<Clock::time_point, kActions> g_next{};
uint32_t g_was_held = 0;
// The seat's forward axis (camera_view_axis), learned from the car's motion
// when 0: per candidate axis, the sums of |cos| and cos to the motion.
int g_axis = 0;
struct Candidate {
  float aligned = 0, along = 0;
};
std::array<Candidate, 4> g_candidates{};
int g_samples = 0;
uint32_t g_learn_view = ~0u;
float g_last_pos[3] = {};
bool g_has_last = false;


float Parse(std::string_view text) {
  float value = 0;
  std::from_chars(text.data(), text.data() + text.size(), value);
  return std::isfinite(value) ? value : 0.0f;
}

void Load() {
  g_loaded = true;
  g_axis = std::clamp(REXCVAR_GET(camera_view_axis), -4, 4);
  const std::string text = REXCVAR_GET(camera_views);
  std::string_view rest = text;
  while (!rest.empty()) {
    const size_t semi = rest.find(';');
    const std::string_view entry = rest.substr(0, semi);
    rest = semi == std::string_view::npos ? std::string_view() : rest.substr(semi + 1);
    const size_t eq = entry.find('=');
    if (eq == std::string_view::npos) continue;
    uint32_t view = 0;
    std::from_chars(entry.data(), entry.data() + eq, view);
    std::string_view values = entry.substr(eq + 1);
    float v[3] = {};
    for (float& f : v) {
      const size_t comma = values.find(',');
      f = Parse(values.substr(0, comma));
      values = comma == std::string_view::npos ? std::string_view() : values.substr(comma + 1);
    }
    Adjust a{std::clamp(v[0], -kFovRange, kFovRange), std::clamp(v[1], -kHeightRange, kHeightRange),
             std::clamp(v[2], -kForwardRange, kForwardRange)};
    if (!a.Zero()) g_views[view] = a;
  }
}

void Save() {
  g_dirty = false;
  std::string views;
  for (const auto& [view, a] : g_views) {
    if (a.Zero()) continue;
    if (!views.empty()) views += ';';
    views += fmt::format("{}={:.1f},{:.3f},{:.3f}", view, a.fov, a.height, a.forward);
  }
  rex::cvar::SetFlagByName("camera_views", views);
  const auto file = ConfigFile();
  auto tmp = file;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    out << "# The game's camera adjustments (field of view, seat position) per camera view.\n"
           "# Written by the game as they change; delete this file to reset them all.\n"
        << fmt::format("camera_views = \"{}\"\ncamera_view_axis = {}\n", views, g_axis);
    if (!out) {
      REXLOG_WARN("Camera: could not write {}", tmp.string());
      return;
    }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, file, ec);
  if (ec) REXLOG_WARN("Camera: could not write {}: {}", file.string(), ec.message());
}

void Step(Adjust& a, CameraAction action) {
  switch (action) {
    case CameraAction::kFovIn: a.fov -= kFovStep; break;
    case CameraAction::kFovOut: a.fov += kFovStep; break;
    case CameraAction::kFovReset: a.fov = 0; break;
    case CameraAction::kSeatUp: a.height += kSeatStep; break;
    case CameraAction::kSeatDown: a.height -= kSeatStep; break;
    case CameraAction::kSeatForward: a.forward += kSeatStep; break;
    case CameraAction::kSeatBack: a.forward -= kSeatStep; break;
    case CameraAction::kSeatReset: a.height = a.forward = 0; break;
    default: break;
  }
  // Whole steps, so repeated presses come back to exactly 0.
  a.fov = std::clamp(std::round(a.fov / kFovStep) * kFovStep, -kFovRange, kFovRange);
  a.height = std::clamp(std::round(a.height / kSeatStep) * kSeatStep, -kHeightRange, kHeightRange);
  a.forward = std::clamp(std::round(a.forward / kSeatStep) * kSeatStep, -kForwardRange, kForwardRange);
}

// Once a frame, from the main view: the held actions change this view's
// adjustments (a press steps once, holding repeats).
void Tick(uint32_t view) {
  if (!g_loaded) Load();
  if (view != g_view) {
    g_view = view;
    const Adjust a = g_views.count(view) ? g_views[view] : Adjust{};
    REXLOG_INFO("Camera: view {} (field of view {:+.0f} deg, seat height {:+.0f} cm, forward {:+.0f} cm)", view,
                a.fov, a.height * 100, a.forward * 100);
  }
  const uint32_t held = KeysHeld() | wheel::CameraHeld();
  const auto now = Clock::now();
  bool stepped = false;
  for (size_t i = 0; i < kActions; ++i) {
    const uint32_t bit = 1u << i;
    if (!(held & bit)) continue;
    const bool resets = CameraAction(i) == CameraAction::kFovReset || CameraAction(i) == CameraAction::kSeatReset;
    if (!(g_was_held & bit)) {
      g_next[i] = now + kRepeatDelay;
    } else if (resets || now < g_next[i]) {
      continue;
    } else {
      g_next[i] = now + kRepeatEvery;
    }
    Step(g_views[view], CameraAction(i));
    stepped = true;
  }
  g_was_held = held;
  if (stepped) {
    const Adjust& a = g_views[view];
    REXLOG_INFO("Camera: view {} field of view {:+.0f} deg, seat height {:+.0f} cm, forward {:+.0f} cm", view, a.fov,
                a.height * 100, a.forward * 100);
    g_dirty = true;
    g_changed = now;
  }
  if (g_dirty && !held && now - g_changed > std::chrono::seconds(1)) Save();
}

// Candidate axis i (0-3: row 0, row 2, column 0, column 2) of a camera matrix.
void Axis(const float (&m)[16], int i, float (&out)[3]) {
  for (int k = 0; k < 3; ++k) out[k] = i < 2 ? m[(i == 0 ? 0 : 8) + k] : m[4 * k + (i == 2 ? 0 : 2)];
  const float length = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
  for (float& v : out) v = length > 0 ? v / length : 0.0f;
}

// Learns which of the matrix's axes points the way the car drives: over two
// seconds of driving in one view, the one that stays most in line with the
// motion. The sign is the way it moves.
void Learn(const float (&m)[16], uint32_t view) {
  const float* pos = m + 12;
  if (view != g_learn_view) {
    g_learn_view = view;
    g_candidates = {};
    g_samples = 0;
    g_has_last = false;
  }
  if (g_has_last) {
    const float d[3] = {pos[0] - g_last_pos[0], pos[1] - g_last_pos[1], pos[2] - g_last_pos[2]};
    const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    // 3 m/s up to a camera cut, at 60 fps.
    if (length > 0.05f && length < 3.0f) {
      for (int i = 0; i < 4; ++i) {
        float axis[3];
        Axis(m, i, axis);
        const float cos = (d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2]) / length;
        g_candidates[i].aligned += std::abs(cos);
        g_candidates[i].along += cos;
      }
      if (++g_samples >= 120) {
        int best = 0;
        for (int i = 1; i < 4; ++i)
          if (g_candidates[i].aligned > g_candidates[best].aligned) best = i;
        const float mean = g_candidates[best].aligned / float(g_samples);
        // Not yet in line (cornering hard, a camera fly-by): try again.
        if (mean > 0.9f) {
          g_axis = (best + 1) * (g_candidates[best].along > 0 ? 1 : -1);
          static const char* names[] = {"row 0", "row 2", "column 0", "column 2"};
          REXLOG_INFO("Camera: forward is {}{} of the camera matrix (in line {:.2f}, learned from the car's motion)",
                      g_axis > 0 ? "+" : "-", names[best], mean);
          Save();
        }
        g_candidates = {};
        g_samples = 0;
      }
    }
  }
  std::copy(pos, pos + 3, g_last_pos);
  g_has_last = true;
}

// The main view, 1280x720 (r31 + 8 its viewport: width at +8, height at +12).
bool MainView(const PPCContext& ctx, uint8_t* base) {
  const uint32_t viewport = ctx.r31.u32 + 8;
  return REX_LOAD_U32(viewport + 8) == 1280u && REX_LOAD_U32(viewport + 12) == 720u;
}

const Adjust* Current(uint32_t view) {
  auto it = g_views.find(view);
  return it == g_views.end() || it->second.Zero() ? nullptr : &it->second;
}
}  // namespace

std::filesystem::path ConfigFile() { return rex::filesystem::GetExecutableFolder() / "fm4.camera.toml"; }

void AttachKeys(rex::ui::Window* window) {
  if (window) window->AddInputListener(&g_listener, 50);
}

void ReleaseKeys() {
  for (auto& key : g_key_down) key.store(false, std::memory_order_relaxed);
}

}  // namespace fm4::camera

// The description's camera-to-world matrix (r3 + 0xC0), for the main view's
// camera (lr 0x822CA784, read into registers) and culling view (0x822CAA04,
// copied to r1 + 0x70). The seat offset goes into a copy at the caller's
// r1 + 0x70: free at the first call, the copy's own place at the second.
extern "C" void __imp__sub_82390070(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void sub_82390070(PPCContext& __restrict ctx, uint8_t* base) {
  using namespace fm4::camera;
  const uint32_t description = ctx.r3.u32;
  __imp__sub_82390070(ctx, base);
  const uint32_t caller = uint32_t(ctx.lr);
  if ((caller != 0x822CA784u && caller != 0x822CAA04u) || !REXCVAR_GET(camera_adjust) || !MainView(ctx, base)) {
    return;
  }
  const uint32_t view = REX_LOAD_U32(description + 8);
  float m[16];
  for (int i = 0; i < 16; ++i) m[i] = std::bit_cast<float>(REX_LOAD_U32(ctx.r3.u32 + 4 * i));
  std::lock_guard lock(g_mutex);
  if (caller == 0x822CA784u) {
    Tick(view);
    if (!g_axis) Learn(m, view);
  }
  const Adjust* a = Current(view);
  if (!a || (!a->height && !a->forward)) return;
  // The eye moves along the camera's up axis (row 1 or column 1, alike for a
  // level camera) and the learned forward axis (row 2 until it is learned).
  const int candidate = g_axis ? std::abs(g_axis) - 1 : 1;
  float up[3], axis[3];
  for (int k = 0; k < 3; ++k) up[k] = candidate < 2 ? m[4 + k] : m[4 * k + 1];
  const float up_length = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
  for (float& v : up) v = up_length > 0 ? v / up_length : 0.0f;
  Axis(m, candidate, axis);
  const float forward = a->forward * float(g_axis < 0 ? -1 : 1);
  for (int i = 0; i < 3; ++i) m[12 + i] += up[i] * a->height + axis[i] * forward;
  const uint32_t copy = ctx.r1.u32 + 0x70;
  for (int i = 0; i < 16; ++i) REX_STORE_U32(copy + 4 * i, std::bit_cast<uint32_t>(m[i]));
  ctx.r3.u64 = copy;
}

// The description's vertical field of view in radians (r3 + 0x100), for the
// main view's camera (lr 0x822CA7C8) and culling view (0x822CAA4C).
extern "C" void __imp__sub_822EC390(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void sub_822EC390(PPCContext& __restrict ctx, uint8_t* base) {
  using namespace fm4::camera;
  const uint32_t description = ctx.r3.u32;
  __imp__sub_822EC390(ctx, base);
  const uint32_t caller = uint32_t(ctx.lr);
  if ((caller != 0x822CA7C8u && caller != 0x822CAA4Cu) || !REXCVAR_GET(camera_adjust) || !MainView(ctx, base)) {
    return;
  }
  std::lock_guard lock(g_mutex);
  const Adjust* a = Current(REX_LOAD_U32(description + 8));
  if (!a || !a->fov) return;
  constexpr float kDegrees = 57.29578f;
  const float fov = float(ctx.f1.f64) * kDegrees;
  // The change stays within sensible lenses, whatever the game's own.
  const float adjusted = std::clamp(fov + a->fov, std::min(fov, kFovMin), std::max(fov, kFovMax));
  ctx.f1.f64 = double(adjusted / kDegrees);
}
