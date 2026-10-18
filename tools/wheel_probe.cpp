#include "../src/wheel.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <thread>

// Read-only hardware inventory. This executable never requests motor effects.
int main() {
  fm4::wheel::DeviceMonitor monitor;
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  auto state = monitor.Read();
  std::printf("Raw joystick devices: %zu\n",state.devices.size());
  const auto devices=state.devices;
  for (const auto& d : devices) {
    std::printf("\n%s%s\nkey: %s\n",d.name.c_str(),d.wheel ? " [wheel]" : "",d.key.c_str());
    monitor.Select(d.key);
    for(int i=0;i<150;++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      state=monitor.Read(); if(state.connected && state.key==d.key) break;
    }
    if(state.connected && state.key==d.key) {
      std::printf("axes=%zu buttons=%zu raw:",state.axes.size(),state.buttons.size());
      for(int value:state.axes) std::printf(" %d",value);
      std::printf("\n");
    }
  }
  if(state.devices.empty()) std::printf("Connect a wheel in PC mode and run again.\n");
  return 0;
}
