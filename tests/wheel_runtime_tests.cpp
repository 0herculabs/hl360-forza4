#include "../src/wheel_input.h"
#include <SDL3/SDL.h>
#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <cstdio>
#include <functional>
#include <stdexcept>

REXCVAR_DECLARE(bool,wheel_enabled);
REXCVAR_DECLARE(std::string,wheel_device);
REXCVAR_DECLARE(std::string,wheel_throttle);
REXCVAR_DECLARE(std::string,wheel_brake);
REXCVAR_DECLARE(std::string,wheel_clutch);
REXCVAR_DECLARE(std::string,wheel_buttons);
REXCVAR_DECLARE(std::string,wheel_clutch_button);
REXCVAR_DECLARE(std::string,wheel_ffb_mode);
REXCVAR_DECLARE(bool,wheel_clutch_analog);
REXCVAR_DECLARE(int32_t,wheel_pedal_deadzone);

void Check(bool v,const char* why) {if(!v) throw std::runtime_error(why);}
SDL_Joystick* Attach(const char* name,SDL_JoystickType type,int axes,int buttons,int hats,SDL_JoystickID& id) {
  SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);
  desc.type=Uint16(type);desc.naxes=Uint16(axes);desc.nbuttons=Uint16(buttons);desc.nhats=Uint16(hats);desc.name=name;
  id=SDL_AttachVirtualJoystick(&desc);Check(id!=0,SDL_GetError());
  auto* j=SDL_OpenJoystick(id);Check(j!=nullptr,SDL_GetError());
  return j;
}
bool WaitFor(rex::input::InputSystem* input,rex::input::X_INPUT_STATE& state,const std::function<bool(const rex::input::X_INPUT_STATE&)>& ok) {
  for(int i=0;i<300;++i) {
    SDL_PumpEvents();SDL_UpdateJoysticks();
    if(input->GetState(0,&state)==0 && ok(state)) return true;
    SDL_Delay(10);
  }
  return false;
}
int main() {
  try {
    SDL_ResetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS);
    Check(SDL_Init(SDL_INIT_JOYSTICK),SDL_GetError());
    SDL_JoystickID wheel_id,pedal_id;
    // The wheel base and a separate USB pedal set, as with a direct-drive base.
    auto* wheel=Attach("FM4 runtime test wheel",SDL_JOYSTICK_TYPE_WHEEL,4,8,1,wheel_id);
    auto* pedals=Attach("FM4 test pedals",SDL_JOYSTICK_TYPE_UNKNOWN,3,0,0,pedal_id);
    SDL_SetJoystickVirtualAxis(pedals,0,-32768);SDL_SetJoystickVirtualAxis(pedals,1,32767);SDL_SetJoystickVirtualAxis(pedals,2,-32768);
    REXCVAR_SET(wheel_enabled,true);
    REXCVAR_SET(wheel_device,std::string("runtime test wheel"));
    REXCVAR_SET(wheel_throttle,std::string("FM4 test pedals|+axis0"));
    REXCVAR_SET(wheel_brake,std::string("FM4 test pedals|-axis1"));
    REXCVAR_SET(wheel_clutch,std::string("FM4 test pedals|+axis2"));
    REXCVAR_SET(wheel_buttons,std::string("UP=h0.up,DOWN=h0.down,LEFT=h0.left,RIGHT=h0.right,A=3,RB=0,LB=1,X=FM4 test pedals|+axis0"));
    REXCVAR_SET(wheel_clutch_button,std::string("B"));
    REXCVAR_SET(wheel_ffb_mode,std::string("off"));
    REXCVAR_SET(wheel_pedal_deadzone,0);
    auto system=fm4::wheel::CreateInputSystem(false);
    auto* input=static_cast<rex::input::InputSystem*>(system.get());
    rex::input::X_INPUT_STATE state{};
    Check(WaitFor(input,state,[](auto& s){return s.gamepad.left_trigger==0 && s.gamepad.right_trigger==0;}),
        "Wheel did not reach the guest, or pedals at rest read pressed");

    SDL_SetJoystickVirtualAxis(wheel,0,16384);
    SDL_SetJoystickVirtualAxis(pedals,0,32767);
    SDL_SetJoystickVirtualButton(wheel,3,true);
    SDL_SetJoystickVirtualHat(wheel,0,SDL_HAT_RIGHTUP);
    Check(WaitFor(input,state,[](auto& s){return s.gamepad.right_trigger==255 && int16_t(s.gamepad.thumb_lx)>=16383 &&
        (uint16_t(s.gamepad.buttons)&0x1009)==0x1009;}),"Steering, throttle on the pedal device, A or the hat did not reach the guest");
    Check(state.gamepad.left_trigger==0,"Released brake became pressed");
    Check((uint16_t(state.gamepad.buttons)&0x4000)==0x4000,"An axis on another device did not press its button (X)");

    SDL_SetJoystickVirtualAxis(pedals,0,-32768);
    SDL_SetJoystickVirtualAxis(pedals,1,-32768);
    Check(WaitFor(input,state,[](auto& s){return s.gamepad.left_trigger==255 && s.gamepad.right_trigger==0;}),
        "Inverted brake on the pedal device was not independent of throttle");

    SDL_SetJoystickVirtualButton(wheel,0,true);SDL_SetJoystickVirtualButton(wheel,1,true);
    SDL_SetJoystickVirtualAxis(pedals,2,32767);
    Check(WaitFor(input,state,[](auto& s){return (uint16_t(s.gamepad.buttons)&0x2300)==0x2300;}),
        "Paddles RB/LB and the clutch pedal's B did not reach guest input");
    Check(int16_t(state.gamepad.thumb_ry)==0,"Clutch sent as analog while off");
    SDL_SetJoystickVirtualButton(wheel,0,false);SDL_SetJoystickVirtualButton(wheel,1,false);
    SDL_SetJoystickVirtualAxis(pedals,2,-32768);
    Check(WaitFor(input,state,[](auto& s){return (uint16_t(s.gamepad.buttons)&0x2300)==0;}),"Clutch or paddle buttons stuck after release");

    rex::input::X_INPUT_CAPABILITIES caps{};
    Check(input->GetCapabilities(0,1,&caps)==0 && caps.sub_type==2 && uint16_t(caps.flags)==0,
        "Wheel subtype or disabled FFB capability incorrect");
    input->Shutdown();system.reset();

    REXCVAR_SET(wheel_clutch_analog,true);
    system=fm4::wheel::CreateInputSystem(false);
    input=static_cast<rex::input::InputSystem*>(system.get());
    SDL_SetJoystickVirtualAxis(pedals,2,32767);
    Check(WaitFor(input,state,[](auto& s){return int16_t(s.gamepad.thumb_ry)==32767;}),"Analog clutch not on the right stick's Y axis");

    SDL_CloseJoystick(pedals);SDL_DetachVirtualJoystick(pedal_id);
    SDL_CloseJoystick(wheel);SDL_DetachVirtualJoystick(wheel_id);
    bool disconnected=false;
    for(int i=0;i<300;++i) {
      SDL_PumpEvents();SDL_UpdateJoysticks();
      if(input->GetCapabilities(0,1,&caps)!=0 || caps.sub_type!=2) {disconnected=true;break;}
      SDL_Delay(10);
    }
    Check(disconnected,"Disconnected wheel still presented as connected");
    input->Shutdown();system.reset();SDL_Quit();
    std::puts("Runtime wheel on two devices: steering, pedals, buttons, hat, clutch, capabilities and disconnect passed.");return 0;
  }catch(const std::exception& e){std::fprintf(stderr,"Runtime wheel test failed: %s\n",e.what());return 1;}
}
