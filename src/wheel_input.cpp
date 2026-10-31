// The racing wheel as the game sees it: one XInput wheel (subtype 2) built
// from bindings on any number of USB devices (a wheel base, separate pedals,
// a shifter, a button box), with the game's native force feedback played on
// the force device. Settings and binding syntax follow the Pinyon Shift
// racing wheel work, so its Wheel screen and docs apply.
#include "wheel_input.h"
#include "scripted_input.h"
#include "wheel.h"
#include <cmath>
#include <map>
#include <mutex>
#include <optional>
#include <atomic>
#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(wheel_enabled, false, "FM4/Wheel", "Use the racing wheel bindings below as the player's controller");
REXCVAR_DEFINE_STRING(wheel_device, "", "FM4/Wheel", "Wheel base: vvvv:pppp, part of its name, or path:<SDL path>; empty for the first wheel");
REXCVAR_DEFINE_INT32(wheel_steering_axis, 0, "FM4/Wheel", "Steering axis on the wheel base");
REXCVAR_DEFINE_BOOL(wheel_steering_invert, false, "FM4/Wheel", "Reverse the steering axis");
REXCVAR_DEFINE_INT32(wheel_steering_scale, 100, "FM4/Wheel", "Steering sensitivity in percent; 200 reaches full lock at half the wheel's rotation");
REXCVAR_DEFINE_INT32(wheel_steering_deadzone, 0, "FM4/Wheel", "Steering dead zone in percent of half travel");
REXCVAR_DEFINE_INT32(wheel_rotation, 900, "FM4/Wheel", "The rotation set in the wheel's software, degrees lock to lock");
REXCVAR_DEFINE_INT32(wheel_game_rotation, 0, "FM4/Wheel",
                     "The in-game steering wheel's rotation lock to lock in degrees, so it turns as far as "
                     "the real one (wheel_rotation); 0 maps the wheel's full rotation to full lock");
REXCVAR_DEFINE_STRING(wheel_throttle, "", "FM4/Wheel", "Throttle axis, [device|][+|-]axisN");
REXCVAR_DEFINE_STRING(wheel_brake, "", "FM4/Wheel", "Brake axis, [device|][+|-]axisN");
REXCVAR_DEFINE_STRING(wheel_clutch, "", "FM4/Wheel", "Clutch axis, [device|][+|-]axisN");
REXCVAR_DEFINE_STRING(wheel_clutch_button, "B", "FM4/Wheel", "Xbox buttons the clutch pedal presses past half travel (FM4's clutch is a button)");
REXCVAR_DEFINE_BOOL(wheel_clutch_analog, false, "FM4/Wheel", "Also send the clutch pedal as the right stick's Y axis (FH1's analog clutch; unconfirmed in FM4)");
REXCVAR_DEFINE_STRING(wheel_buttons, "UP=h0.up,DOWN=h0.down,LEFT=h0.left,RIGHT=h0.right", "FM4/Wheel",
                      "Button bindings, NAME=[device|]input,...: N, hN.up, [+|-]axisN; NAME an Xbox control or GEAR1-7/REVERSE");
REXCVAR_DEFINE_INT32(wheel_pedal_deadzone, 3, "FM4/Wheel", "Pedal dead zone at rest in percent, for pedals without their own");
REXCVAR_DEFINE_INT32(wheel_throttle_deadzone_bottom, -1, "FM4/Wheel", "Throttle dead zone at rest; -1 uses wheel_pedal_deadzone");
REXCVAR_DEFINE_INT32(wheel_throttle_deadzone_top, 0, "FM4/Wheel", "Throttle travel at the top that already reads fully pressed");
REXCVAR_DEFINE_INT32(wheel_brake_deadzone_bottom, -1, "FM4/Wheel", "Brake dead zone at rest; -1 uses wheel_pedal_deadzone");
REXCVAR_DEFINE_INT32(wheel_brake_deadzone_top, 0, "FM4/Wheel", "Brake travel at the top that already reads fully pressed");
REXCVAR_DEFINE_INT32(wheel_clutch_deadzone_bottom, -1, "FM4/Wheel", "Clutch dead zone at rest; -1 uses wheel_pedal_deadzone");
REXCVAR_DEFINE_INT32(wheel_clutch_deadzone_top, 0, "FM4/Wheel", "Clutch travel at the top that already reads fully pressed");
REXCVAR_DEFINE_STRING(wheel_ffb_device, "", "FM4/Wheel", "Device that receives force feedback; empty for the wheel base");
REXCVAR_DEFINE_STRING(wheel_ffb_mode, "on", "FM4/Wheel", "on (the game's own wheel effects), rumble (vibration only) or off");
REXCVAR_DEFINE_INT32(wheel_ffb_strength, 25, "FM4/Wheel", "Overall force feedback strength in percent (0-200); start low on a direct-drive base");
REXCVAR_DEFINE_INT32(wheel_ffb_min_force, 0, "FM4/Wheel", "Lift any non-zero force to at least this percent, for geared or belted wheels");
REXCVAR_DEFINE_INT32(wheel_ffb_spring, 100, "FM4/Wheel", "Strength of the game's centring spring, percent");
REXCVAR_DEFINE_INT32(wheel_ffb_damper, 100, "FM4/Wheel", "Strength of the game's damper, percent");
REXCVAR_DEFINE_INT32(wheel_ffb_friction, 100, "FM4/Wheel", "Strength of the game's friction, percent");
REXCVAR_DEFINE_INT32(wheel_ffb_inertia, 100, "FM4/Wheel", "Strength of the game's inertia, percent");
REXCVAR_DEFINE_INT32(wheel_ffb_rumble, 50, "FM4/Wheel", "Strength of the game's rumble, played as a vibration");
REXCVAR_DEFINE_INT32(wheel_ffb_rumble_engine, 100, "FM4/Wheel", "Share of the rumble for the right (engine) motor, percent");
REXCVAR_DEFINE_INT32(wheel_ffb_rumble_road, 100, "FM4/Wheel", "Share of the rumble for the left (road) motor, percent");
REXCVAR_DEFINE_BOOL(wheel_ffb_invert, false, "FM4/Wheel", "Reverse the force direction");
REXCVAR_DEFINE_BOOL(wheel_log_inputs, false, "FM4/Wheel", "Log raw axis, button and hat changes of every device, to find input numbers");

namespace fm4::wheel {
namespace {
using namespace rex::input;
constexpr DeviceId kWheelId = static_cast<DeviceId>(0xF400000000000001ull);
std::mutex service_mutex;
std::weak_ptr<DeviceMonitor> service;
std::atomic<bool> focused{false};
std::atomic<uint32_t> camera_held{0};
std::shared_ptr<DeviceMonitor> Service() { std::lock_guard lock(service_mutex);return service.lock(); }

class Assignment final : public DeviceAssignment {
 public:
  void OnDevicesChanged(const std::vector<DeviceInfo>& devices) override { devices_ = devices; }
  void DevicesForUser(uint32_t user, std::vector<DeviceId>& out) const override {
    out.clear();
    const bool wheel = std::any_of(devices_.begin(), devices_.end(), [](const DeviceInfo& d) {return d.id == kWheelId;});
    if (wheel) {
      // Keep wheel capabilities stable even when navigating with the keyboard.
      if (user == 0) out.push_back(kWheelId);
    } else {
      uint32_t slot=0;
      for (const auto& d : devices_) {
        if(d.synthetic) {if(user==0) out.push_back(d.id);}
        else if(slot++==user) out.push_back(d.id);
      }
    }
  }
 private:
  std::vector<DeviceInfo> devices_;
};
}  // namespace

Settings ReadSettings() {
  Settings s;
  s.enabled = REXCVAR_GET(wheel_enabled);
  s.device = MigrateDeviceKey(REXCVAR_GET(wheel_device));
  s.steering_axis = REXCVAR_GET(wheel_steering_axis);
  s.steering_invert = REXCVAR_GET(wheel_steering_invert);
  s.steering_scale = REXCVAR_GET(wheel_steering_scale);
  s.steering_deadzone = REXCVAR_GET(wheel_steering_deadzone);
  s.wheel_rotation = REXCVAR_GET(wheel_rotation);
  s.game_rotation = REXCVAR_GET(wheel_game_rotation);
  s.throttle = REXCVAR_GET(wheel_throttle);
  s.brake = REXCVAR_GET(wheel_brake);
  s.clutch = REXCVAR_GET(wheel_clutch);
  s.clutch_button = REXCVAR_GET(wheel_clutch_button);
  s.clutch_analog = REXCVAR_GET(wheel_clutch_analog);
  s.buttons = REXCVAR_GET(wheel_buttons);
  s.pedal_deadzone = REXCVAR_GET(wheel_pedal_deadzone);
  s.deadzone_bottom = {{REXCVAR_GET(wheel_throttle_deadzone_bottom), REXCVAR_GET(wheel_brake_deadzone_bottom),
                        REXCVAR_GET(wheel_clutch_deadzone_bottom)}};
  s.deadzone_top = {{REXCVAR_GET(wheel_throttle_deadzone_top), REXCVAR_GET(wheel_brake_deadzone_top),
                     REXCVAR_GET(wheel_clutch_deadzone_top)}};
  s.ffb_device = REXCVAR_GET(wheel_ffb_device);
  s.ffb_mode = REXCVAR_GET(wheel_ffb_mode);
  s.ffb_strength = REXCVAR_GET(wheel_ffb_strength);
  s.ffb_min_force = REXCVAR_GET(wheel_ffb_min_force);
  s.ffb_spring = REXCVAR_GET(wheel_ffb_spring);
  s.ffb_damper = REXCVAR_GET(wheel_ffb_damper);
  s.ffb_friction = REXCVAR_GET(wheel_ffb_friction);
  s.ffb_inertia = REXCVAR_GET(wheel_ffb_inertia);
  s.ffb_rumble = REXCVAR_GET(wheel_ffb_rumble);
  s.ffb_rumble_engine = REXCVAR_GET(wheel_ffb_rumble_engine);
  s.ffb_rumble_road = REXCVAR_GET(wheel_ffb_rumble_road);
  s.ffb_invert = REXCVAR_GET(wheel_ffb_invert);
  s.log_inputs = REXCVAR_GET(wheel_log_inputs);
  return s;
}

namespace {
// What the game reads from the bindings, the same maths the launcher's live
// view uses.
struct Composer {
  Settings s;
  std::optional<AxisBinding> throttle, brake, clutch;
  std::vector<ButtonBinding> buttons;
  uint16_t clutch_buttons = 0;
  bool shifter = false;

  explicit Composer(Settings settings) : s(std::move(settings)) {
    throttle = ParseAxisBinding(s.throttle);
    brake = ParseAxisBinding(s.brake);
    clutch = ParseAxisBinding(s.clutch);
    buttons = ParseButtonBindings(s.buttons);
    clutch_buttons = ButtonsFromNames(s.clutch_button);
    shifter = std::any_of(buttons.begin(), buttons.end(), [](const ButtonBinding& b) { return b.gear != 0; });
  }
  const Device* Base(const Snapshot& snap) const {
    if (!s.device.empty()) return FindDevice(snap.devices, s.device);
    for (const auto& d : snap.devices) if (d.wheel) return &d;
    return nullptr;
  }
  const Device* On(const Snapshot& snap, const std::string& device) const {
    return device.empty() ? Base(snap) : FindDevice(snap.devices, device);
  }
  uint8_t Pedal(const Snapshot& snap, const std::optional<AxisBinding>& b, int which) const {
    if (!b) return 0;
    const Device* d = On(snap, b->device);
    if (!d || b->axis >= int(d->axes.size())) return 0;
    const int bottom = s.deadzone_bottom[which] < 0 ? s.pedal_deadzone : s.deadzone_bottom[which];
    return PedalFromAxis(d->axes[b->axis], ResolveAxisDirection(b->direction, d->rest[b->axis]), bottom,
                         s.deadzone_top[which]);
  }
  // `camera`: the camera actions held (CAM_* bindings, CameraAction bits).
  X_INPUT_GAMEPAD Compose(const Snapshot& snap, uint32_t& camera) const {
    camera = 0;
    X_INPUT_GAMEPAD pad{};
    if (const Device* base = Base(snap); base && s.steering_axis >= 0 && s.steering_axis < int(base->axes.size()))
      pad.thumb_lx = SteeringFromAxis(base->axes[s.steering_axis], s.steering_invert, SteeringScale(s), s.steering_deadzone);
    uint8_t rt = Pedal(snap, throttle, 0), lt = Pedal(snap, brake, 1);
    const uint8_t c = Pedal(snap, clutch, 2);
    uint16_t pressed = 0;
    int16_t gear = 0;
    if (s.clutch_analog) pad.thumb_ry = int16_t(int32_t(c) * 32767 / 255);
    if (c >= 128) pressed |= clutch_buttons;
    for (const auto& b : buttons) {
      const Device* d = On(snap, b.device);
      if (!d) continue;
      bool on = false;
      if (b.axis >= 0 && b.axis < int(d->axes.size()))
        on = PedalFromAxis(d->axes[b.axis], ResolveAxisDirection(b.axis_direction, d->rest[b.axis]), 0, 0) >= 128;
      else if (b.button >= 0 && b.button < int(d->buttons.size())) on = d->buttons[b.button];
      else if (b.hat >= 0 && b.hat < int(d->hats.size())) on = (d->hats[b.hat] & b.hat_mask) != 0;
      if (!on) continue;
      if (b.camera >= 0) {
        camera |= 1u << b.camera;
        continue;
      }
      if (b.gear) gear = b.gear;
      pressed |= b.xbuttons;
      if (b.left_trigger) lt = 255;
      if (b.right_trigger) rt = 255;
    }
    pad.buttons = pressed;
    pad.left_trigger = lt;
    pad.right_trigger = rt;
    // With gears bound, FH1 reads the held gear from thumb_rx (-1 reverse, 0
    // neutral); FM4 shares its wheel code but this is not confirmed there.
    if (shifter) pad.thumb_rx = gear;
    return pad;
  }
};

class Driver final : public InputDriver {
 public:
  Driver() : InputDriver(nullptr,0), composer_(ReadSettings()), monitor_(std::make_shared<DeviceMonitor>()) {
    monitor_->ConfigureForce(composer_.s);
    monitor_->SetActive(focused);
    std::lock_guard lock(service_mutex);service=monitor_;
  }
  void OnWindowAvailable(rex::ui::Window* window) override { window_ = window; }
  rex::X_STATUS Setup() override { return 0; }
  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    const auto s = monitor_->Read();
    const Device* base = composer_.Base(s);
    if (s.generation != generation_) {
      generation_ = s.generation;
      for (const auto& d : s.devices)
        REXLOG_INFO("Wheel device: \"{}\" {:04x}:{:04x} ({} axes, {} buttons, {} hats){}{}", d.name, d.vendor, d.product,
                    d.axes.size(), d.buttons.size(), d.hats.size(), d.wheel ? ", SDL wheel" : "",
                    base && base->id == d.id ? ", the wheel base" : "");
      if (!base) REXLOG_WARN("Wheel base \"{}\" not connected", composer_.s.device);
    }
    if (s.haptic_status != haptic_status_) {
      haptic_status_ = s.haptic_status;
      REXLOG_INFO("Wheel force feedback: {} (device \"{}\", constant force {}, mode {}, strength {}%)", s.haptic_status,
                  s.ffb_name, s.force_supported, composer_.s.ffb_mode, composer_.s.ffb_strength);
    }
    if (composer_.s.log_inputs) LogInputs(s);
    if (!base) return;
    out.push_back({kWheelId,0,base->name,"wheel",false});
  }
  rex::X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out) override {
    const auto s = monitor_->Read();
    if (id != kWheelId || !out || !composer_.Base(s)) return 1167;
    *out = {};
    out->packet_number = ++packet_;
    uint32_t camera = 0;
    if (window_ && !focused) {
      camera_held = 0;
      return 0;
    }
    out->gamepad = composer_.Compose(s, camera);
    camera_held = camera;
    return 0;
  }
  rex::X_RESULT GetDeviceCapabilities(DeviceId id,uint32_t,X_INPUT_CAPABILITIES* out) override {
    if (id != kWheelId || !out || !composer_.Base(monitor_->Read())) return 1167;
    *out = {}; out->type = 1; out->sub_type = 2;
    out->gamepad.buttons = 0xF3FF; out->gamepad.thumb_lx = 32767;
    out->gamepad.left_trigger = out->gamepad.right_trigger = 255;
    if (composer_.shifter) out->gamepad.thumb_rx = int16_t(0xFFFF);
    if (composer_.s.clutch_analog) out->gamepad.thumb_ry = int16_t(0xFFFF);
    out->flags = ForceAvailable(0) ? 1 : 0;
    return 0;
  }
  rex::X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) override {
    if (id != kWheelId || !vibration) return 1167;
    monitor_->SetRumble(vibration->left_motor_speed, vibration->right_motor_speed);
    return 0;
  }
  rex::X_RESULT GetDeviceKeystroke(DeviceId,uint32_t,X_INPUT_KEYSTROKE*) override {return 4306;}
 private:
  void LogInputs(const Snapshot& s) {
    for (const auto& d : s.devices) {
      auto& last = logged_[d.name + "|" + d.path];
      for (size_t i = 0; i < d.axes.size(); ++i) {
        if (last.axes.size() != d.axes.size()) break;
        if (std::abs(d.axes[i] - last.axes[i]) > 4000)
          REXLOG_INFO("Wheel input: \"{}\" axis{} {}", d.name, i, d.axes[i]);
        else continue;
        last.axes[i] = d.axes[i];
      }
      for (size_t i = 0; i < d.buttons.size() && last.buttons.size() == d.buttons.size(); ++i)
        if (d.buttons[i] != last.buttons[i]) REXLOG_INFO("Wheel input: \"{}\" button {} {}", d.name, i, d.buttons[i] ? "down" : "up");
      for (size_t i = 0; i < d.hats.size() && last.hats.size() == d.hats.size(); ++i)
        if (d.hats[i] != last.hats[i]) REXLOG_INFO("Wheel input: \"{}\" hat {} = {}", d.name, i, d.hats[i]);
      if (last.axes.size() != d.axes.size()) last.axes = d.axes;
      last.buttons = d.buttons;
      last.hats = d.hats;
    }
  }
  Composer composer_;
  std::shared_ptr<DeviceMonitor> monitor_;
  rex::ui::Window* window_ = nullptr;
  uint64_t generation_ = ~0ull;
  uint32_t packet_ = 0;
  std::string haptic_status_;
  std::map<std::string, Device> logged_;
};
}  // namespace

bool ForceAvailable(uint32_t user) {
  const auto device=Service();
  if(user!=0 || !device || ParseFfbMode(REXCVAR_GET(wheel_ffb_mode))!=FfbMode::kEffects) return false;
  return device->Read().force_supported;
}
uint32_t CameraHeld() { return camera_held.load(std::memory_order_relaxed); }
void SetFocus(bool value) {focused=value;if(auto s=Service()) s->SetActive(value);}
void SubmitEffect(ForceEffect effect) {
  if(effect.id<1 || effect.id>5) return;
  static std::atomic<uint32_t> seen{0};
  if(!(seen.fetch_or(1u<<effect.id)&(1u<<effect.id)))
    REXLOG_INFO("Native wheel effect: slot={}, type={}, gain={}, constant={}",effect.id,effect.type,effect.gain,effect.magnitude);
  if(auto s=Service()) s->UpdateForce(effect);
}
void OperateEffect(int id,bool start) {if(auto s=Service()) s->OperateForce(id,start);}
void ResetEffects() {if(auto s=Service()) s->ClearForce();}
void SetDriving(bool driving) {if(auto s=Service()) s->SetDriving(driving);}
std::unique_ptr<rex::system::IInputSystem> CreateInputSystem(bool tool_mode) {
  auto input = rex::input::CreateDefaultInputSystem(tool_mode);
  if (!tool_mode) AddScriptedInput(*input);
  if (!tool_mode && REXCVAR_GET(wheel_enabled)) {
    const Settings s = ReadSettings();
    REXLOG_INFO("Wheel input enabled: base \"{}\" steering axis {}, throttle \"{}\", brake \"{}\", clutch \"{}\", "
                "buttons \"{}\"; force feedback {} at {}% on \"{}\"",
                s.device, s.steering_axis, s.throttle, s.brake, s.clutch, s.buttons, s.ffb_mode, s.ffb_strength,
                s.ffb_device.empty() ? s.device : s.ffb_device);
    input->AddDriver(std::make_unique<Driver>());
    input->SetDeviceAssignment(std::make_unique<Assignment>());
  }
  return input;
}
}
