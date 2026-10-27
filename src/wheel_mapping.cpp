#include "wheel_mapping.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>

namespace fm4::wheel {
namespace {

std::string_view Trim(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
  return text;
}

std::string Lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool StartsWithNoCase(std::string_view text, std::string_view prefix) {
  if (text.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(text[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
      return false;
  }
  return true;
}

std::optional<int> ParseIndex(std::string_view text) {
  text = Trim(text);
  int value = -1;
  auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc() || end != text.data() + text.size() || value < 0 || value > 255) return std::nullopt;
  return value;
}

// Splits "device|rest" at the last '|', so a device name may not hold one.
void SplitDevice(std::string_view text, std::string& device, std::string_view& rest) {
  const size_t bar = text.rfind('|');
  if (bar == std::string_view::npos) {
    device.clear();
    rest = Trim(text);
    return;
  }
  device = std::string(Trim(text.substr(0, bar)));
  rest = Trim(text.substr(bar + 1));
}

struct Target {
  uint16_t xbuttons = 0;
  bool left_trigger = false, right_trigger = false;
  int8_t gear = 0;
};

std::optional<Target> TargetForName(std::string_view name) {
  const std::string lower = Lower(Trim(name));
  auto gear = [](int value) { Target t; t.gear = int8_t(value); return t; };
  if (lower == "reverse") return gear(-1);
  if (lower.size() == 5 && lower.compare(0, 4, "gear") == 0 && lower[4] >= '1' && lower[4] <= '7')
    return gear(lower[4] - '0');
  static const std::pair<const char*, uint16_t> names[] = {
      {"a", 0x1000}, {"b", 0x2000}, {"x", 0x4000}, {"y", 0x8000}, {"lb", 0x0100}, {"rb", 0x0200},
      {"ls", 0x0040}, {"rs", 0x0080}, {"back", 0x0020}, {"start", 0x0010}, {"up", 0x0001},
      {"down", 0x0002}, {"left", 0x0004}, {"right", 0x0008}};
  for (const auto& [n, bits] : names) {
    if (lower == n) { Target t; t.xbuttons = bits; return t; }
  }
  Target trigger;
  trigger.left_trigger = lower == "lt";
  trigger.right_trigger = lower == "rt";
  if (trigger.left_trigger || trigger.right_trigger) return trigger;
  return std::nullopt;
}

}  // namespace

std::optional<AxisBinding> ParseAxisBinding(std::string_view text) {
  text = Trim(text);
  if (text.empty()) return std::nullopt;
  AxisBinding binding;
  std::string_view rest;
  SplitDevice(text, binding.device, rest);
  if (!rest.empty() && (rest.front() == '+' || rest.front() == '-')) {
    binding.direction = rest.front() == '+' ? AxisDirection::kNormal : AxisDirection::kInverted;
    rest.remove_prefix(1);
  }
  if (StartsWithNoCase(rest, "axis")) rest.remove_prefix(4);
  else if (StartsWithNoCase(rest, "a")) rest.remove_prefix(1);
  else return std::nullopt;
  auto index = ParseIndex(rest);
  if (!index) return std::nullopt;
  binding.axis = *index;
  return binding;
}

std::vector<ButtonBinding> ParseButtonBindings(std::string_view text) {
  std::vector<ButtonBinding> out;
  // A comma-separated part without '=' continues the previous NAME ("A=0,1").
  std::optional<Target> target;
  size_t start = 0;
  while (start <= text.size()) {
    size_t comma = text.find(',', start);
    if (comma == std::string_view::npos) comma = text.size();
    std::string_view part = Trim(text.substr(start, comma - start));
    start = comma + 1;
    if (part.empty()) continue;
    std::string_view input = part;
    const size_t equals = part.find('=');
    if (equals != std::string_view::npos) {
      target = TargetForName(part.substr(0, equals));
      input = part.substr(equals + 1);
    }
    if (!target) continue;
    ButtonBinding binding;
    std::string_view rest;
    SplitDevice(input, binding.device, rest);
    binding.xbuttons = target->xbuttons;
    binding.left_trigger = target->left_trigger;
    binding.right_trigger = target->right_trigger;
    binding.gear = target->gear;
    std::string_view axis_text = rest;
    AxisDirection axis_direction = AxisDirection::kAuto;
    if (!axis_text.empty() && (axis_text.front() == '+' || axis_text.front() == '-')) {
      axis_direction = axis_text.front() == '+' ? AxisDirection::kNormal : AxisDirection::kInverted;
      axis_text.remove_prefix(1);
    }
    if (StartsWithNoCase(axis_text, "axis")) {
      auto axis = ParseIndex(axis_text.substr(4));
      if (!axis) continue;
      binding.axis = *axis;
      binding.axis_direction = axis_direction;
    } else if (StartsWithNoCase(rest, "h")) {
      rest.remove_prefix(1);
      const size_t dot = rest.find('.');
      if (dot == std::string_view::npos) continue;
      auto hat = ParseIndex(rest.substr(0, dot));
      const std::string direction = Lower(Trim(rest.substr(dot + 1)));
      uint8_t mask = 0;
      if (direction == "up" || direction == "1") mask = 0x01;
      else if (direction == "right" || direction == "2") mask = 0x02;
      else if (direction == "down" || direction == "4") mask = 0x04;
      else if (direction == "left" || direction == "8") mask = 0x08;
      if (!hat || !mask) continue;
      binding.hat = *hat;
      binding.hat_mask = mask;
    } else {
      if (StartsWithNoCase(rest, "b")) rest.remove_prefix(1);
      auto button = ParseIndex(rest);
      if (!button) continue;
      binding.button = *button;
    }
    out.push_back(std::move(binding));
    if (comma == text.size()) break;
  }
  return out;
}

uint16_t ButtonsFromNames(std::string_view text) {
  uint16_t bits = 0;
  size_t start = 0;
  while (start < text.size()) {
    size_t plus = text.find_first_of("+,", start);
    if (plus == std::string_view::npos) plus = text.size();
    if (auto t = TargetForName(text.substr(start, plus - start))) bits |= t->xbuttons;
    start = plus + 1;
  }
  return bits;
}

bool DeviceMatches(std::string_view match, std::string_view name, uint16_t vendor, uint16_t product,
                   std::string_view path) {
  match = Trim(match);
  if (match.empty()) return true;
  if (StartsWithNoCase(match, "path:")) return !path.empty() && Lower(Trim(match.substr(5))) == Lower(path);
  if (match.size() == 9 && match[4] == ':') {
    auto hex = [](std::string_view digits, uint16_t& value) {
      unsigned parsed = 0;
      auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), parsed, 16);
      if (error != std::errc() || end != digits.data() + digits.size() || parsed > 0xFFFF) return false;
      value = uint16_t(parsed);
      return true;
    };
    uint16_t want_vendor = 0, want_product = 0;
    if (hex(match.substr(0, 4), want_vendor) && hex(match.substr(5, 4), want_product))
      return want_vendor == vendor && want_product == product;
  }
  return Lower(name).find(Lower(match)) != std::string::npos;
}

std::string MigrateDeviceKey(std::string_view key) {
  const size_t bar = key.find('|');
  if (bar != 32) return std::string(key);
  for (size_t i = 0; i < 32; ++i) {
    if (!std::isxdigit(static_cast<unsigned char>(key[i]))) return std::string(key);
  }
  return "path:" + std::string(key.substr(33));
}

AxisDirection ResolveAxisDirection(AxisDirection direction, int16_t rest) {
  if (direction != AxisDirection::kAuto) return direction;
  return rest > 0 ? AxisDirection::kInverted : AxisDirection::kNormal;
}

int16_t SteeringFromAxis(int16_t raw, bool invert, int scale_percent, int deadzone_percent) {
  int32_t value = raw;
  if (invert) value = -value - 1;
  const int32_t deadzone = std::clamp(deadzone_percent, 0, 50) * 32767 / 100;
  const int32_t magnitude = std::abs(value);
  if (magnitude <= deadzone) return 0;
  int64_t scaled = int64_t(magnitude - deadzone) * 32767 / (32767 - deadzone);
  scaled = scaled * std::clamp(scale_percent, 10, 1000) / 100;
  scaled = std::min<int64_t>(scaled, 32767);
  return int16_t(value < 0 ? -scaled : scaled);
}

uint8_t PedalFromAxis(int16_t raw, AxisDirection direction, int bottom_percent, int top_percent) {
  int32_t travel = int32_t(raw) + 32768;
  if (direction == AxisDirection::kInverted) travel = 65535 - travel;
  const int32_t bottom = std::clamp(bottom_percent, 0, 50) * 65535 / 100;
  const int32_t top = std::clamp(top_percent, 0, 50) * 65535 / 100;
  const int32_t span = std::max(65535 - bottom - top, 1);
  if (travel <= bottom) return 0;
  if (travel >= 65535 - top) return 255;
  return uint8_t(std::clamp((travel - bottom) * 255 / span, 0, 255));
}

uint16_t DpadFromHat(uint8_t hat) {
  return uint16_t(((hat & 1) ? 0x0001 : 0) | ((hat & 2) ? 0x0008 : 0) | ((hat & 4) ? 0x0002 : 0) |
                  ((hat & 8) ? 0x0004 : 0));
}

FfbMode ParseFfbMode(std::string_view text) {
  const std::string mode = Lower(Trim(text));
  if (mode == "off") return FfbMode::kOff;
  if (mode == "rumble") return FfbMode::kRumble;
  return FfbMode::kEffects;  // "on", and FH1's "difference"
}

const char* FfbModeName(FfbMode mode) {
  switch (mode) {
    case FfbMode::kOff: return "off";
    case FfbMode::kRumble: return "rumble";
    default: return "on";
  }
}

ForceTuning TuningFrom(const Settings& s) {
  return {std::clamp(s.ffb_strength, 0, 200), std::clamp(s.ffb_min_force, 0, 30), std::clamp(s.ffb_spring, 0, 200),
          std::clamp(s.ffb_damper, 0, 200), std::clamp(s.ffb_friction, 0, 200), std::clamp(s.ffb_inertia, 0, 200),
          s.ffb_invert};
}

namespace {
constexpr int kFullScale = 127;  // FM4's effect magnitudes and coefficients
int64_t Scale127(int value) { return std::clamp<int64_t>(int64_t(value) * 32767 / kFullScale, -32767, 32767); }
int64_t Gain(int64_t value, int gain) { return value * std::clamp(gain, 0, 255) / 255; }
}  // namespace

int16_t EffectConstantLevel(const ForceEffect& effect, const ForceTuning& tuning) {
  int64_t level = Gain(Scale127(effect.magnitude), effect.gain);
  if (level != 0 && tuning.min_force > 0) {
    const int64_t floor = int64_t(32767) * std::clamp(tuning.min_force, 0, 100) / 100;
    const int64_t magnitude = floor + std::abs(level) * (32767 - floor) / 32767;
    level = level < 0 ? -magnitude : magnitude;
  }
  level = level * std::clamp(tuning.strength, 0, 200) / 100;
  if (tuning.invert) level = -level;
  return int16_t(std::clamp<int64_t>(level, -32767, 32767));
}

Condition EffectCondition(const ForceEffect& effect, const ForceTuning& tuning) {
  int share = 100;
  switch (effect.type) {
    case kSpring: share = tuning.spring; break;
    case kDamper: share = tuning.damper; break;
    case kFriction: share = tuning.friction; break;
    case kInertia: share = tuning.inertia; break;
  }
  auto coeff = [&](int value) {
    int64_t scaled = Gain(Scale127(value), effect.gain) * std::clamp(tuning.strength, 0, 200) / 100;
    scaled = scaled * std::clamp(share, 0, 400) / 100;
    return int16_t(std::clamp<int64_t>(scaled, -32767, 32767));
  };
  auto saturation = [&](int value) {
    return uint16_t(std::clamp(value, 0, 255) * 65535 / 255);
  };
  Condition c;
  c.right_coeff = coeff(effect.positive_coefficient);
  c.left_coeff = coeff(effect.negative_coefficient);
  c.right_sat = saturation(effect.positive_saturation);
  c.left_sat = saturation(effect.negative_saturation);
  c.deadband = uint16_t(std::clamp(effect.deadband, 0, 255) * 32767 / 255);
  c.center = int16_t(Scale127(effect.offset) * (tuning.invert ? -1 : 1));
  return c;
}

int16_t RumbleMagnitude(uint16_t left, uint16_t right, const Settings& s) {
  const int64_t road = int64_t(left) * std::clamp(s.ffb_rumble_road, 0, 100) / 100;
  const int64_t engine = int64_t(right) * std::clamp(s.ffb_rumble_engine, 0, 100) / 100;
  int64_t magnitude = std::max(road, engine) * 32767 / 65535;
  magnitude = magnitude * std::clamp(s.ffb_rumble, 0, 100) / 100;
  magnitude = magnitude * std::clamp(s.ffb_strength, 0, 100) / 100;
  return int16_t(std::clamp<int64_t>(magnitude, 0, 32767));
}

}  // namespace fm4::wheel
