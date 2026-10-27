// SimHub telemetry: reads the player's car each frame and sends it in Forza's
// Data Out layout (telemetry_out.cpp). The car comes from FM4's rumble
// builder, which runs once a frame for the pad's cars; the offsets were
// carried over from Pinyon Shift's FH1 findings by matching FM4's code to
// FH1's (doc/telemetry.md).
#include "fm4_car.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>

#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "generated/default/fm4_pch.h"
#include "telemetry_out.h"

namespace {
using namespace fm4::telemetry;

// The car's dynamics object (FH1's ICarDynamics): [car + 0x2D50].
constexpr uint32_t kDynamics = 0x2D50;
// The car's driver type, 0 for the player's car (FH1 +0x2DD0; both sit 0x40
// past the dynamics pointer, and FM4 compares it with 0 the way FH1 does).
constexpr uint32_t kDriverType = 0x2D90;
constexpr uint32_t kEngineSpeed = 0x144;   // f32 rad/s
constexpr uint32_t kRedline = 0x1C4;       // f32 rad/s
constexpr uint32_t kLimiter = 0x1C8;       // f32 rad/s
constexpr uint32_t kIdle = 0x4CC;          // f32 rad/s
constexpr uint32_t kGear = 0x8BC;          // u8: 0 reverse, 1-8, 9 neutral (as FH1)
constexpr uint32_t kSpeed = 0xC18;         // f32 m/s
// The car's controls as the game built them: f32 gas, brake, steer, clutch, handbrake.
constexpr uint32_t kControls = 0xE90;
// Four wheel blocks, 0 and 1 the front axle.
constexpr uint32_t kWheels = 0xEB0;
constexpr uint32_t kWheelStride = 0x420;
constexpr uint32_t kWheelSuspension = 0x0;      // f32 suspension position
constexpr uint32_t kWheelSpeed = 0xC;           // f32 rad/s
constexpr uint32_t kWheelSlip = 0x210;          // f32 slip ratio over the tyre's peak
constexpr uint32_t kWheelSlipAngle = 0x228;     // f32 slip angle over the tyre's peak
constexpr uint32_t kWheelContact = 0x33C;       // u8
constexpr uint32_t kWheelSuspensionRest = 0x37C;  // f32 the spring's rest point
constexpr float kRadPerSecToRpm = 9.5492966f;

std::atomic<bool> g_driving{false};

// A guest address the host can read: allocated in a guest heap and readable.
bool Readable(uint32_t p, uint32_t size) {
  if (p < 0x10000u || (p & 3) || p > 0xFFFFFFFFu - size) return false;
  auto* heap = rex::system::kernel_memory()->LookupHeap(p);
  return heap && heap->QueryRangeAccess(p, p + size - 1) != rex::memory::PageAccess::kNoAccess;
}

float F32(uint8_t* base, uint32_t address) {
  const float value = std::bit_cast<float>(uint32_t(REX_LOAD_U32(address)));
  return std::isfinite(value) ? value : 0.0f;
}

uint32_t TimestampSeconds() {
  using namespace std::chrono;
  return uint32_t(duration_cast<seconds>(steady_clock::now().time_since_epoch()).count());
}

uint32_t g_logged = 0;
bool LogDue() {
  if (!LogValues() || TimestampSeconds() < g_logged + 5) return false;
  g_logged = TimestampSeconds();
  return true;
}

void Sample(uint8_t* base, uint32_t owner) {
  // The builder's object holds its cars as a vector at +0x68 (begin, end);
  // the player's car is the one whose driver type is 0.
  const uint32_t begin = REX_LOAD_U32(owner + 0x68), end = REX_LOAD_U32(owner + 0x6C);
  const uint32_t count = end > begin && end - begin <= 256 && Readable(begin, end - begin) ? (end - begin) / 4 : 0;
  uint32_t car = 0, dyn = 0, first = 0;
  int driver = -1;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t c = REX_LOAD_U32(begin + 4 * i);
    if (!Readable(c, kDriverType + 4)) continue;
    if (!first) first = c;
    if (REX_LOAD_U32(c + kDriverType) == 0) { car = c; break; }
  }
  if (!car) car = first;  // no car typed as the player's: the pad's first car
  if (car) {
    driver = int(REX_LOAD_U32(car + kDriverType));
    const uint32_t d = REX_LOAD_U32(car + kDynamics);
    if (Readable(d, kWheels + 4 * kWheelStride)) dyn = d;
  }
  if (!dyn) {
    Send({});
    if (LogDue())
      REXLOG_INFO("Telemetry: no car (builder {:08X}, {} cars at {:08X}, car {:08X} driver {}, dynamics {:08X}, driving {})",
                  owner, count, begin, car, driver, car ? uint32_t(REX_LOAD_U32(car + kDynamics)) : 0u, g_driving.load());
    return;
  }
  CarSample s;
  s.rpm = F32(base, dyn + kEngineSpeed) * kRadPerSecToRpm;
  s.idle_rpm = F32(base, dyn + kIdle) * kRadPerSecToRpm;
  s.redline_rpm = F32(base, dyn + kRedline) * kRadPerSecToRpm;
  if (s.redline_rpm <= 0) s.redline_rpm = F32(base, dyn + kLimiter) * kRadPerSecToRpm;
  const uint8_t gear = REX_LOAD_U8(dyn + kGear);
  s.gear = gear == 0 ? -1 : gear >= 9 ? 0 : int(gear);
  s.speed_mps = F32(base, dyn + kSpeed);
  // The game's driving flag, or a car that moves (in case the flag misses a mode).
  s.driving = g_driving || std::fabs(s.speed_mps) > 0.3f;
  const float direction = s.gear < 0 ? -1.0f : 1.0f;
  s.velocity_forward_mps = direction * s.speed_mps;
  // Controls are checked: an offset that moved would read as nonsense, not as
  // a stuck pedal.
  float controls[5];
  bool sane = true;
  for (int i = 0; i < 5; ++i) {
    controls[i] = F32(base, dyn + kControls + 4 * i);
    sane = sane && std::fabs(controls[i]) <= 1.5f;
  }
  if (sane) {
    s.throttle = controls[0];
    s.brake = controls[1];
    s.steer = controls[2];
    s.clutch = controls[3];
    s.handbrake = controls[4];
  }
  // Suspension travel normalised over the range this car has shown, since the
  // game's travel limits are not traced; reset when the car changes.
  static uint32_t travel_car = 0;
  static std::array<float, 4> travel_min{}, travel_max{};
  if (travel_car != dyn) {
    travel_car = dyn;
    travel_min.fill(-0.02f);
    travel_max.fill(0.02f);
  }
  for (uint32_t w = 0; w < 4; ++w) {
    const uint32_t wheel = dyn + kWheels + w * kWheelStride;
    const bool contact = REX_LOAD_U8(wheel + kWheelContact) != 0;
    s.wheel_slip[w] = contact ? direction * F32(base, wheel + kWheelSlip) : 0.0f;
    s.wheel_slip_angle[w] = contact ? F32(base, wheel + kWheelSlipAngle) : 0.0f;
    s.wheel_speed_rad_s[w] = F32(base, wheel + kWheelSpeed);
    const float travel = F32(base, wheel + kWheelSuspension) - F32(base, wheel + kWheelSuspensionRest);
    travel_min[w] = std::min(travel_min[w], travel);
    travel_max[w] = std::max(travel_max[w], travel);
    s.suspension_m[w] = travel;
    s.suspension_normalized[w] = (travel - travel_min[w]) / (travel_max[w] - travel_min[w]);
  }
  if (MovingOnly() && std::fabs(s.speed_mps) < 0.5f) s.driving = false;
  Send(s);
  if (LogDue()) {
    REXLOG_INFO("Telemetry: {} (driving flag {}, car {:08X} driver {}, {} cars) rpm {:.0f} idle {:.0f} redline {:.0f} gear {} (raw {}) speed {:.1f} m/s; "
                "gas {:.2f} brake {:.2f} steer {:.2f} clutch {:.2f} handbrake {:.2f}{}; "
                "wheel speed {:.1f} {:.1f} {:.1f} {:.1f}; slip {:.2f} {:.2f} {:.2f} {:.2f}; "
                "angle {:.2f} {:.2f} {:.2f} {:.2f}; suspension {:.3f} {:.3f} {:.3f} {:.3f}",
                s.driving ? "race on" : "race off", g_driving.load(), car, driver, count, s.rpm, s.idle_rpm, s.redline_rpm, s.gear, gear, s.speed_mps, controls[0], controls[1], controls[2],
                controls[3], controls[4], sane ? "" : " (out of range, not sent)", s.wheel_speed_rad_s[0],
                s.wheel_speed_rad_s[1], s.wheel_speed_rad_s[2], s.wheel_speed_rad_s[3], s.wheel_slip[0],
                s.wheel_slip[1], s.wheel_slip[2], s.wheel_slip[3], s.wheel_slip_angle[0], s.wheel_slip_angle[1],
                s.wheel_slip_angle[2], s.wheel_slip_angle[3], s.suspension_m[0], s.suspension_m[1],
                s.suspension_m[2], s.suspension_m[3]);
  }
}
}  // namespace

namespace fm4::telemetry {
void NoteDriving(bool driving) { g_driving = driving; }
}

// FM4's per-frame rumble builder (FH1's sub_8248AB08): r3 holds the pad's cars.
extern "C" void __imp__sub_826DB940(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void sub_826DB940(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t owner = ctx.r3.u32;
  __imp__sub_826DB940(ctx, base);
  if (!Wanted() || !Readable(owner, 0x70)) return;
  Sample(base, owner);
}
