#pragma once
// Racing wheel bindings and force feedback maths, shared by the game and the
// launcher's Wheel screen so the live view shows what the game will receive.
// Ported from the Pinyon Shift racing wheel work (shiftglue-sdk
// src/input/sdl/wheel_mapping.cpp): the same config keys and binding syntax.
//
// A wheel's parts may come from several USB devices (a direct-drive base, a
// separate pedal set, a shifter or button box), so each binding names the
// device it reads. Nothing here touches SDL.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fm4::wheel {

// Which way an axis travels as the pedal goes down.
enum class AxisDirection : uint8_t {
  kAuto,      // decided from the axis's resting value
  kNormal,    // -32768 released, 32767 pressed
  kInverted,  // 32767 released, -32768 pressed
};

// "[device|][+|-]axisN": an axis on the steering device, or on the device
// matched by `device` (see DeviceMatches).
struct AxisBinding {
  std::string device;
  int axis = -1;
  AxisDirection direction = AxisDirection::kAuto;
};

// One entry of wheel_buttons, "NAME=[device|]input": button N, a hat
// direction ("hN.up"), or an axis past half travel ("[+|-]axisN", e.g. a
// handbrake lever). NAME is an Xbox control (A, B, X, Y, LB, RB, LT, RT, LS,
// RS, BACK, START, UP, DOWN, LEFT, RIGHT), an H-shifter gate (GEAR1 to
// GEAR7, REVERSE) or a camera action (CAM_FOV_IN, ..., see CameraAction).
struct ButtonBinding {
  std::string device;
  int button = -1;
  int hat = -1;
  uint8_t hat_mask = 0;
  int axis = -1;
  AxisDirection axis_direction = AxisDirection::kAuto;
  uint16_t xbuttons = 0;
  bool left_trigger = false, right_trigger = false;
  int8_t gear = 0;  // -1 reverse, 1-7 a gear, 0 a pad control
  int8_t camera = -1;  // a CameraAction, -1 none
};

// The in-game camera adjustments (camera_view.cpp), bound to keys
// (keybind_camera_*) and wheel buttons (CAM_*). Held, they repeat.
enum class CameraAction : uint8_t {
  kFovIn,       // CAM_FOV_IN: narrower field of view
  kFovOut,      // CAM_FOV_OUT: wider
  kFovReset,    // CAM_FOV_RESET: the game's own
  kSeatUp,      // CAM_SEAT_UP
  kSeatDown,    // CAM_SEAT_DOWN
  kSeatForward, // CAM_SEAT_FORWARD
  kSeatBack,    // CAM_SEAT_BACK
  kSeatReset,   // CAM_SEAT_RESET: the game's own position
  kCount,
};
// "CAM_FOV_IN" and so on, by action.
const char* CameraActionName(CameraAction action);

std::optional<AxisBinding> ParseAxisBinding(std::string_view text);
// Malformed entries are skipped, so a typo loses one button, not all of them.
std::vector<ButtonBinding> ParseButtonBindings(std::string_view text);
// XInput button bits for "A", "B+LB" and so on; 0 for none.
uint16_t ButtonsFromNames(std::string_view text);

// `match` is empty (any device), "vvvv:pppp" in hex, "path:<SDL device
// path>" (exact, case-insensitive), or a case-insensitive part of the name.
bool DeviceMatches(std::string_view match, std::string_view name, uint16_t vendor,
                   uint16_t product, std::string_view path);
// FM4 0.3 stored the wheel as "<SDL GUID>|<path>"; read as "path:<path>".
std::string MigrateDeviceKey(std::string_view key);

AxisDirection ResolveAxisDirection(AxisDirection direction, int16_t rest);
// Steering axis to the guest's left stick X. `scale_percent` above 100 needs
// less rotation for full lock; the deadzone is a percentage of half travel.
int16_t SteeringFromAxis(int16_t raw, bool invert, int scale_percent, int deadzone_percent);
struct Settings;
// The steering scale in percent: the sensitivity, times the wheel's rotation
// over the game's when game_rotation is set (900 over 360: 250 %).
int SteeringScale(const Settings& s);
// A pedal axis to a trigger value, 0 at rest and 255 fully pressed, with a
// dead zone at rest (bottom) and one at full travel (top), in percent.
uint8_t PedalFromAxis(int16_t raw, AxisDirection direction, int bottom_percent, int top_percent);
// XInput D-pad bits for an SDL hat value (up 1, right 2, down 4, left 8).
uint16_t DpadFromHat(uint8_t hat);

// The wheel_* and telemetry_* settings, as fm4.user.toml stores them.
struct Settings {
  bool enabled = false;
  std::string device;  // the wheel base: steering and, by default, forces
  int steering_axis = 0;
  bool steering_invert = false;
  int steering_scale = 100, steering_deadzone = 0;
  // The rotation set in the wheel's software and the in-game steering wheel's
  // lock to lock, in degrees; with game_rotation set, steering is scaled so the
  // game's wheel turns as far as the real one (0: the wheel's full rotation is
  // the game's full lock).
  int wheel_rotation = 900, game_rotation = 0;
  std::string throttle, brake, clutch;
  std::string buttons = "UP=h0.up,DOWN=h0.down,LEFT=h0.left,RIGHT=h0.right";
  // The clutch pedal past half travel also presses these Xbox buttons; FM4's
  // own clutch is a button. With clutch_analog it is also sent as the right
  // stick's Y axis, where FH1 reads an analog clutch (unconfirmed in FM4).
  std::string clutch_button = "B";
  bool clutch_analog = false;
  int pedal_deadzone = 3;
  // Per pedal (throttle, brake, clutch): bottom -1 uses pedal_deadzone.
  std::array<int, 3> deadzone_bottom{{-1, -1, -1}}, deadzone_top{{0, 0, 0}};

  std::string ffb_device;     // empty: the wheel base
  std::string ffb_mode = "on";  // on (the game's own wheel effects), rumble, off
  int ffb_strength = 25, ffb_min_force = 0;
  int ffb_spring = 100, ffb_damper = 100, ffb_friction = 100, ffb_inertia = 100;
  int ffb_rumble = 50, ffb_rumble_engine = 100, ffb_rumble_road = 100;
  bool ffb_invert = false;
  bool log_inputs = false;

  bool telemetry_udp = false;
  std::string telemetry_format = "fm7";  // fm7 (311 bytes) or horizon (324)
  std::string telemetry_target = "127.0.0.1:5300";
  bool telemetry_moving_only = false;
};

enum class FfbMode : uint8_t { kOff, kEffects, kRumble };
FfbMode ParseFfbMode(std::string_view text);
const char* FfbModeName(FfbMode mode);

// One of the game's five native effect slots (XInput-FF), decoded from guest
// memory: FM4 quantizes forces and coefficients to +/-127, gain and
// saturations to 255.
struct ForceEffect {
  int id = 0, type = 0, gain = 255;
  int magnitude = 0, offset = 0, positive_coefficient = 0, negative_coefficient = 0;
  int positive_saturation = 0, negative_saturation = 0, deadband = 0;
};
enum EffectType { kConstant = 1, kSpring = 8, kDamper = 9, kInertia = 10, kFriction = 11 };

// How the game's effects become SDL haptic values.
struct ForceTuning {
  int strength = 25, min_force = 0;
  int spring = 100, damper = 100, friction = 100, inertia = 100;
  bool invert = false;
};
ForceTuning TuningFrom(const Settings& s);
// The Test's constant-force level: Strength, 100 % being half the base's
// maximum and 200 % all of it.
int16_t TestForceLevel(const Settings& s);
// SDL constant-force level: magnitude x gain, lifted to min_force, scaled by
// strength (up to 200 %, clipped at full scale), reversed by invert. SDL and DirectInput name the side a force
// comes from, as the game's effects do.
int16_t EffectConstantLevel(const ForceEffect& effect, const ForceTuning& tuning);
struct Condition {
  int16_t right_coeff = 0, left_coeff = 0, center = 0;
  uint16_t right_sat = 0, left_sat = 0, deadband = 0;
};
// SDL condition values (spring, damper, inertia, friction): gain, strength
// and that effect's own share scale the coefficients.
Condition EffectCondition(const ForceEffect& effect, const ForceTuning& tuning);
// The rumble played as a vibration: the stronger motor after the engine
// (right) and road (left) shares, 0-32767.
int16_t RumbleMagnitude(uint16_t left, uint16_t right, const Settings& s);

}  // namespace fm4::wheel
