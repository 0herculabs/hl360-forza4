#include "wheel.h"
#include "wheel_effects.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

namespace fm4::wheel {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

std::string IdsText(uint16_t vendor, uint16_t product) {
  char text[10];
  std::snprintf(text, sizeof(text), "%04x:%04x", vendor, product);
  return text;
}
}  // namespace

std::string DeviceMatch(const Device& device, const std::vector<Device>& all) {
  if (device.vendor || device.product) {
    const auto same = std::count_if(all.begin(), all.end(), [&](const Device& d) {
      return d.vendor == device.vendor && d.product == device.product;
    });
    if (same <= 1) return IdsText(device.vendor, device.product);
  }
  if (!device.name.empty() && device.name.find_first_of("|,=") == std::string::npos) {
    const auto same = std::count_if(all.begin(), all.end(), [&](const Device& d) {
      return DeviceMatches(device.name, d.name, 0, 0, {});
    });
    if (same <= 1) return device.name;
  }
  return "path:" + device.path;
}

const Device* FindDevice(const std::vector<Device>& devices, std::string_view match) {
  for (const auto& d : devices) {
    if (DeviceMatches(match, d.name, d.vendor, d.product, d.path)) return &d;
  }
  return nullptr;
}

struct DeviceMonitor::Impl {
  // Shared with the callers, under mutex.
  mutable std::mutex mutex;
  Snapshot snapshot;
  Settings settings;
  std::array<ForceEffect, 5> forces{};
  std::array<bool, 5> playing{};
  bool driving = false;
  Clock::time_point heartbeat{}, rumble_time{};
  uint16_t rumble_left = 0, rumble_right = 0;
  std::atomic<bool> stop{false}, active{false}, test{false};

  // SDL owner only.
  bool caller_thread;
  bool initialized = false, init_attempted = false, haptics_initialized = false;
  struct Open { SDL_Joystick* joystick = nullptr; Device device; };
  std::map<SDL_JoystickID, Open> open;
  SDL_JoystickID haptic_owner = 0;
  SDL_Haptic* haptic = nullptr;
  uint32_t features = 0;
  std::array<SDL_HapticEffectID, 5> ids{{-1, -1, -1, -1, -1}};
  std::array<SDL_HapticEffect, 5> applied{};
  std::array<bool, 5> running{};
  std::array<Clock::time_point, 5> applied_at{};
  SDL_HapticEffectID rumble_id = -1, test_id = -1;
  int test_stage = 0;
  Clock::time_point test_next{}, test_cooldown{}, discover{}, haptic_attempt{};
  uint64_t generation = 0;
  std::string haptic_status;
  std::thread thread;

  explicit Impl(bool caller) : caller_thread(caller) {
    if (!caller_thread) thread = std::thread([this] {
      while (!stop) { Tick(); std::this_thread::sleep_for(4ms); }
      Shutdown();
    });
  }
  ~Impl() {
    stop = true;
    if (thread.joinable()) thread.join();
    if (caller_thread) Shutdown();
  }

  void StopEffects() {
    if (!haptic) return;
    for (size_t i = 0; i < ids.size(); ++i) {
      if (ids[i] >= 0 && running[i]) SDL_StopHapticEffect(haptic, ids[i]);
      running[i] = false;
    }
    if (rumble_id >= 0) SDL_StopHapticEffect(haptic, rumble_id);
  }
  void CloseHaptic() {
    if (!haptic) return;
    SDL_StopHapticEffects(haptic);
    for (auto& id : ids) { if (id >= 0) SDL_DestroyHapticEffect(haptic, id); id = -1; }
    if (rumble_id >= 0) SDL_DestroyHapticEffect(haptic, rumble_id);
    if (test_id >= 0) SDL_DestroyHapticEffect(haptic, test_id);
    rumble_id = test_id = -1;
    running.fill(false);
    applied = {};
    SDL_CloseHaptic(haptic);
    haptic = nullptr;
    haptic_owner = 0;
    features = 0;
  }
  void Shutdown() {
    CloseHaptic();
    for (auto& [id, o] : open) SDL_CloseJoystick(o.joystick);
    open.clear();
    if (haptics_initialized) { SDL_QuitSubSystem(SDL_INIT_HAPTIC); haptics_initialized = false; }
    if (initialized) { SDL_QuitSubSystem(SDL_INIT_JOYSTICK); initialized = false; }
  }

  void Discover() {
    int count = 0;
    SDL_JoystickID* list = SDL_GetJoysticks(&count);
    std::vector<SDL_JoystickID> present(list, list + (list ? count : 0));
    SDL_free(list);
    bool changed = false;
    for (auto it = open.begin(); it != open.end();) {
      if (std::find(present.begin(), present.end(), it->first) == present.end() ||
          !SDL_JoystickConnected(it->second.joystick)) {
        if (it->first == haptic_owner) CloseHaptic();
        SDL_CloseJoystick(it->second.joystick);
        it = open.erase(it);
        changed = true;
      } else ++it;
    }
    for (SDL_JoystickID id : present) {
      if (open.count(id)) continue;
      SDL_Joystick* joystick = SDL_OpenJoystick(id);
      if (!joystick) continue;
      Open o{joystick, {}};
      o.device.id = id;
      const char* name = SDL_GetJoystickName(joystick);
      const char* path = SDL_GetJoystickPath(joystick);
      o.device.name = name ? name : "Controller";
      o.device.path = path ? path : "";
      o.device.vendor = SDL_GetJoystickVendor(joystick);
      o.device.product = SDL_GetJoystickProduct(joystick);
      o.device.wheel = SDL_GetJoystickType(joystick) == SDL_JOYSTICK_TYPE_WHEEL;
      open.emplace(id, std::move(o));
      changed = true;
    }
    if (changed) ++generation;
  }

  void ReadDevices(Snapshot& next) {
    SDL_UpdateJoysticks();
    next.devices.clear();
    for (auto& [id, o] : open) {
      Device& d = o.device;
      const int axes = std::min(64, SDL_GetNumJoystickAxes(o.joystick));
      const int buttons = std::min(128, SDL_GetNumJoystickButtons(o.joystick));
      const int hats = std::min(4, SDL_GetNumJoystickHats(o.joystick));
      d.axes.resize(std::max(axes, 0));
      for (int i = 0; i < axes; ++i) d.axes[i] = SDL_GetJoystickAxis(o.joystick, i);
      if (d.rest.size() != d.axes.size()) d.rest = d.axes;
      d.buttons.resize(std::max(buttons, 0));
      for (int i = 0; i < buttons; ++i) d.buttons[i] = SDL_GetJoystickButton(o.joystick, i);
      d.hats.resize(std::max(hats, 0));
      for (int i = 0; i < hats; ++i) d.hats[i] = SDL_GetJoystickHat(o.joystick, i);
      next.devices.push_back(d);
    }
  }

  // The device that receives forces: wheel_ffb_device, else the wheel base,
  // else the first device SDL types as a wheel.
  SDL_JoystickID ForceDevice(const Settings& s) const {
    const std::string& match = !s.ffb_device.empty() ? s.ffb_device : s.device;
    for (const auto& [id, o] : open) {
      if (match.empty() ? o.device.wheel : DeviceMatches(match, o.device.name, o.device.vendor, o.device.product, o.device.path))
        return id;
    }
    return 0;
  }

  bool OpenHaptic(SDL_JoystickID id) {
    if (haptic && haptic_owner == id) return true;
    CloseHaptic();
    if (!id) { haptic_status = "No force feedback device connected"; return false; }
    if (!haptics_initialized) haptics_initialized = SDL_InitSubSystem(SDL_INIT_HAPTIC);
    if (!haptics_initialized) { haptic_status = SDL_GetError(); return false; }
    haptic = SDL_OpenHapticFromJoystick(open[id].joystick);
    if (!haptic) { haptic_status = std::string("Cannot open force feedback: ") + SDL_GetError(); return false; }
    haptic_owner = id;
    features = SDL_GetHapticFeatures(haptic);
    // Strength is applied to each effect; the device-wide gain stays at full,
    // whatever an earlier session left it at.
    if (features & SDL_HAPTIC_GAIN) SDL_SetHapticGain(haptic, 100);
    haptic_status = (features & SDL_HAPTIC_CONSTANT) ? "Force feedback ready" : "This device has no constant force";
    return true;
  }

  void ApplyEffects(const Settings& s, const std::array<ForceEffect, 5>& effects, const std::array<bool, 5>& starts,
                    Clock::time_point now) {
    const ForceTuning tuning = TuningFrom(s);
    for (size_t i = 0; i < effects.size(); ++i) {
      if (!starts[i] || !effects[i].id) {
        if (ids[i] >= 0 && running[i]) SDL_StopHapticEffect(haptic, ids[i]);
        running[i] = false;
        continue;
      }
      SDL_HapticEffect output{};
      if (!BuildHapticEffect(effects[i], tuning, output) || !(features & output.type)) continue;
      const bool same = ids[i] >= 0 && std::memcmp(&output, &applied[i], sizeof(output)) == 0;
      // Updates are rate limited, keeping the latest value: constant force
      // 10 ms, conditions 40 ms.
      const auto interval = output.type == SDL_HAPTIC_CONSTANT ? 10ms : 40ms;
      if (!same && (ids[i] < 0 || now - applied_at[i] >= interval)) {
        if (ids[i] >= 0 && applied[i].type != output.type) { SDL_DestroyHapticEffect(haptic, ids[i]); ids[i] = -1; }
        if (ids[i] < 0) ids[i] = SDL_CreateHapticEffect(haptic, &output);
        else if (!SDL_UpdateHapticEffect(haptic, ids[i], &output)) { haptic_status = SDL_GetError(); continue; }
        if (ids[i] < 0) { haptic_status = SDL_GetError(); continue; }
        applied[i] = output;
        applied_at[i] = now;
      }
      if (ids[i] >= 0 && !running[i]) {
        running[i] = SDL_RunHapticEffect(haptic, ids[i], 1);
        if (!running[i]) haptic_status = SDL_GetError();
      }
    }
  }

  void ApplyRumble(int16_t magnitude, uint16_t left, uint16_t right) {
    if (!(features & SDL_HAPTIC_SINE)) return;
    if (magnitude <= 0) {
      if (rumble_id >= 0) SDL_StopHapticEffect(haptic, rumble_id);
      return;
    }
    SDL_HapticEffect effect{};
    effect.type = SDL_HAPTIC_SINE;
    effect.periodic.direction.type = SDL_HAPTIC_STEERING_AXIS;
    effect.periodic.length = SDL_HAPTIC_INFINITY;
    // The right (small) motor buzzes, the left (large) one rumbles.
    effect.periodic.period = right > left ? 25 : 60;
    effect.periodic.magnitude = magnitude;
    if (rumble_id < 0) rumble_id = SDL_CreateHapticEffect(haptic, &effect);
    else SDL_UpdateHapticEffect(haptic, rumble_id, &effect);
    if (rumble_id < 0) { features &= ~uint32_t(SDL_HAPTIC_SINE); return; }
    SDL_RunHapticEffect(haptic, rumble_id, 1);
  }

  void RunTest(const Settings& s, Clock::time_point now) {
    if (test.exchange(false) && test_stage == 0 && now >= test_cooldown) {
      StopEffects();
      if (!OpenHaptic(ForceDevice(s))) {
        test_cooldown = now + 750ms;
      } else if (!(features & SDL_HAPTIC_CONSTANT)) {
        haptic_status = "\"" + open[haptic_owner].device.name + "\" has no constant force; the game would only rumble it.";
        test_cooldown = now + 750ms;
        if (caller_thread) CloseHaptic();
      } else {
        test_stage = 1;
        test_next = now;
      }
    }
    if (test_stage == 0 || now < test_next || !haptic) return;
    if (test_id >= 0) { SDL_DestroyHapticEffect(haptic, test_id); test_id = -1; }
    if (test_stage == 3) {
      haptic_status = "Pulled \"" + open[haptic_owner].device.name +
                      "\" LEFT, then RIGHT (" + std::to_string(s.ffb_strength) +
                      " % strength). If it pulled right first, tick Reverse force direction.";
      test_stage = 0;
      test_cooldown = now + 500ms;
      if (caller_thread) CloseHaptic();
      return;
    }
    // A positive level pulls left (SDL and DirectInput name the side a force
    // comes from); the game's forces go out the same way, so Reverse flips both.
    const Sint16 level = TestForceLevel(s);
    const Sint16 left = s.ffb_invert ? Sint16(-level) : level;
    SDL_HapticEffect pulse{};
    pulse.type = SDL_HAPTIC_CONSTANT;
    pulse.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
    pulse.constant.length = 500;
    pulse.constant.level = test_stage == 1 ? left : Sint16(-left);
    test_id = SDL_CreateHapticEffect(haptic, &pulse);
    if (test_id < 0 || !SDL_RunHapticEffect(haptic, test_id, 1)) {
      haptic_status = std::string("The device refused a constant force: ") + SDL_GetError();
      test_stage = 0;
      test_cooldown = now + 750ms;
      if (caller_thread) CloseHaptic();
      return;
    }
    haptic_status = test_stage == 1 ? "Pulling LEFT..." : "Pulling RIGHT...";
    ++test_stage;
    test_next = now + 650ms;
  }

  void Tick() {
    if (!init_attempted) {
      init_attempted = true;
      SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
      initialized = SDL_InitSubSystem(SDL_INIT_JOYSTICK);
      if (!initialized) { std::lock_guard lock(mutex); snapshot.status = SDL_GetError(); }
    }
    if (!initialized) return;
    Settings s;
    std::array<ForceEffect, 5> effects;
    std::array<bool, 5> starts;
    bool in_race;
    Clock::time_point beat, rumble_at;
    uint16_t left, right;
    {
      std::lock_guard lock(mutex);
      s = settings; effects = forces; starts = playing; in_race = driving; beat = heartbeat;
      rumble_at = rumble_time; left = rumble_left; right = rumble_right;
    }
    const auto now = Clock::now();
    if (now - discover >= 1s) { discover = now; Discover(); }
    Snapshot next;
    ReadDevices(next);
    next.generation = generation;
    next.status = open.empty() ? "No game controllers found" : std::to_string(open.size()) + " devices";

    const FfbMode mode = ParseFfbMode(s.ffb_mode);
    // The game keeps the force device open while force feedback is on; the
    // launcher opens it only for Test, so the game can have it.
    if (!caller_thread && mode != FfbMode::kOff && now - haptic_attempt >= 1s) {
      const SDL_JoystickID want = ForceDevice(s);
      if (want != haptic_owner || !haptic) { haptic_attempt = now; OpenHaptic(want); }
    }
    RunTest(s, now);
    if (haptic && test_stage == 0 && !caller_thread) {
      const bool focused = active;
      if (mode == FfbMode::kEffects && ForceOutputAllowed(focused, in_race, beat, now)) ApplyEffects(s, effects, starts, now);
      else StopEffects();
      const bool rumbling = mode != FfbMode::kOff && focused && now - rumble_at < 500ms;
      ApplyRumble(rumbling ? RumbleMagnitude(left, right, s) : 0, left, right);
    }
    next.force_supported = haptic && (features & SDL_HAPTIC_CONSTANT);
    next.rumble_supported = haptic && (features & SDL_HAPTIC_SINE);
    next.ffb_name = haptic ? open[haptic_owner].device.name : "";
    next.haptic_status = haptic_status;
    next.test_busy = test_stage != 0 || now < test_cooldown;
    std::lock_guard lock(mutex);
    snapshot = std::move(next);
  }
};

DeviceMonitor::DeviceMonitor(bool caller_thread) : impl_(std::make_unique<Impl>(caller_thread)) {}
DeviceMonitor::~DeviceMonitor() = default;
void DeviceMonitor::Pump() { if (impl_->caller_thread) impl_->Tick(); }
Snapshot DeviceMonitor::Read() const { std::lock_guard lock(impl_->mutex); return impl_->snapshot; }
void DeviceMonitor::SetActive(bool active) { impl_->active = active; }
void DeviceMonitor::ConfigureForce(const Settings& settings) {
  std::lock_guard lock(impl_->mutex);
  impl_->settings = settings;
}
void DeviceMonitor::TestHaptics() { impl_->test = true; }
void DeviceMonitor::UpdateForce(ForceEffect e) {
  if (e.id < 1 || e.id > 5) return;
  std::lock_guard lock(impl_->mutex);
  impl_->forces[e.id - 1] = e;
  impl_->playing[e.id - 1] = true;
  impl_->heartbeat = std::chrono::steady_clock::now();
}
void DeviceMonitor::OperateForce(int id, bool start) {
  if (id < 1 || id > 5) return;
  std::lock_guard lock(impl_->mutex);
  impl_->playing[id - 1] = start;
}
void DeviceMonitor::ClearForce() {
  std::lock_guard lock(impl_->mutex);
  impl_->playing.fill(false);
  impl_->driving = false;
}
void DeviceMonitor::SetDriving(bool driving) {
  std::lock_guard lock(impl_->mutex);
  impl_->driving = driving;
  impl_->heartbeat = std::chrono::steady_clock::now();
}
void DeviceMonitor::SetRumble(uint16_t left, uint16_t right) {
  std::lock_guard lock(impl_->mutex);
  impl_->rumble_left = left;
  impl_->rumble_right = right;
  impl_->rumble_time = std::chrono::steady_clock::now();
}
}  // namespace fm4::wheel
