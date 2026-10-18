#include "../src/wheel.h"
#include "../src/wheel_effects.h"
#include "../launcher/src/settings.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <thread>

using namespace fm4::wheel;
void Check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class Predicate> Snapshot Wait(DeviceMonitor& m,Predicate p) {
  for(int i=0;i<250;++i) {
    SDL_PumpEvents(); SDL_UpdateJoysticks();
    auto s=m.Read(); if(p(s)) return s;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  auto state=m.Read();
  std::fprintf(stderr,"Monitor connected=%d device=%s axes=%zu first=%d buttons=%zu status=%s\n",
      state.connected,state.name.c_str(),state.axes.size(),state.axes.empty()?0:state.axes[0],state.buttons.size(),state.status.c_str());
  throw std::runtime_error("Timed out waiting for wheel monitor");
}
int main() {
  try {
    Config c;
    Check(!Valid(c),"Uncalibrated wheel accepted");
    c.calibration=127;
    Check(Valid(c),"Valid calibration rejected");
    Check(Steering(-32768,c)==-1 && Steering(0,c)==0 && Steering(32767,c)==1,"Steering endpoints incorrect");
    Check(std::abs(Steering(16384,c)-0.5f)<0.001f,"Steering midpoint incorrect");
    std::swap(c.left,c.right);
    Check(Steering(32767,c)==-1 && Steering(-32768,c)==1,"Inverted steering failed");
    c.center=5000;
    Check(Steering(5000,c)==0,"Asymmetric center calibration failed");
    Check(Pedal(32767,32767,-32768)==0 && Pedal(-32768,32767,-32768)==1,"Inverted pedals failed");
    Check(Pedal(15000,0,10000)==1 && Pedal(-2000,0,10000)==0,"Pedal clamping failed");
    Check(Pedal(1,0,0)==0,"Degenerate pedal calibration unsafe");
    c.right=c.center; Check(!Valid(c),"Degenerate steering calibration accepted");
    c=Config{};c.calibration=127;c.throttle_axis=c.brake_axis;
    Check(!Valid(c),"Shared pedal axis accepted as independent pedals");
    c=Config{};c.calibration=127;c.center=INT32_MAX;
    Check(!Valid(c),"Out-of-range calibration accepted");
    ForceEffect force;force.id=5;force.type=1;force.gain=255;force.magnitude=-127;
    SDL_HapticEffect output{};
    Check(BuildHapticEffect(force,false,output) && output.constant.level==-32767 && output.constant.length==100,"Native negative constant force translation failed");
    Check(BuildHapticEffect(force,true,output) && output.constant.level==32767,"Constant force inversion failed");
    force.magnitude=INT32_MAX;
    Check(BuildHapticEffect(force,false,output) && output.constant.level==32767,"Force overflow/clamping failed");
    force.type=8;force.positive_coefficient=127;force.negative_coefficient=-127;
    force.positive_saturation=255;force.negative_saturation=255;force.deadband=255;
    Check(BuildHapticEffect(force,false,output) && output.condition.right_coeff[0]==32767 &&
        output.condition.left_coeff[0]==-32767 && output.condition.right_sat[0]==65535 &&
        output.condition.deadband[0]==32767 && output.condition.length==100,"Condition force translation failed");
    for(int type:{8,9,10,11}) {force.type=type;Check(BuildHapticEffect(force,false,output),"Native condition effect rejected");}
    force.type=999;Check(!BuildHapticEffect(force,false,output),"Unknown force type accepted");
    const auto now=std::chrono::steady_clock::now();
    Check(ForceOutputAllowed(true,true,now-std::chrono::milliseconds(10),now),"Fresh force output blocked");
    Check(!ForceOutputAllowed(false,true,now,now),"Force output allowed without focus");
    Check(!ForceOutputAllowed(true,false,now,now),"Force output allowed while paused/disabled");
    Check(!ForceOutputAllowed(true,true,now-std::chrono::milliseconds(200),now),"Stale force output allowed");
    Check(!ForceOutputAllowed(true,true,now+std::chrono::milliseconds(10),now),"Future heartbeat accepted");
    const auto config_file=std::filesystem::current_path()/"wheel-test-settings.toml";
    launcher::Settings saved;
    saved.wheel.enabled=true;saved.wheel.device="test-guid|test-path";saved.wheel.calibration=127;
    saved.wheel.buttons[0]=17;saved.wheel.brake_full=-30000;
    saved.wheel.buttons[10]=12;saved.wheel.buttons[13]=15;
    saved.wheel.buttons[14]=8;saved.wheel.buttons[15]=9;
    saved.wheel.clutch_enabled=true;saved.wheel.clutch_axis=6;saved.wheel.clutch_rest=32767;
    saved.wheel.clutch_full=-32768;saved.wheel.clutch_calibration=3;saved.wheel.clutch_action=1;saved.wheel.clutch_threshold=40;
    saved.wheel.ffb=true;saved.wheel.ffb_invert=true;saved.wheel.ffb_gain=100;
    Check(saved.Save(config_file),"Cannot save wheel settings");
    launcher::Settings loaded;
    Check(loaded.Load(config_file) && loaded.wheel.enabled && loaded.wheel.device==saved.wheel.device &&
        loaded.wheel.buttons[0]==17 && loaded.wheel.buttons[10]==12 && loaded.wheel.buttons[13]==15 &&
        loaded.wheel.brake_full==-30000 && loaded.wheel.calibration==127 &&
        loaded.wheel.ffb && loaded.wheel.ffb_invert && loaded.wheel.ffb_gain==100 &&
        loaded.wheel.buttons[14]==8 && loaded.wheel.buttons[15]==9 && ValidClutch(loaded.wheel) &&
        loaded.wheel.clutch_full==-32768 && loaded.wheel.clutch_threshold==40,"Wheel settings lost on reload");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    Check(SDL_Init(SDL_INIT_JOYSTICK),SDL_GetError());
    SDL_VirtualJoystickDesc desc{}; SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_WHEEL;desc.naxes=3;desc.nbuttons=10;desc.nhats=1;desc.name="FM4 test wheel";
    const auto id=SDL_AttachVirtualJoystick(&desc);Check(id!=0,SDL_GetError());
    auto* joystick=SDL_OpenJoystick(id);Check(joystick!=nullptr,SDL_GetError());
    {
      DeviceMonitor monitor;
      auto inventory=Wait(monitor,[](const Snapshot& s){return std::any_of(s.devices.begin(),s.devices.end(),[](const Device& d){return d.name=="FM4 test wheel";});});
      const auto item=std::find_if(inventory.devices.begin(),inventory.devices.end(),[](const Device& d){return d.name=="FM4 test wheel";});
      monitor.Select(item->key);
      auto s=Wait(monitor,[](const Snapshot& s){return s.connected && s.name=="FM4 test wheel";});
      Check(s.axes.size()==3 && s.buttons.size()==10,"Virtual wheel capabilities lost");
      Check(SDL_SetJoystickVirtualAxis(joystick,0,12345),SDL_GetError());
      Check(SDL_SetJoystickVirtualAxis(joystick,1,-23456),SDL_GetError());
      Check(SDL_SetJoystickVirtualButton(joystick,3,true),SDL_GetError());
      s=Wait(monitor,[](const Snapshot& s){return s.axes.size()==3 && s.axes[0]==12345 && s.axes[1]==-23456 && s.buttons[3];});
      Check(s.haptic_status.empty(),"Motor diagnostic ran without a request");
      SDL_CloseJoystick(joystick);SDL_DetachVirtualJoystick(id);
      Wait(monitor,[](const Snapshot& s){return !s.connected && s.axes.empty() && s.buttons.empty();});
    }
    SDL_Quit();
    std::puts("Wheel calibration, settings, raw axes/buttons and disconnect tests passed.");
    return 0;
  } catch(const std::exception& e) {std::fprintf(stderr,"Wheel test failed: %s\n",e.what());return 1;}
}

