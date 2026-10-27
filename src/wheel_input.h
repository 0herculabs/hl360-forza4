#pragma once
#include <memory>
#include "wheel.h"
namespace rex::system { class IInputSystem; }
namespace fm4::wheel {
std::unique_ptr<rex::system::IInputSystem> CreateInputSystem(bool tool_mode);
Settings ReadSettings();  // the wheel_* cvars
bool ForceAvailable(uint32_t user);
void SetFocus(bool focused);
void SubmitEffect(ForceEffect effect);
void OperateEffect(int id, bool start);
void ResetEffects();
void SetDriving(bool driving);
}
