#pragma once
#include "wheel.h"
#include <SDL3/SDL.h>
#include <chrono>

namespace fm4::wheel {
inline bool ForceOutputAllowed(bool focused,bool driving,std::chrono::steady_clock::time_point heartbeat,
    std::chrono::steady_clock::time_point now) {
  return focused && driving && now>=heartbeat && now-heartbeat<std::chrono::milliseconds(200);
}
// The game's native effect as an SDL haptic effect, scaled by the tuning.
inline bool BuildHapticEffect(const ForceEffect& source,const ForceTuning& tuning,SDL_HapticEffect& output) {
  output={};
  if(source.id<1 || source.id>5) return false;
  switch(source.type) {
    case kConstant: output.type=SDL_HAPTIC_CONSTANT;break;
    case kSpring: output.type=SDL_HAPTIC_SPRING;break;
    case kDamper: output.type=SDL_HAPTIC_DAMPER;break;
    case kInertia: output.type=SDL_HAPTIC_INERTIA;break;
    case kFriction: output.type=SDL_HAPTIC_FRICTION;break;
    default:return false;
  }
  if(source.type==kConstant) {
    output.constant.direction.type=SDL_HAPTIC_STEERING_AXIS;
    output.constant.length=SDL_HAPTIC_INFINITY;
    output.constant.level=EffectConstantLevel(source,tuning);
  } else {
    const Condition c=EffectCondition(source,tuning);
    output.condition.direction.type=SDL_HAPTIC_STEERING_AXIS;
    output.condition.length=SDL_HAPTIC_INFINITY;
    output.condition.right_coeff[0]=c.right_coeff;
    output.condition.left_coeff[0]=c.left_coeff;
    output.condition.right_sat[0]=c.right_sat;
    output.condition.left_sat[0]=c.left_sat;
    output.condition.center[0]=c.center;
    output.condition.deadband[0]=c.deadband;
  }
  return true;
}
}
