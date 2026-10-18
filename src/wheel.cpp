#include "wheel.h"
#include "wheel_effects.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <cmath>
#include <thread>

namespace fm4::wheel {
namespace {
std::string Key(SDL_JoystickID id) {
  char guid[33]{};
  SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof(guid));
  // Path distinguishes multiple identical USB devices. Never persist instance IDs.
  const char* path = SDL_GetJoystickPathForID(id);
  return std::string(guid) + "|" + (path ? path : "");
}
}
struct DeviceMonitor::Impl {
  mutable std::mutex mutex;
  std::string selected;
  Snapshot snapshot;
  std::atomic<bool> stop{false};
  std::atomic<bool> active{false}, test{false};
  bool force_enabled = false, force_inverted = false, driving = false;
  int force_gain = 10;
  std::array<ForceEffect,5> forces{};
  std::array<bool,5> playing{};
  std::chrono::steady_clock::time_point force_time{};
  bool caller_thread;
  bool initialized = false, init_attempted = false;
  SDL_Joystick* joystick = nullptr;
  SDL_Haptic* haptic = nullptr;
  SDL_HapticEffectID effect = -1;
  std::array<SDL_HapticEffectID,5> force_ids{{-1,-1,-1,-1,-1}};
  std::chrono::steady_clock::time_point force_refresh{}, haptic_attempt{}, effect_until{}, discover{}, next_test{};
  bool haptics_initialized = false;
  SDL_JoystickID opened = 0;
  std::string open_error, last_requested;
  Snapshot next;
  std::thread thread;
  explicit Impl(bool caller) : caller_thread(caller) {
    if (!caller_thread) thread = std::thread([this] {
      while (!stop) { Tick(); std::this_thread::sleep_for(std::chrono::milliseconds(8)); }
      Shutdown();
    });
  }
  ~Impl() {
    stop = true;
    if (thread.joinable()) thread.join();
    if (caller_thread) Shutdown();
  }
  void stop_effect(bool close = true) {
    if (!haptic) return;
    const bool have_effect = effect >= 0 || std::any_of(force_ids.begin(), force_ids.end(), [](auto id) { return id >= 0; });
    if (have_effect) SDL_StopHapticEffects(haptic);
    if (effect >= 0) SDL_DestroyHapticEffect(haptic, effect);
    effect = -1;
    for (auto& id : force_ids) { if (id >= 0) SDL_DestroyHapticEffect(haptic, id); id = -1; }
    if (close) { SDL_CloseHaptic(haptic); haptic = nullptr; }
  }
  void Shutdown() {
    stop_effect();
    if (joystick) { SDL_CloseJoystick(joystick); joystick = nullptr; }
    if (haptics_initialized) { SDL_QuitSubSystem(SDL_INIT_HAPTIC); haptics_initialized = false; }
    if (initialized) { SDL_QuitSubSystem(SDL_INIT_JOYSTICK); initialized = false; }
  }
  void Tick() {
    if (!init_attempted) {
      init_attempted = true;
      SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
      initialized = SDL_InitSubSystem(SDL_INIT_JOYSTICK);
      if (!initialized) { std::lock_guard lock(mutex); snapshot.status = SDL_GetError(); }
    }
    if (!initialized) return;
      std::string requested;
      bool enabled, inverted, in_race;
      int gain;
      std::array<ForceEffect,5> effects;
      std::array<bool,5> starts;
      std::chrono::steady_clock::time_point heartbeat;
      { std::lock_guard lock(mutex); requested = selected; enabled=force_enabled; inverted=force_inverted;
        gain=force_gain;in_race=driving;effects=forces;starts=playing;heartbeat=force_time; }
      const auto now = std::chrono::steady_clock::now();
      if (now - discover >= std::chrono::seconds(1) || requested != last_requested) {
        discover = now;
        last_requested = requested;
        next.devices.clear();
        int count = 0;
        SDL_JoystickID* ids = SDL_GetJoysticks(&count);
        SDL_JoystickID chosen = 0;
        for (int i = 0; ids && i < count; ++i) {
          const char* name = SDL_GetJoystickNameForID(ids[i]);
          const bool is_wheel = SDL_GetJoystickTypeForID(ids[i]) == SDL_JOYSTICK_TYPE_WHEEL;
          auto key = Key(ids[i]);
          if(std::any_of(next.devices.begin(),next.devices.end(),[&](const Device& d){return d.key==key;})) continue;
          next.devices.push_back({key, name ? name : "Joystick", is_wheel});
          if ((!requested.empty() && requested == key) || (requested.empty() && is_wheel && !chosen)) chosen = ids[i];
        }
        SDL_free(ids);
        if (chosen != opened) {
          stop_effect();
          next.force_supported = false;
          haptic_attempt = {};
          if (joystick) SDL_CloseJoystick(joystick);
          joystick = chosen ? SDL_OpenJoystick(chosen) : nullptr;
          open_error = chosen && !joystick ? SDL_GetError() : "";
          opened = joystick ? chosen : 0;
          ++next.generation;
        }
      }
      next.connected = joystick && SDL_JoystickConnected(joystick);
      if (!next.connected) {stop_effect(); next.force_supported=false;}
      else if (!enabled && (!active || now >= effect_until)) stop_effect(false);
      next.axes.clear(); next.buttons.clear(); next.hat = 0;
      if (next.connected) {
        SDL_UpdateJoysticks();
        next.key = Key(opened);
        const char* name = SDL_GetJoystickName(joystick);
        next.name = name ? name : "Joystick";
        for (int i=0; i<std::min(64,SDL_GetNumJoystickAxes(joystick)); ++i) next.axes.push_back(SDL_GetJoystickAxis(joystick,i));
        for (int i=0; i<std::min(128,SDL_GetNumJoystickButtons(joystick)); ++i) next.buttons.push_back(SDL_GetJoystickButton(joystick,i));
        if (SDL_GetNumJoystickHats(joystick)>0) next.hat = SDL_GetJoystickHat(joystick,0);
        next.status = "Connected";
      } else {
        next.key.clear(); next.name.clear();
        next.status = open_error.empty() ? "No selected wheel connected" : "Cannot open wheel: " + open_error;
      }
      if(enabled && next.connected && !haptic && now-haptic_attempt>=std::chrono::seconds(1)) {
        haptic_attempt=now;
        if(!haptics_initialized) haptics_initialized=SDL_InitSubSystem(SDL_INIT_HAPTIC);
        if(haptics_initialized) haptic=SDL_OpenHapticFromJoystick(joystick);
        next.force_supported=haptic && (SDL_GetHapticFeatures(haptic)&SDL_HAPTIC_CONSTANT) && SDL_SetHapticGain(haptic,std::clamp(gain,0,100));
        if(!next.force_supported) {next.haptic_status=SDL_GetError();stop_effect();}
        else next.haptic_status="Native wheel FFB ready (experimental)";
      }
      if(enabled && haptic) {
        const bool allowed=ForceOutputAllowed(active,in_race,heartbeat,now);
        if(!allowed) stop_effect(false);
        else if(now-force_refresh>=std::chrono::milliseconds(32)) {
          force_refresh=now;
          for(size_t i=0;i<effects.size();++i) {
            const auto& source=effects[i];
            if(!starts[i] || !source.id) {
              if(force_ids[i]>=0) SDL_StopHapticEffect(haptic,force_ids[i]);
              continue;
            }
            SDL_HapticEffect output{};
            if(!BuildHapticEffect(source,inverted,output) || !(SDL_GetHapticFeatures(haptic)&output.type)) continue;
            if(force_ids[i]<0) force_ids[i]=SDL_CreateHapticEffect(haptic,&output);
            else if(!SDL_UpdateHapticEffect(haptic,force_ids[i],&output)) {
              next.haptic_status=SDL_GetError();stop_effect(false);break;
            }
            if(force_ids[i]<0 || !SDL_RunHapticEffect(haptic,force_ids[i],1)) {
              next.haptic_status=SDL_GetError();stop_effect(false);break;
            }
          }
        }
      }
      if (test.exchange(false) && now >= next_test) {
        next_test = now + std::chrono::milliseconds(750);
        stop_effect(false);
        if (!next.connected || !active) next.haptic_status = "Test cancelled: window inactive or device disconnected";
        else {
          if (!haptics_initialized) haptics_initialized = SDL_InitSubSystem(SDL_INIT_HAPTIC);
          if (haptics_initialized && !haptic) haptic = SDL_OpenHapticFromJoystick(joystick);
          if (!haptic) next.haptic_status = SDL_GetError();
          else if (!(SDL_GetHapticFeatures(haptic) & SDL_HAPTIC_CONSTANT)) {
            next.haptic_status = "Constant-force effects are not supported"; stop_effect();
          } else if (!SDL_SetHapticGain(haptic, 100)) {
            next.haptic_status = "Cannot set diagnostic gain: " + std::string(SDL_GetError()); stop_effect();
          } else {
            // Gain is device-wide and can survive a previous game session.
            // The effect itself is capped at 20%, so reset the global multiplier
            // to prevent a previous 10% gain reducing this pulse to 2%.
            SDL_HapticEffect pulse{};
            pulse.type = SDL_HAPTIC_CONSTANT;
            pulse.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
            pulse.constant.length = 150;
            pulse.constant.level = 6553;
            pulse.constant.attack_length = 50; pulse.constant.fade_length = 50;
            effect = SDL_CreateHapticEffect(haptic,&pulse);
            if (effect<0 || !SDL_RunHapticEffect(haptic,effect,1)) {
              next.haptic_status = SDL_GetError(); stop_effect();
            } else {
              next.haptic_status = "Driver accepted a 20% pulse (150 ms); physical output is not verified";
              effect_until = now + std::chrono::milliseconds(200);
            }
          }
        }
      }
      next.test_busy = now < next_test;
      { std::lock_guard lock(mutex); snapshot = next; }
  }
};
DeviceMonitor::DeviceMonitor(bool caller_thread) : impl_(std::make_unique<Impl>(caller_thread)) {}
void DeviceMonitor::Pump() { if (impl_->caller_thread) impl_->Tick(); }
DeviceMonitor::~DeviceMonitor() = default;
void DeviceMonitor::Select(std::string key) { std::lock_guard lock(impl_->mutex); impl_->selected = std::move(key); }
Snapshot DeviceMonitor::Read() const { std::lock_guard lock(impl_->mutex); return impl_->snapshot; }
void DeviceMonitor::SetActive(bool active) { impl_->active = active; }
void DeviceMonitor::TestHaptics() { impl_->test = true; }
void DeviceMonitor::ConfigureForce(bool enabled,int gain,bool inverted) {
  std::lock_guard lock(impl_->mutex);impl_->force_enabled=enabled;impl_->force_gain=std::clamp(gain,0,100);impl_->force_inverted=inverted;
}
void DeviceMonitor::UpdateForce(ForceEffect e) {
  if(e.id<1 || e.id>5) return;
  std::lock_guard lock(impl_->mutex);impl_->forces[e.id-1]=e;impl_->playing[e.id-1]=true;
  impl_->force_time=std::chrono::steady_clock::now();
}
void DeviceMonitor::OperateForce(int id,bool start) {
  if(id<1 || id>5) return;
  std::lock_guard lock(impl_->mutex);impl_->playing[id-1]=start;
}
void DeviceMonitor::ClearForce() {
  std::lock_guard lock(impl_->mutex);impl_->playing.fill(false);impl_->driving=false;
}
void DeviceMonitor::SetDriving(bool driving) {
  std::lock_guard lock(impl_->mutex);impl_->driving=driving;impl_->force_time=std::chrono::steady_clock::now();
}
}


