// Frame pacing at 120 FPS. FM4 ticks its main loop once per guest vblank
// (fh1_guest_vblank_hz, 60 or 120 from the launcher). Races step their cars by
// the measured time, but the menus' UI and the showroom camera assume a fixed
// 1/60 s per update, so at 120 they ran twice as fast. Above 60 Hz the hooks
// below give them the time the main loop measured instead, as
// fm4-static-recomp does (src/game_hooks.cpp). At 60 nothing changes.
// The hooks are declared in config/codegen.toml.

#include <algorithm>
#include <atomic>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/ppc/context.h>

namespace {

// The main loop's measured milliseconds since the UI's and the showroom's
// last update.
std::atomic<uint32_t> g_ui_pending_ms{0};
std::atomic<uint32_t> g_showroom_pending_ms{0};

// The GPU plugin defines the cvar, so it is read by name; the launcher sets it
// before the game starts.
bool AboveConsoleRate() {
  static const bool above = rex::cvar::Query<double>("fh1_guest_vblank_hz") > 61.0;
  return above;
}

// At most 0.1 s (six times the 1/60 s it replaces), so the first update after
// time spent elsewhere does not jump.
double TakeSeconds(std::atomic<uint32_t>& pending) {
  return std::min(pending.exchange(0, std::memory_order_relaxed), 100u) / 1000.0;
}

}  // namespace

void Fm4MainLoopStep(PPCRegister& r30) {
  g_ui_pending_ms.fetch_add(r30.u32, std::memory_order_relaxed);
  g_showroom_pending_ms.fetch_add(r30.u32, std::memory_order_relaxed);
}

void Fm4UiSceneStep(PPCRegister& f31) {
  const double seconds = TakeSeconds(g_ui_pending_ms);
  if (AboveConsoleRate()) f31.f64 = seconds;
}

void Fm4ShowroomCameraStep(PPCRegister& f1) {
  const double seconds = TakeSeconds(g_showroom_pending_ms);
  if (AboveConsoleRate()) f1.f64 = seconds;
}
