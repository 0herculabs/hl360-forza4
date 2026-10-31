// Test input: a virtual pad that presses buttons on a schedule, for driving
// the game into a menu or a race without a player (renderer checks).
//
//   --input_script "8:start,12:a,15:a,30:rt+20"
//
// Each entry is seconds:control[+hold]; times count from the game's first
// input poll. Controls: a b x y start back up down left right lb rb ls rs lt
// rt (triggers full), lx- lx+ ly- ly+ (left stick to an end). A press without
// +hold lasts 0.15 s. It is a synthetic device, so it feeds player one beside
// any real controller.
#include "scripted_input.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(input_script, "", "FM4/Debug",
                      "Test input: seconds:control[+hold],... pressed by a virtual pad "
                      "(see src/scripted_input.cpp)");

namespace fm4 {
namespace {

using namespace rex::input;
constexpr DeviceId kScriptId = static_cast<DeviceId>(0xF400000000000002ull);

struct Step {
  double at = 0, hold = 0.15;
  uint16_t buttons = 0;
  uint8_t left_trigger = 0, right_trigger = 0;
  int16_t lx = 0, ly = 0;
};

std::optional<Step> ParseControl(const std::string& name) {
  static const std::pair<const char*, uint16_t> kButtons[] = {
      {"up", 0x0001},  {"down", 0x0002}, {"left", 0x0004}, {"right", 0x0008}, {"start", 0x0010},
      {"back", 0x0020}, {"ls", 0x0040},  {"rs", 0x0080},   {"lb", 0x0100},    {"rb", 0x0200},
      {"a", 0x1000},   {"b", 0x2000},    {"x", 0x4000},    {"y", 0x8000}};
  Step step;
  for (const auto& [button, mask] : kButtons) {
    if (name == button) {
      step.buttons = mask;
      return step;
    }
  }
  if (name == "lt") step.left_trigger = 255;
  else if (name == "rt") step.right_trigger = 255;
  else if (name == "lx-") step.lx = -32767;
  else if (name == "lx+") step.lx = 32767;
  else if (name == "ly-") step.ly = -32767;
  else if (name == "ly+") step.ly = 32767;
  else return std::nullopt;
  return step;
}

std::vector<Step> ParseScript(const std::string& text) {
  std::vector<Step> steps;
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find(',', at);
    if (end == std::string::npos) end = text.size();
    const std::string entry = text.substr(at, end - at);
    at = end + 1;
    const size_t colon = entry.find(':');
    if (colon == std::string::npos) continue;
    std::string control = entry.substr(colon + 1);
    double hold = 0.15;
    if (const size_t plus = control.find('+', 1); plus != std::string::npos && plus + 1 < control.size() &&
                                                  control[plus + 1] >= '0' && control[plus + 1] <= '9') {
      hold = std::atof(control.c_str() + plus + 1);
      control.resize(plus);
    }
    auto step = ParseControl(control);
    if (!step) {
      REXLOG_WARN("input_script: unknown control '{}'", control);
      continue;
    }
    step->at = std::atof(entry.c_str());
    step->hold = hold;
    steps.push_back(*step);
  }
  return steps;
}

class ScriptDriver final : public InputDriver {
 public:
  explicit ScriptDriver(std::vector<Step> steps) : InputDriver(nullptr, 0), steps_(std::move(steps)) {}
  rex::X_STATUS Setup() override { return 0; }
  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    DeviceInfo info;
    info.id = kScriptId;
    info.name = "Input script";
    info.guid = "script";
    info.synthetic = true;
    out.push_back(info);
  }
  rex::X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out) override {
    if (id != kScriptId || !out) return 1167;
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(mutex_);
    if (!start_) start_ = now;
    const double t = std::chrono::duration<double>(now - *start_).count();
    Step now_held;
    for (size_t i = 0; i < steps_.size(); ++i) {
      const Step& s = steps_[i];
      if (t < s.at || t >= s.at + s.hold) continue;
      if (!(logged_ & (1ull << (i % 64)))) {
        logged_ |= 1ull << (i % 64);
        REXLOG_INFO("input_script: step {} at {:.1f} s", i + 1, t);
      }
      now_held.buttons |= s.buttons;
      now_held.left_trigger = std::max(now_held.left_trigger, s.left_trigger);
      now_held.right_trigger = std::max(now_held.right_trigger, s.right_trigger);
      if (s.lx) now_held.lx = s.lx;
      if (s.ly) now_held.ly = s.ly;
    }
    *out = {};
    out->packet_number = ++packet_;
    out->gamepad.buttons = now_held.buttons;
    out->gamepad.left_trigger = now_held.left_trigger;
    out->gamepad.right_trigger = now_held.right_trigger;
    out->gamepad.thumb_lx = now_held.lx;
    out->gamepad.thumb_ly = now_held.ly;
    return 0;
  }
  rex::X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t, X_INPUT_CAPABILITIES* out) override {
    if (id != kScriptId || !out) return 1167;
    *out = {};
    out->type = 1;
    out->sub_type = 1;
    out->gamepad.buttons = 0xF3FF;
    out->gamepad.left_trigger = out->gamepad.right_trigger = 255;
    out->gamepad.thumb_lx = out->gamepad.thumb_ly = 32767;
    return 0;
  }
  rex::X_RESULT SetDeviceVibration(DeviceId, X_INPUT_VIBRATION*) override { return 0; }
  rex::X_RESULT GetDeviceKeystroke(DeviceId, uint32_t, X_INPUT_KEYSTROKE*) override { return 4306; }

 private:
  std::vector<Step> steps_;
  std::mutex mutex_;
  std::optional<std::chrono::steady_clock::time_point> start_;
  uint32_t packet_ = 0;
  uint64_t logged_ = 0;
};

}  // namespace

void AddScriptedInput(rex::input::InputSystem& input) {
  const std::string script = REXCVAR_GET(input_script);
  if (script.empty()) return;
  auto steps = ParseScript(script);
  REXLOG_INFO("input_script: {} steps", steps.size());
  input.AddDriver(std::make_unique<ScriptDriver>(std::move(steps)));
}

}  // namespace fm4
