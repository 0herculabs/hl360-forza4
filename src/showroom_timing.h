#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace fm4 {
// The game's camera queue expects seconds, irrespective of rendering rate.
class ShowroomClock {
 public:
  using Clock = std::chrono::steady_clock;
  double Step(uint32_t camera, Clock::time_point now) {
    double seconds = 1.0 / 60.0;
    if (initialized_ && camera == camera_) {
      seconds = std::clamp(std::chrono::duration<double>(now - last_).count(), 0.0, 0.1);
    }
    initialized_ = true;
    camera_ = camera;
    last_ = now;
    return seconds;
  }

 private:
  bool initialized_ = false;
  uint32_t camera_ = 0;
  Clock::time_point last_{};
};
}  // namespace fm4
