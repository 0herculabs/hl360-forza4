#include "frame_stats.h"
#include <rex/cvar.h>
#include <rex/ppc/context.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

extern "C" void sub_8237F598(PPCContext&, uint8_t*);
static unsigned calls = 0;
extern "C" void __imp__sub_8237F598(PPCContext&, uint8_t*) { ++calls; }

int main() {
  try {
    PPCContext context{};
    rex::cvar::SetFlagByName("fm4_frame_limit", "30");
    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < 40; ++i) sub_8237F598(context, nullptr);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const auto stats = GetFm4FrameStats();
    if (calls != 40 || seconds < 1.2 || seconds > 3.0 ||
        stats.fps < 25 || stats.fps > 35 || stats.frame_time_ms < 28 || stats.frame_time_ms > 40)
      throw std::runtime_error("Frame cap or guest frame statistics failed");
    // Changing the cap to game-default must not leave a pending sleep behind.
    rex::cvar::SetFlagByName("fm4_frame_limit", "0");
    const auto uncapped = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i) sub_8237F598(context, nullptr);
    if (calls != 50 || std::chrono::steady_clock::now() - uncapped > std::chrono::milliseconds(250))
      throw std::runtime_error("Disabling the cap left delayed or dropped calls");
    std::cout << "Frame measurements and optional host cap passed (" << stats.fps << " FPS).\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
