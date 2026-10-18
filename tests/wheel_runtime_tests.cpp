#include "../src/wheel_input.h"
#include <SDL3/SDL.h>
#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

REXCVAR_DECLARE(bool,wheel_enabled);
REXCVAR_DECLARE(bool,wheel_ffb);
REXCVAR_DECLARE(std::string,wheel_device);
REXCVAR_DECLARE(int32_t,wheel_calibration);
REXCVAR_DECLARE(int32_t,wheel_button_a);
REXCVAR_DECLARE(int32_t,wheel_button_left);
REXCVAR_DECLARE(int32_t,wheel_throttle_axis);
REXCVAR_DECLARE(int32_t,wheel_brake_axis);
REXCVAR_DECLARE(int32_t,wheel_button_shift_up);
REXCVAR_DECLARE(int32_t,wheel_button_shift_down);
REXCVAR_DECLARE(bool,wheel_clutch_enabled);
REXCVAR_DECLARE(int32_t,wheel_clutch_axis);
REXCVAR_DECLARE(int32_t,wheel_clutch_rest);
REXCVAR_DECLARE(int32_t,wheel_clutch_full);
REXCVAR_DECLARE(int32_t,wheel_clutch_calibration);

void Check(bool v,const char* why) {if(!v) throw std::runtime_error(why);}
int main() {
  try {
    SDL_ResetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS);
    Check(SDL_Init(SDL_INIT_JOYSTICK),SDL_GetError());
    SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_WHEEL;desc.naxes=8;desc.nbuttons=4;desc.nhats=1;desc.name="FM4 runtime test wheel";
    const auto id=SDL_AttachVirtualJoystick(&desc);Check(id!=0,SDL_GetError());
    auto* joystick=SDL_OpenJoystick(id);Check(joystick!=nullptr,SDL_GetError());
    std::string key;
    {
      fm4::wheel::DeviceMonitor monitor;
      for(int i=0;i<250 && key.empty();++i) {
        for(const auto& d:monitor.Read().devices) if(d.name==desc.name) key=d.key;
        SDL_Delay(10);
      }
    }
    Check(!key.empty(),"Virtual test device was not discovered");
    Check(SDL_GetHintBoolean(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,false),
        "Raw wheel polling depends on an SDL window in the same module");
    REXCVAR_SET(wheel_enabled,true);REXCVAR_SET(wheel_ffb,false);REXCVAR_SET(wheel_device,key);
    REXCVAR_SET(wheel_calibration,127);REXCVAR_SET(wheel_button_a,3);
    REXCVAR_SET(wheel_calibration,0);
    auto uncalibrated=fm4::wheel::CreateInputSystem(false);
    auto* menu_input=static_cast<rex::input::InputSystem*>(uncalibrated.get());
    SDL_SetJoystickVirtualButton(joystick,3,true);
    SDL_SetJoystickVirtualAxis(joystick,1,32767);
    rex::input::X_INPUT_STATE menu_state{};
    bool menu_button=false;
    for(int i=0;i<250;++i) {
      SDL_PumpEvents();SDL_UpdateJoysticks();
      if(menu_input->GetState(0,&menu_state)==0 && (uint16_t(menu_state.gamepad.buttons)&0x1000)) {menu_button=true;break;}
      SDL_Delay(10);
    }
    Check(menu_button,"Uncalibrated wheel A button did not reach guest menus");
    Check(menu_state.gamepad.thumb_lx==0 && menu_state.gamepad.left_trigger==0 && menu_state.gamepad.right_trigger==0,
        "Uncalibrated axes must remain neutral");
    menu_input->Shutdown();uncalibrated.reset();
    REXCVAR_SET(wheel_calibration,127);
    REXCVAR_SET(wheel_throttle_axis,2);REXCVAR_SET(wheel_brake_axis,5);
    REXCVAR_SET(wheel_button_left,2);
    REXCVAR_SET(wheel_button_shift_up,0);REXCVAR_SET(wheel_button_shift_down,1);
    REXCVAR_SET(wheel_clutch_enabled,true);REXCVAR_SET(wheel_clutch_axis,6);
    REXCVAR_SET(wheel_clutch_rest,32767);REXCVAR_SET(wheel_clutch_full,-32768);
    REXCVAR_SET(wheel_clutch_calibration,3);
    auto generic=fm4::wheel::CreateInputSystem(false);
    auto* input=static_cast<rex::input::InputSystem*>(generic.get());
    SDL_SetJoystickVirtualAxis(joystick,0,16384);
    SDL_SetJoystickVirtualAxis(joystick,2,32767);
    SDL_SetJoystickVirtualAxis(joystick,5,-32768);
    SDL_SetJoystickVirtualButton(joystick,2,true);
    SDL_SetJoystickVirtualAxis(joystick,6,32767);
    SDL_SetJoystickVirtualButton(joystick,3,true);
    SDL_SetJoystickVirtualHat(joystick,0,SDL_HAT_RIGHTUP);
    rex::input::X_INPUT_STATE state{};
    bool matched=false;
    for(int i=0;i<250;++i) {
      SDL_PumpEvents();SDL_UpdateJoysticks();
      if(input->GetState(0,&state)==0 && state.gamepad.right_trigger==255 &&
          int16_t(state.gamepad.thumb_lx)>=16383 && (uint16_t(state.gamepad.buttons)&0x100D)==0x100D) {matched=true;break;}
      SDL_Delay(10);
    }
    Check(matched,"Wheel axes/buttons did not reach the guest InputSystem");
    Check(state.gamepad.left_trigger==0,"Released brake became pressed");
    SDL_SetJoystickVirtualAxis(joystick,2,-32768);
    SDL_SetJoystickVirtualAxis(joystick,5,32767);
    bool brake_matched=false;
    for(int i=0;i<250;++i) {
      SDL_PumpEvents();SDL_UpdateJoysticks();
      if(input->GetState(0,&state)==0 && state.gamepad.left_trigger==255 && state.gamepad.right_trigger==0) {
        brake_matched=true;break;
      }
      SDL_Delay(10);
    }
    Check(brake_matched,"Brake axis 5 / throttle axis 2 were not independent");
    SDL_SetJoystickVirtualButton(joystick,0,true);
    SDL_SetJoystickVirtualButton(joystick,1,true);
    SDL_SetJoystickVirtualAxis(joystick,6,-32768);
    bool shifted=false;
    for(int i=0;i<250;++i) {
      SDL_PumpEvents();SDL_UpdateJoysticks();
      if(input->GetState(0,&state)==0 && (uint16_t(state.gamepad.buttons)&0x2300)==0x2300) {shifted=true;break;}
      SDL_Delay(10);
    }
    Check(shifted,"Paddles RB/LB and clutch B did not reach guest input");
    SDL_SetJoystickVirtualButton(joystick,0,false);SDL_SetJoystickVirtualButton(joystick,1,false);
    SDL_SetJoystickVirtualAxis(joystick,6,32767);
    bool released=false;
    for(int i=0;i<250;++i) {
      SDL_PumpEvents();SDL_UpdateJoysticks();
      if(input->GetState(0,&state)==0 && (uint16_t(state.gamepad.buttons)&0x2300)==0) {released=true;break;}
      SDL_Delay(10);
    }
    Check(released,"Clutch or paddle buttons stuck after release");
    rex::input::X_INPUT_CAPABILITIES caps{};
    Check(input->GetCapabilities(0,1,&caps)==0 && caps.sub_type==2 && uint16_t(caps.flags)==0,
        "Wheel subtype or disabled FFB capability incorrect");
    SDL_CloseJoystick(joystick);SDL_DetachVirtualJoystick(id);
    bool disconnected=false;
    for(int i=0;i<250;++i) {
      SDL_PumpEvents();SDL_UpdateJoysticks();
      if(input->GetCapabilities(0,1,&caps)!=0 || caps.sub_type!=2) {disconnected=true;break;}
      SDL_Delay(10);
    }
    Check(disconnected,"Disconnected wheel still presented as connected");
    input->Shutdown();generic.reset();SDL_Quit();
    std::puts("Runtime wheel assignment, guest axes/triggers/buttons, capabilities and disconnect passed.");return 0;
  }catch(const std::exception& e){std::fprintf(stderr,"Runtime wheel test failed: %s\n",e.what());return 1;}
}
