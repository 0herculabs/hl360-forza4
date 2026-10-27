#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "wheel_mapping.h"

namespace fm4::wheel {
// One connected game controller, as SDL reports it (raw joystick: works for
// MOZA, Fanatec, pedals, shifters and button boxes without a gamepad mapping).
struct Device {
  uint32_t id = 0;  // SDL instance id; never persisted
  std::string name, path;
  uint16_t vendor = 0, product = 0;
  bool wheel = false;  // SDL types it as a wheel
  std::vector<int16_t> axes, rest;  // rest: the axes when the device opened
  std::vector<bool> buttons;
  std::vector<uint8_t> hats;
};
// The text that names exactly this device among those connected (the
// launcher writes it): "vvvv:pppp", else its name, else "path:<path>".
std::string DeviceMatch(const Device& device, const std::vector<Device>& all);
const Device* FindDevice(const std::vector<Device>& devices, std::string_view match);

struct Snapshot {
  std::vector<Device> devices;
  uint64_t generation = 0;  // changes when a device connects or disconnects
  std::string status, haptic_status, ffb_name;
  bool force_supported = false;  // the force device plays constant force
  bool rumble_supported = false;
  bool test_busy = false;
};

// Each instance has one SDL owner: a worker thread in the game, or the UI
// caller in the launcher. SDL haptics are not thread-safe.
class DeviceMonitor {
 public:
  // UI mode: all SDL calls, including destruction, stay on the caller thread.
  explicit DeviceMonitor(bool caller_thread = false);
  ~DeviceMonitor();
  DeviceMonitor(const DeviceMonitor&) = delete;
  void Pump();  // call once per UI frame in caller-thread mode
  Snapshot Read() const;
  void SetActive(bool active);
  // The force device (FfbDevice matching) and how the game's effects play.
  void ConfigureForce(const Settings& settings);
  // Pulls the wheel LEFT, then RIGHT, at 15 % (reversed by ffb_invert).
  void TestHaptics();
  void UpdateForce(ForceEffect effect);
  void OperateForce(int id, bool start);
  void ClearForce();
  void SetDriving(bool driving);
  void SetRumble(uint16_t left, uint16_t right);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace fm4::wheel
