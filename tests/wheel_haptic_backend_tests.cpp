#include <SDL3/SDL.h>
#include <cstdio>

// No device is opened and no motor command is sent. In particular, this must
// fail when linked to an SDK SDL built with SDL_HAPTIC=OFF, even without a wheel.
int main() {
  if (!SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC)) {
    std::fprintf(stderr, "Wheel release requires an SDL haptic backend: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  int count = 0;
  SDL_HapticID* devices = SDL_GetHaptics(&count);
  if (!devices) {
    std::fprintf(stderr, "Haptic enumeration failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  SDL_free(devices);
  std::printf("SDL haptic initialization/enumeration passed (%d devices; no forces sent).\n", count);
  SDL_Quit();
  return 0;
}
