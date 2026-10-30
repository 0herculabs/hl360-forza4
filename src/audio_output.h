#pragma once
#include <rex/audio/sdl/sdl_audio_driver.h>
#include <rex/audio/sdl/sdl_audio_system.h>
#include <rex/cvar.h>

REXCVAR_DEFINE_STRING(audio_output_mode, "auto", "FM4/Audio", "Audio output: auto, stereo, surround")
    .allowed({"auto", "stereo", "surround"})
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace fm4 {
class OutputDriver : public rex::audio::sdl::SDLAudioDriver {
 public:
  using SDLAudioDriver::SDLAudioDriver;
  bool InitializeOutput() {
    if (!Initialize()) return false;
    const auto mode = REXCVAR_GET(audio_output_mode);
    if (mode == "auto") return true;
    // The runtime callback folds to stereo or supplies the game's six PCM
    // channels. SDL converts to the endpoint layout configured in Windows.
    const int channels = mode == "stereo" ? 2 : 6;
    SDL_AudioSpec spec{SDL_AUDIO_F32LE, channels, 48000};
    if (!SDL_LockAudioStream(sdl_stream_)) return false;
    const bool configured = SDL_SetAudioStreamFormat(sdl_stream_, &spec, nullptr);
    if (configured) sdl_device_channels_ = static_cast<uint8_t>(channels);
    SDL_UnlockAudioStream(sdl_stream_);
    return configured;
  }
};
class OutputSystem : public rex::audio::sdl::SDLAudioSystem {
 public:
  using SDLAudioSystem::SDLAudioSystem;
  rex::X_STATUS CreateDriver(size_t, rex::thread::Semaphore* semaphore,
      rex::audio::AudioDriver** out) override {
    auto driver = std::make_unique<OutputDriver>(memory_, semaphore);
    if (!driver->InitializeOutput()) {
      driver->Shutdown();
      return static_cast<rex::X_STATUS>(0xC0000001L);
    }
    *out = driver.release();
    return 0;
  }
};
}
