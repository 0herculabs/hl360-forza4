#pragma once
#include "wheel.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <chrono>

namespace fm4::wheel {
inline bool ForceOutputAllowed(bool focused,bool driving,std::chrono::steady_clock::time_point heartbeat,
    std::chrono::steady_clock::time_point now) {
  return focused && driving && now>=heartbeat && now-heartbeat<std::chrono::milliseconds(200);
}
inline bool BuildHapticEffect(const ForceEffect& source,bool inverted,SDL_HapticEffect& output) {
  output={};
  if(source.id<1 || source.id>5) return false;
  switch(source.type) {
    case 1: output.type=SDL_HAPTIC_CONSTANT;break;
    case 8: output.type=SDL_HAPTIC_SPRING;break;
    case 9: output.type=SDL_HAPTIC_DAMPER;break;
    case 10: output.type=SDL_HAPTIC_INERTIA;break;
    case 11: output.type=SDL_HAPTIC_FRICTION;break;
    default:return false;
  }
  // FM4 quantizes signed forces/coefficients to +/-127, gain/saturation to 255.
  const float scale=std::clamp(source.gain,0,255)/255.0f;
  auto signed_value=[&](int v){return Sint16(std::lround(std::clamp(v,-127,127)/127.0f*32767*scale));};
  auto saturation=[&](int v){return Uint16(std::lround(std::clamp(v,0,255)/255.0f*65535*scale));};
  if(source.type==1) {
    output.constant.direction.type=SDL_HAPTIC_STEERING_AXIS;
    output.constant.length=100;
    output.constant.level=signed_value(source.magnitude)*(inverted?-1:1);
  } else {
    output.condition.direction.type=SDL_HAPTIC_STEERING_AXIS;
    output.condition.length=100;
    output.condition.right_coeff[0]=signed_value(source.positive_coefficient);
    output.condition.left_coeff[0]=signed_value(source.negative_coefficient);
    output.condition.right_sat[0]=saturation(source.positive_saturation);
    output.condition.left_sat[0]=saturation(source.negative_saturation);
    output.condition.center[0]=Sint16(std::clamp(source.offset,-127,127)*32767/127);
    output.condition.deadband[0]=Uint16(std::clamp(source.deadband,0,255)*32767/255);
  }
  return true;
}
}
