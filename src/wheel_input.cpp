#include "wheel_input.h"
#include "wheel.h"
#include <cmath>
#include <mutex>
#include <atomic>
#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(wheel_enabled, false, "FM4/Wheel", "Use a calibrated raw joystick as a steering wheel");
REXCVAR_DEFINE_STRING(wheel_device, "", "FM4/Wheel", "Selected wheel GUID and device path");
REXCVAR_DEFINE_BOOL(wheel_ffb, false, "FM4/Wheel", "Experimental native wheel force feedback");
REXCVAR_DEFINE_BOOL(wheel_ffb_invert, false, "FM4/Wheel", "Invert native constant torque");
REXCVAR_DEFINE_INT32(wheel_ffb_gain, 10, "FM4/Wheel", "Wheel device gain percent, limited to 100");
#define WHEEL_INT(name, value) REXCVAR_DEFINE_INT32(wheel_##name, value, "FM4/Wheel", #name)
WHEEL_INT(steering_axis,0); WHEEL_INT(throttle_axis,1); WHEEL_INT(brake_axis,2);
WHEEL_INT(left,-32768); WHEEL_INT(center,0); WHEEL_INT(right,32767);
WHEEL_INT(throttle_rest,-32768); WHEEL_INT(throttle_full,32767);
WHEEL_INT(brake_rest,-32768); WHEEL_INT(brake_full,32767); WHEEL_INT(calibration,0);
WHEEL_INT(button_a,-1); WHEEL_INT(button_b,-1); WHEEL_INT(button_x,-1); WHEEL_INT(button_y,-1);
WHEEL_INT(button_lb,-1); WHEEL_INT(button_rb,-1); WHEEL_INT(button_start,-1); WHEEL_INT(button_back,-1);
WHEEL_INT(button_ls,-1); WHEEL_INT(button_rs,-1);
WHEEL_INT(button_up,-1); WHEEL_INT(button_down,-1); WHEEL_INT(button_left,-1); WHEEL_INT(button_right,-1);
WHEEL_INT(button_shift_up,-1); WHEEL_INT(button_shift_down,-1);
WHEEL_INT(shift_up_action,5); WHEEL_INT(shift_down_action,4);
WHEEL_INT(clutch_axis,-1); WHEEL_INT(clutch_rest,-32768); WHEEL_INT(clutch_full,32767);
WHEEL_INT(clutch_calibration,0); WHEEL_INT(clutch_action,1); WHEEL_INT(clutch_threshold,50);
REXCVAR_DEFINE_BOOL(wheel_clutch_enabled,false,"FM4/Wheel","Enable calibrated clutch pedal to button bridge");
#undef WHEEL_INT

namespace fm4::wheel {
namespace {
using namespace rex::input;
constexpr DeviceId kWheelId = static_cast<DeviceId>(0xF400000000000001ull);
std::mutex service_mutex;
std::weak_ptr<DeviceMonitor> service;
std::atomic<bool> focused{false};
std::shared_ptr<DeviceMonitor> Service() { std::lock_guard lock(service_mutex);return service.lock(); }
class Assignment final : public DeviceAssignment {
 public:
  void OnDevicesChanged(const std::vector<DeviceInfo>& devices) override {
    devices_ = devices;
  }
  void DevicesForUser(uint32_t user, std::vector<DeviceId>& out) const override {
    out.clear();
    const bool wheel = std::any_of(devices_.begin(), devices_.end(), [](const DeviceInfo& d) {return d.id == kWheelId;});
    if (wheel) {
      // Keep wheel capabilities stable even when navigating with the keyboard.
      if (user == 0) {
        out.push_back(kWheelId);
      }
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
Config ReadConfig() {
  Config c;
  c.enabled = REXCVAR_GET(wheel_enabled); c.device = REXCVAR_GET(wheel_device);
  c.ffb=REXCVAR_GET(wheel_ffb);c.ffb_invert=REXCVAR_GET(wheel_ffb_invert);c.ffb_gain=REXCVAR_GET(wheel_ffb_gain);
#define READ(name) c.name = REXCVAR_GET(wheel_##name)
  READ(steering_axis); READ(throttle_axis); READ(brake_axis);
  READ(left); READ(center); READ(right); READ(throttle_rest); READ(throttle_full);
  READ(brake_rest); READ(brake_full); READ(calibration);
  READ(shift_up_action); READ(shift_down_action); READ(clutch_axis); READ(clutch_rest); READ(clutch_full);
  READ(clutch_calibration); READ(clutch_action); READ(clutch_threshold);
  c.clutch_enabled=REXCVAR_GET(wheel_clutch_enabled);
#undef READ
  c.buttons = {{REXCVAR_GET(wheel_button_a),REXCVAR_GET(wheel_button_b),REXCVAR_GET(wheel_button_x),REXCVAR_GET(wheel_button_y),
    REXCVAR_GET(wheel_button_lb),REXCVAR_GET(wheel_button_rb),REXCVAR_GET(wheel_button_start),REXCVAR_GET(wheel_button_back),
    REXCVAR_GET(wheel_button_ls),REXCVAR_GET(wheel_button_rs),
    REXCVAR_GET(wheel_button_up),REXCVAR_GET(wheel_button_down),REXCVAR_GET(wheel_button_left),REXCVAR_GET(wheel_button_right),
    REXCVAR_GET(wheel_button_shift_up),REXCVAR_GET(wheel_button_shift_down)}};
  return c;
}
class Driver final : public InputDriver {
 public:
  Driver() : InputDriver(nullptr,0), config_(ReadConfig()), monitor_(std::make_shared<DeviceMonitor>()) {
    monitor_->Select(config_.device);
    monitor_->ConfigureForce(config_.ffb && Valid(config_),config_.ffb_gain,config_.ffb_invert);
    monitor_->SetActive(focused);
    std::lock_guard lock(service_mutex);service=monitor_;
  }
  void OnWindowAvailable(rex::ui::Window* window) override { window_ = window; }
  rex::X_STATUS Setup() override { return 0; }
  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    auto s = monitor_->Read();
    const auto status = s.status + "; " + s.haptic_status;
    if (status != last_status_) {
      last_status_ = status;
      REXLOG_INFO("Wheel backend: {}", status);
    }
    if (!Ready(s)) return;
    out.push_back({kWheelId,0,s.name,s.key,false});
    if (s.generation != generation_) {
      generation_ = s.generation;
      REXLOG_INFO("Wheel connected: {} ({} axes, {} buttons); experimental native FFB requested={}",s.name,s.axes.size(),s.buttons.size(),config_.ffb);
    }
    if(s.force_supported!=force_reported_) {
      force_reported_=s.force_supported;
      REXLOG_INFO("Wheel force feedback: supported={}, status={}",s.force_supported,s.haptic_status);
    }
  }
  rex::X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out) override {
    const auto s = monitor_->Read();
    if (id != kWheelId || !Ready(s) || !out) return 1167;
    *out = {};
    out->packet_number = ++packet_;
    if (window_ && !focused) return 0;
    if (AxesReady(s)) {
    out->gamepad.thumb_lx = int16_t(std::lround(Steering(s.axes[config_.steering_axis],config_)*32767));
    out->gamepad.right_trigger = uint8_t(std::lround(Pedal(s.axes[config_.throttle_axis],config_.throttle_rest,config_.throttle_full)*255));
    out->gamepad.left_trigger = uint8_t(std::lround(Pedal(s.axes[config_.brake_axis],config_.brake_rest,config_.brake_full)*255));
    }
    constexpr uint16_t bits[]{0x1000,0x2000,0x4000,0x8000,0x100,0x200,0x10,0x20,0x40,0x80,1,2,4,8};
    uint16_t pressed = s.hat & 0xF;
    // SDL hat order up/right/down/left differs from XInput's up/down/left/right.
    pressed = ((s.hat&1)?1:0)|((s.hat&2)?8:0)|((s.hat&4)?2:0)|((s.hat&8)?4:0);
    for (size_t i=0;i<config_.buttons.size();++i) {
      const int index=config_.buttons[i];
      const int action=i<14?int(i):(i==14?config_.shift_up_action:config_.shift_down_action);
      if(action>=0 && action<14 && index>=0 && size_t(index)<s.buttons.size() && s.buttons[index]) pressed |= bits[action];
    }
    if(ValidClutch(config_) && size_t(config_.clutch_axis)<s.axes.size() &&
        Pedal(s.axes[config_.clutch_axis],config_.clutch_rest,config_.clutch_full)*100>=std::clamp(config_.clutch_threshold,1,100))
      pressed |= bits[config_.clutch_action];
    out->gamepad.buttons = pressed;
    return 0;
  }
  rex::X_RESULT GetDeviceCapabilities(DeviceId id,uint32_t,X_INPUT_CAPABILITIES* out) override {
    if (id != kWheelId || !out || !Ready(monitor_->Read())) return 1167;
    *out = {}; out->type = 1; out->sub_type = 2;
    out->gamepad.buttons = 0xF3FF; out->gamepad.thumb_lx = 32767;
    out->gamepad.left_trigger = out->gamepad.right_trigger = 255;
    out->flags = ForceAvailable(0) ? 1 : 0;
    return 0;
  }
  rex::X_RESULT SetDeviceVibration(DeviceId, X_INPUT_VIBRATION*) override { return 50; }
  rex::X_RESULT GetDeviceKeystroke(DeviceId,uint32_t,X_INPUT_KEYSTROKE*) override {return 4306;}
 private:
  bool Ready(const Snapshot& s) const {
    return !config_.device.empty() && s.connected && s.key == config_.device;
  }
  bool AxesReady(const Snapshot& s) const {
    return Valid(config_) && Ready(s) &&
      size_t(std::max({config_.steering_axis,config_.throttle_axis,config_.brake_axis})) < s.axes.size();
  }
  Config config_;
  std::shared_ptr<DeviceMonitor> monitor_;
  rex::ui::Window* window_ = nullptr;
  uint64_t generation_ = 0;
  bool force_reported_ = false;
  uint32_t packet_ = 0;
  std::string last_status_;
};
}
bool ForceAvailable(uint32_t user) {
  const auto device=Service();
  if(user!=0 || !REXCVAR_GET(wheel_ffb) || !device) return false;
  const auto state=device->Read();return state.force_supported && state.connected;
}
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
  if (!tool_mode && REXCVAR_GET(wheel_enabled)) {
    if (ReadConfig().device.empty()) REXLOG_WARN("No wheel selected; using normal input");
    else {
      if (!Valid(ReadConfig())) REXLOG_WARN("Wheel buttons available; calibrate axes to enable steering, pedals and FFB");
      REXLOG_INFO("Wheel input enabled: calibration={}, native FFB={}",
          ReadConfig().calibration, ReadConfig().ffb);
      input->AddDriver(std::make_unique<Driver>());
      input->SetDeviceAssignment(std::make_unique<Assignment>());
    }
  }
  return input;
}
}

