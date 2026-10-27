// fm4-wheel-probe: lists every game controller with its match text and raw
// axes, buttons and hats, without requesting any force. Use the match text
// and numbers in the wheel_* settings (or let the launcher's Wheel tab bind).
#include "../src/wheel.h"
#include <SDL3/SDL.h>
#include <cstdio>

int main() {
  fm4::wheel::DeviceMonitor monitor;
  fm4::wheel::Snapshot state;
  for (int i = 0; i < 150; ++i) {
    SDL_Delay(10);
    state = monitor.Read();
    if (!state.devices.empty() && i > 120) break;
  }
  if (state.devices.empty()) std::printf("No game controllers found.\n");
  for (const auto& d : state.devices) {
    std::printf("\n%s%s\nmatch: %s\nvid:pid %04x:%04x  path: %s\n", d.name.c_str(), d.wheel ? " [wheel]" : "",
                fm4::wheel::DeviceMatch(d, state.devices).c_str(), d.vendor, d.product, d.path.c_str());
    std::printf("axes=%zu buttons=%zu hats=%zu raw:", d.axes.size(), d.buttons.size(), d.hats.size());
    for (int value : d.axes) std::printf(" %d", value);
    std::printf("\npressed:");
    for (size_t b = 0; b < d.buttons.size(); ++b) if (d.buttons[b]) std::printf(" %zu", b);
    for (size_t h = 0; h < d.hats.size(); ++h) if (d.hats[h]) std::printf(" hat%zu=%d", h, d.hats[h]);
    std::printf("\n");
  }
  return 0;
}
