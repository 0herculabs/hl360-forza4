#include "../src/wheel.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <stdexcept>

static void Check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
struct Owner { SDL_ThreadID thread; bool wrong_thread = false; };
static void SDLCALL Updated(void* data) {
  auto& owner = *static_cast<Owner*>(data);
  if (SDL_GetCurrentThreadID() != owner.thread) owner.wrong_thread = true;
}
int main() {
  try {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    Check(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK), SDL_GetError());
    auto* window = SDL_CreateWindow("Wheel UI test", 320, 240, SDL_WINDOW_HIDDEN);
    Check(window != nullptr, SDL_GetError());
    Owner owner{SDL_GetCurrentThreadID()};
    SDL_VirtualJoystickDesc desc{}; SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_WHEEL; desc.naxes = 8; desc.nbuttons = 128;
    desc.name = "FM4 UI test wheel"; desc.userdata = &owner; desc.Update = Updated;
    const auto id = SDL_AttachVirtualJoystick(&desc);
    Check(id != 0, SDL_GetError());
    auto* joystick = SDL_OpenJoystick(id); Check(joystick != nullptr, SDL_GetError());
    char guid[33]{}; SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof(guid));
    const auto* path = SDL_GetJoystickPathForID(id);
    const std::string key = std::string(guid) + "|" + (path ? path : "");
    for (int session = 0; session < 3; ++session) {
      fm4::wheel::DeviceMonitor monitor(true);
      monitor.Select(key); monitor.SetActive(true); monitor.Pump();
      Check(monitor.Read().connected, "UI monitor failed to open selected virtual wheel");
      // Virtual joystick has no haptic backend: repeated diagnostics exercise
      // errors and cooldown without ever applying physical force.
      monitor.TestHaptics(); monitor.Pump();
      auto initial = monitor.Read();
      Check(initial.test_busy && !initial.haptic_status.empty(), "Diagnostic/cooldown did not start");
      for (int frame = 0; frame < 100; ++frame) {
        SDL_Event event; while (SDL_PollEvent(&event)) {}
        SDL_SetJoystickVirtualAxis(joystick, 0, Sint16(frame * 123));
        monitor.TestHaptics(); monitor.Pump();
        const auto state = monitor.Read();
        Check(state.axes.size() == 8 && state.buttons.size() == 128,
              "Axis display lost data during repeated diagnostics");
        Check(state.axes[0] == frame * 123, "Axes stopped updating during diagnostic cooldown");
        Check(state.haptic_status == initial.haptic_status, "Repeated request replaced the diagnostic during cooldown");
      }
      Check(!owner.wrong_thread, "UI monitor used SDL on a different thread");
      monitor.SetActive(false); monitor.Pump();
    } // Every monitor is destroyed before SDL/video shutdown.
    SDL_CloseJoystick(joystick); SDL_DetachVirtualJoystick(id);
    SDL_DestroyWindow(window); SDL_Quit();
    std::puts("UI event pumping, repeated diagnostic requests, axes and same-thread teardown passed.");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "Wheel UI test failed: %s\n", error.what()); return 1;
  }
}
