#include "../src/wheel_mapping.h"
#include "../src/wheel_effects.h"
#include "../launcher/src/settings.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

using namespace fm4::wheel;
void Check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}

int main() {
  try {
    // Bindings, in the Pinyon Shift syntax.
    auto pedal=ParseAxisBinding("SimJack|-axis2");
    Check(pedal && pedal->device=="SimJack" && pedal->axis==2 && pedal->direction==AxisDirection::kInverted,"Device axis binding");
    Check(ParseAxisBinding("+axis5")->device.empty() && ParseAxisBinding("a1")->axis==1,"Base axis binding");
    Check(!ParseAxisBinding("") && !ParseAxisBinding("axisX"),"Malformed axis accepted");
    auto buttons=ParseButtonBindings("UP=h0.up,A=0,1,B=0eb7:0004|+axis3,GEAR1=0079:0006|0,REVERSE=0079:0006|7,LT=4,junk=9,X=");
    Check(buttons.size()==7,"Button binding count");
    Check(buttons[0].hat==0 && buttons[0].hat_mask==1 && buttons[0].xbuttons==0x0001,"Hat binding");
    Check(buttons[1].button==0 && buttons[2].button==1 && buttons[2].xbuttons==0x1000,"A=0,1 continues the name");
    Check(buttons[3].axis==3 && buttons[3].device=="0eb7:0004" && buttons[3].xbuttons==0x2000,"Lever axis as B");
    Check(buttons[4].gear==1 && buttons[5].gear==-1,"Shifter gates");
    Check(buttons[6].left_trigger,"LT target");
    Check(ButtonsFromNames("B")==0x2000 && ButtonsFromNames("A+LB")==0x1100 && ButtonsFromNames("")==0,"Clutch button names");

    // Device matching.
    Check(DeviceMatches("0eb7:0004","FANATEC CSL DD",0x0eb7,0x0004,"\\\\?\\hid#1"),"vid:pid match");
    Check(!DeviceMatches("0eb7:0005","FANATEC CSL DD",0x0eb7,0x0004,""),"vid:pid mismatch");
    Check(DeviceMatches("csl dd","FANATEC CSL DD",0,0,""),"Name part match");
    Check(DeviceMatches("path:\\\\?\\HID#1","x",0,0,"\\\\?\\hid#1") && !DeviceMatches("path:a","x",0,0,""),"Path match");
    Check(MigrateDeviceKey("0300bd14b70e00000400000000000000|\\\\?\\HID#VID_0EB7")=="path:\\\\?\\HID#VID_0EB7","FM4 0.3 key");
    Check(MigrateDeviceKey("0eb7:0004")=="0eb7:0004","New keys unchanged");

    // Steering and pedals.
    Check(SteeringFromAxis(0,false,100,0)==0 && SteeringFromAxis(32767,false,100,0)==32767,"Steering ends");
    Check(SteeringFromAxis(-32768,false,100,0)==-32767 && SteeringFromAxis(32767,true,100,0)==-32767,"Steering invert");
    Check(SteeringFromAxis(16384,false,200,0)==32767,"Steering sensitivity reaches lock at half rotation");
    Check(SteeringFromAxis(1000,false,100,5)==0,"Steering deadzone");
    Check(PedalFromAxis(-32768,AxisDirection::kNormal,0,0)==0 && PedalFromAxis(32767,AxisDirection::kNormal,0,0)==255,"Pedal ends");
    Check(PedalFromAxis(32767,AxisDirection::kInverted,0,0)==0,"Inverted pedal rest");
    Check(PedalFromAxis(-32768+1000,AxisDirection::kNormal,3,0)==0,"Pedal bottom deadzone");
    Check(PedalFromAxis(30000,AxisDirection::kNormal,0,10)==255,"Pedal top deadzone");
    Check(ResolveAxisDirection(AxisDirection::kAuto,32767)==AxisDirection::kInverted,"Auto direction");
    Check(DpadFromHat(1|2)==(0x0001|0x0008),"Hat to D-pad");

    // Force feedback scaling of the game's +/-127 effects.
    ForceTuning tuning; tuning.strength=100;
    ForceEffect constant; constant.id=1; constant.type=kConstant; constant.gain=255; constant.magnitude=127;
    Check(EffectConstantLevel(constant,tuning)==32767,"Full constant force");
    tuning.strength=25;
    Check(std::abs(EffectConstantLevel(constant,tuning)-8191)<=1,"Strength scales force");
    tuning.invert=true;
    Check(EffectConstantLevel(constant,tuning)<0,"Invert reverses force");
    tuning={}; tuning.strength=100; tuning.min_force=10;
    constant.magnitude=1;
    Check(EffectConstantLevel(constant,tuning)>=3276,"Minimum force lifts small forces");
    constant.magnitude=0;
    Check(EffectConstantLevel(constant,tuning)==0,"Minimum force keeps zero at zero");
    ForceEffect spring; spring.id=2; spring.type=kSpring; spring.gain=255;
    spring.positive_coefficient=spring.negative_coefficient=127; spring.positive_saturation=spring.negative_saturation=255;
    tuning={}; tuning.strength=100; tuning.spring=50;
    auto condition=EffectCondition(spring,tuning);
    Check(std::abs(condition.right_coeff-16383)<=1 && condition.right_sat==65535,"Spring share scales coefficients only");
    SDL_HapticEffect sdl{};
    Check(BuildHapticEffect(spring,tuning,sdl) && sdl.type==SDL_HAPTIC_SPRING,"Spring maps to SDL");
    ForceEffect unknown; unknown.id=3; unknown.type=4;
    Check(!BuildHapticEffect(unknown,tuning,sdl),"Unknown effect accepted");
    Settings rumble; rumble.ffb_strength=100; rumble.ffb_rumble=100; rumble.ffb_rumble_road=0;
    Check(RumbleMagnitude(65535,0,rumble)==0 && RumbleMagnitude(0,65535,rumble)==32767,"Rumble shares");
    Check(ParseFfbMode("difference")==FfbMode::kEffects && ParseFfbMode("OFF")==FfbMode::kOff,"FFB modes");

    // The launcher writes and reads the same keys, and carries FM4 0.3 settings over.
    const auto dir=std::filesystem::temp_directory_path()/"fm4-wheel-tests";
    std::filesystem::create_directories(dir);
    launcher::Settings out;
    out.wheel.enabled=true; out.wheel.device="0eb7:0004"; out.wheel.throttle="SimJack|+axis0";
    out.wheel.buttons="RB=4,LB=5,B=SimJack|-axis3"; out.wheel.ffb_strength=40; out.wheel.telemetry_udp=true;
    out.wheel.deadzone_top={{5,0,2}};
    Check(out.Save(dir/"user.toml"),"Settings save");
    launcher::Settings in;
    Check(in.Load(dir/"user.toml"),"Settings load");
    Check(in.wheel.device==out.wheel.device && in.wheel.throttle==out.wheel.throttle && in.wheel.buttons==out.wheel.buttons &&
          in.wheel.ffb_strength==40 && in.wheel.telemetry_udp && in.wheel.deadzone_top[0]==5 && in.wheel.deadzone_top[2]==2,
          "Settings round trip");
    {
      std::ofstream old(dir/"old.toml");
      old << "wheel_enabled = true\nwheel_device = '0300bd14b70e00000400000000000000|\\\\?\\HID#X'\n"
             "wheel_ffb = true\nwheel_ffb_gain = 15\nwheel_throttle_axis = 2\nwheel_throttle_rest = 32767\nwheel_throttle_full = -32768\n"
             "wheel_brake_axis = 5\nwheel_brake_rest = -32768\nwheel_brake_full = 32767\nwheel_left = 32767\nwheel_right = -32768\n"
             "wheel_button_a = 3\nwheel_button_shift_up = 0\nwheel_shift_up_action = 5\nwheel_clutch_action = 0\n";
    }
    launcher::Settings migrated;
    Check(migrated.Load(dir/"old.toml"),"Old settings load");
    Check(migrated.wheel.device=="path:\\\\?\\HID#X" && migrated.wheel.throttle=="-axis2" && migrated.wheel.brake=="+axis5",
          "Old pedal axes carried over");
    Check(migrated.wheel.steering_invert && migrated.wheel.ffb_mode=="on" && migrated.wheel.ffb_strength==15,"Old steering and FFB carried over");
    Check(migrated.wheel.buttons.find("A=3")!=std::string::npos && migrated.wheel.buttons.find("RB=0")!=std::string::npos &&
          migrated.wheel.clutch_button=="A","Old buttons carried over");
    std::filesystem::remove_all(dir);
    std::puts("Wheel bindings, device matching, pedal/steering maths, FFB scaling and settings passed.");
    return 0;
  } catch(const std::exception& e) {std::fprintf(stderr,"Wheel test failed: %s\n",e.what());return 1;}
}
