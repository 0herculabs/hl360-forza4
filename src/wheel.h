#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace fm4::wheel {
// Independent of brand and SDK. Pedals may increase or decrease when pressed.
struct Config {
  bool enabled = false;
  std::string device;
  int steering_axis = 0, throttle_axis = 1, brake_axis = 2;
  int left = -32768, center = 0, right = 32767;
  int throttle_rest = -32768, throttle_full = 32767;
  int brake_rest = -32768, brake_full = 32767;
  int calibration = 0; // Seven explicit endpoint captures, never guessed.
  bool ffb = false, ffb_invert = false;
  int ffb_gain = 10; // Experimental device gain, limited to 100%.
  std::array<int, 16> buttons{{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}};
  int shift_up_action=5, shift_down_action=4; // RB / LB; match the game's wheel layout.
  bool clutch_enabled=false;
  int clutch_axis=-1, clutch_rest=-32768, clutch_full=32767, clutch_calibration=0;
  int clutch_action=1, clutch_threshold=50; // Digital B bridge; not a native analog clutch.
};
inline bool Valid(const Config& c) {
  for (int value : {c.left,c.center,c.right,c.throttle_rest,c.throttle_full,c.brake_rest,c.brake_full})
    if (value < -32768 || value > 32767) return false;
  const int a = c.center - c.left, b = c.right - c.center;
  return c.calibration == 127 && ((a > 1024 && b > 1024) || (a < -1024 && b < -1024)) &&
      std::abs(c.throttle_full-c.throttle_rest) > 1024 && std::abs(c.brake_full-c.brake_rest) > 1024 &&
      c.steering_axis >= 0 && c.throttle_axis >= 0 && c.brake_axis >= 0 &&
      std::max({c.steering_axis,c.throttle_axis,c.brake_axis}) < 64 &&
      c.steering_axis != c.throttle_axis && c.steering_axis != c.brake_axis && c.throttle_axis != c.brake_axis;
}
inline float Pedal(int value, int rest, int full) {
  if (std::abs(full-rest) <= 1024) return 0;
  return std::clamp(float(value-rest)/float(full-rest), 0.0f, 1.0f);
}
inline bool ValidClutch(const Config& c) {
  return c.clutch_enabled && c.clutch_calibration==3 && c.clutch_axis>=0 && c.clutch_axis<64 &&
      c.clutch_axis!=c.steering_axis && c.clutch_axis!=c.throttle_axis && c.clutch_axis!=c.brake_axis &&
      c.clutch_rest>=-32768 && c.clutch_rest<=32767 && c.clutch_full>=-32768 && c.clutch_full<=32767 &&
      std::abs(c.clutch_full-c.clutch_rest)>1024 && c.clutch_action>=0 && c.clutch_action<14;
}
inline float Steering(int value, const Config& c) {
  if (!Valid(c)) return 0;
  const float a = float(value-c.center)/float(c.left-c.center);
  const float b = float(value-c.center)/float(c.right-c.center);
  return std::clamp(a > 0 ? -a : b, -1.0f, 1.0f);
}
struct Device { std::string key, name; bool wheel = false; };
struct ForceEffect {
  int id = 0, type = 0, gain = 255;
  int magnitude = 0, offset = 0, positive_coefficient = 0, negative_coefficient = 0;
  int positive_saturation = 0, negative_saturation = 0, deadband = 0;
};
struct Snapshot {
  std::vector<Device> devices;
  std::string key, name, status;
  std::vector<int> axes;
  std::vector<bool> buttons;
  uint8_t hat = 0;
  bool connected = false;
  uint64_t generation = 0;
  std::string haptic_status;
  bool force_supported = false;
  bool test_busy = false;
};
// Raw SDL joysticks: supports MOZA and other brands without a gamepad mapping.
// Each instance has one SDL owner: a worker in the runtime, or the UI caller
// in the launcher. SDL haptics are not thread-safe.
class DeviceMonitor {
 public:
  // UI mode: all SDL calls, including destruction, stay on the caller thread.
  explicit DeviceMonitor(bool caller_thread = false);
  ~DeviceMonitor();
  void Pump(); // Call once per UI frame in caller-thread mode.
  DeviceMonitor(const DeviceMonitor&) = delete;
  void Select(std::string key);
  Snapshot Read() const;
  void SetActive(bool active);
  // Explicit, finite 20% pulse for the launcher's hardware diagnostic button.
  void TestHaptics();
  void ConfigureForce(bool enabled, int gain, bool inverted);
  void UpdateForce(ForceEffect effect);
  void OperateForce(int id, bool start);
  void ClearForce();
  void SetDriving(bool driving);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace fm4::wheel

