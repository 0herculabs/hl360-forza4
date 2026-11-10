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
  bool ready = false;       // the first device list is in
  std::string status, haptic_status, ffb_name;
  bool force_supported = false;  // the force device plays constant force
  bool rumble_supported = false;
  bool test_busy = false;
};

// Each instance has one SDL owner: its worker thread, or the caller (tests).
// SDL haptics are not thread-safe.
class DeviceMonitor {
 public:
  // Launcher mode opens the force device only for TestHaptics, so the game
  // can have it. A caller-thread monitor makes every SDL call, destruction
  // included, from Pump's thread. The launcher keeps a worker: Windows can
  // take tens of seconds to list controllers when a device stops answering.
  explicit DeviceMonitor(bool launcher = false, bool caller_thread = false);
  ~DeviceMonitor();
  DeviceMonitor(const DeviceMonitor&) = delete;
  void Pump();  // call once per frame in caller-thread mode
  Snapshot Read() const;
  void SetActive(bool active);
  // The force device (FfbDevice matching) and how the game's effects play.
  void ConfigureForce(const Settings& settings);
  // Pulls the wheel LEFT, then RIGHT, at the strength set (TestForceLevel;
  // reversed by ffb_invert).
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
