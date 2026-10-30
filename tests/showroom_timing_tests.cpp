#include "showroom_timing.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

int main() {
  using Clock = fm4::ShowroomClock::Clock;
  for (int fps : {30, 60, 120, 240}) {
    fm4::ShowroomClock camera;
    const auto start = Clock::time_point{};
    camera.Step(1, start);
    double animation_seconds = 0;
    for (int i = 1; i <= fps * 10; ++i) {
      animation_seconds += camera.Step(1, start + std::chrono::duration_cast<Clock::duration>(
          std::chrono::duration<double>(double(i) / fps)));
    }
    if (std::abs(animation_seconds - 10.0) > 0.00001)
      throw std::runtime_error("Camera duration depends on frame rate");
    if (std::abs(camera.Step(2, start + std::chrono::seconds(30)) - 1.0 / 60.0) > 0.00001)
      throw std::runtime_error("A new camera inherited time from the previous scene");
    if (camera.Step(2, start + std::chrono::seconds(60)) > 0.100001)
      throw std::runtime_error("Loading pause caused a camera jump");
  }
  std::cout << "Showroom timing: 10 seconds at 30/60/120/240 FPS, scene changes and pauses: OK\n";
}
