// Frame pacing statistics (opt-in: --frame_stats=true), as in the FM2
// recompilation.
//
// Every frame the title ends in 0x8237F598, the XDK present path that calls
// VdSwap (at 0x8237F920). Wrapping it gives the real frame interval as the game
// sees it. Every 5 seconds a summary goes to the log: fps, average / 99th
// percentile / worst frame time, and how many frames missed 1/60 s and 1/30 s.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include "frame_stats.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

REXCVAR_DEFINE_BOOL(frame_stats, false, "FM4/Debug", "Log frame pacing statistics every 5 seconds");
REXCVAR_DEFINE_STRING(frame_samples, "", "FM4/Debug", "Optional CSV path for guest frame intervals and present-call wall time");
REXCVAR_DEFINE_INT32(fm4_frame_limit, 0, "FM4/Graphics",
                    "Experimental host frame cap: 0 = game default, 30 or 60; guest clock remains unchanged");

extern "C" void __imp__sub_8237F598(PPCContext& __restrict ctx, uint8_t* base);

namespace {

using Clock = std::chrono::steady_clock;
std::mutex display_mutex;
rex::ui::FrameStats display_stats;

struct Stats {
  Clock::time_point window_start{};
  Clock::time_point last{};
  std::vector<float> frame_ms;
  FILE* samples = nullptr;
  bool samples_attempted = false;
  Clock::time_point sample_start{}, last_flush{};
  uint64_t frame = 0;
  Clock::time_point display_start{}, next_frame{};
  uint64_t display_frames = 0, total_frames = 0;
  int previous_limit = 0;
};

}  // namespace

rex::ui::FrameStats GetFm4FrameStats() {
  std::lock_guard lock(display_mutex);
  return display_stats;
}

// Overrides the weak alias the generated code calls.
extern "C" void sub_8237F598(PPCContext& __restrict ctx, uint8_t* base) {
  static Stats stats;
  const int limit = REXCVAR_GET(fm4_frame_limit);
  if (limit == 30 || limit == 60) {
    const auto before_wait = Clock::now();
    const auto period = std::chrono::nanoseconds(1000000000 / limit);
    if (stats.previous_limit != limit || stats.next_frame.time_since_epoch().count() == 0 ||
        before_wait > stats.next_frame + period) stats.next_frame = before_wait;
    std::this_thread::sleep_until(stats.next_frame);
    stats.next_frame += period;
  } else {
    stats.next_frame = {};
  }
  stats.previous_limit = limit;
  const auto now = Clock::now();
  if (!stats.display_start.time_since_epoch().count()) stats.display_start = now;
  ++stats.display_frames;
  ++stats.total_frames;
  const double display_seconds = std::chrono::duration<double>(now - stats.display_start).count();
  if (display_seconds >= 0.5) {
    std::lock_guard lock(display_mutex);
    display_stats = {display_seconds * 1000.0 / stats.display_frames,
                     stats.display_frames / display_seconds, stats.total_frames};
    stats.display_frames = 0;
    stats.display_start = now;
  }
  const double unix_s = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
  if (!stats.samples_attempted) {
    stats.samples_attempted = true;
    const auto& path = REXCVAR_GET(frame_samples);
    if (!path.empty()) {
      stats.samples = std::fopen(path.c_str(), "w");
      if (stats.samples) {
        std::fputs("frame,elapsed_s,unix_s,interval_ms,present_call_ms\n", stats.samples);
        stats.sample_start = stats.last_flush = now;
      } else {
        REXLOG_ERROR("Could not open frame_samples CSV: {}", path);
      }
    }
  }
  const double interval_ms = stats.last.time_since_epoch().count()
      ? std::chrono::duration<double, std::milli>(now - stats.last).count() : 0.0;
  if (stats.last.time_since_epoch().count()) {
    if (REXCVAR_GET(frame_stats))
      stats.frame_ms.push_back(std::chrono::duration<float, std::milli>(now - stats.last).count());
  } else {
    stats.window_start = now;
  }
  stats.last = now;

  const float window_s = std::chrono::duration<float>(now - stats.window_start).count();
  if (window_s >= 5.0f && !stats.frame_ms.empty()) {
    auto sorted = stats.frame_ms;
    std::sort(sorted.begin(), sorted.end());
    float sum = 0;
    int over60 = 0, over30 = 0;
    for (float ms : sorted) {
      sum += ms;
      over60 += ms > 17.5f;
      over30 += ms > 34.0f;
    }
    const size_t n = sorted.size();
    REXLOG_WARN(
        "frame stats: {} frames in {:.1f}s = {:.1f} fps | avg {:.2f} ms, p99 {:.2f} ms, max {:.2f} ms | "
        ">1/60s: {} | >1/30s: {}",
        n, window_s, float(n) / window_s, sum / float(n), sorted[std::min(n - 1, n * 99 / 100)],
        sorted.back(), over60, over30);
    stats.frame_ms.clear();
    stats.window_start = now;
  }
  const auto call_start = Clock::now();
  __imp__sub_8237F598(ctx, base);
  if (stats.samples) {
    const auto completed = Clock::now();
    const double present_ms = std::chrono::duration<double, std::milli>(completed - call_start).count();
    std::fprintf(stats.samples, "%llu,%.6f,%.6f,%.6f,%.6f\n",
                 static_cast<unsigned long long>(++stats.frame),
                 std::chrono::duration<double>(now - stats.sample_start).count(), unix_s, interval_ms, present_ms);
    if (completed - stats.last_flush >= std::chrono::seconds(1)) {
      std::fflush(stats.samples);
      stats.last_flush = completed;
    }
  }
}
