#include "../src/wheel_calibration.h"
#include <cstdio>
#include <stdexcept>
using namespace fm4::wheel;
void Check(bool v,const char* why) {if(!v) throw std::runtime_error(why);}
int main() {
 try {
  Config original;original.device="virtual";original.ffb_invert=true;original.buttons[0]=7;
  for(bool inverted:{false,true}) {
   CalibrationTour tour;tour.Start(original);
   Snapshot s;s.connected=true;s.key="virtual";s.axes.assign(8,-32768);s.axes[0]=0;
   if(inverted) s.axes[2]=32767;
   Check(tour.Next(s),"Initial rest capture failed");
   Check(!tour.Next(s) && tour.error==CalibrationTour::NoMovement,"Stationary pedal accepted");
   s.axes[2]=inverted?-32768:32767;
   s.axes[6]=32767;
   Check(!tour.Next(s) && tour.error==CalibrationTour::Ambiguous,"Ambiguous axes accepted");
   s.axes[6]=-32768;
   Check(tour.Next(s) && tour.draft.throttle_axis==2,"Throttle axis detection failed");
   Check(!tour.Next(s),"Pressed pedal accepted as released");
   s.axes[2]=inverted?32767:-32768;Check(tour.Next(s),"Throttle release failed");
   s.axes[5]=12000;Check(tour.Next(s) && tour.draft.brake_axis==5,"Brake axis detection failed");
   s.axes[5]=-32768;Check(tour.Next(s),"Brake release failed");
   s.axes[0]=inverted?32767:-32768;Check(tour.Next(s),"Left steering detection failed");
   Check(!tour.Next(s),"Same steering direction accepted twice");
   s.axes[0]=inverted?-32768:32767;Check(tour.Next(s),"Right steering capture failed");
   Check(!tour.Next(s),"Full lock accepted as center");
   s.axes[0]=20;Check(tour.Next(s) && tour.step==8 && Valid(tour.draft),"Completed calibration invalid");
   Check(tour.draft.ffb_invert && tour.draft.buttons[0]==7 && tour.draft.enabled,"Existing settings lost");
   Check(original.calibration==0,"Tour mutated original settings before commit");
   Check(!tour.Next(s),"Completed tour accepted more input");
  }
  CalibrationTour t;t.Start(original);Snapshot missing;
  Config clutch_config=original;clutch_config.throttle_axis=2;clutch_config.brake_axis=5;
  ClutchCalibrationTour ct;ct.Start(clutch_config);
  Snapshot cs;cs.connected=true;cs.key="virtual";cs.axes.assign(8,0);cs.axes[6]=32767;
  Check(ct.Next(cs),"Clutch rest baseline failed");
  Check(!ct.Next(cs),"Motionless clutch accepted");
  cs.axes[2]=32767;Check(!ct.Next(cs),"Throttle accepted as clutch");cs.axes[2]=0;
  cs.axes[6]=-32768;Check(ct.Next(cs) && ct.draft.clutch_axis==6,"Inverted clutch axis detection failed");
  Check(!ct.Next(cs),"Pressed clutch accepted as released");
  cs.axes[6]=32767;Check(ct.Next(cs) && ValidClutch(ct.draft),"Clutch release calibration failed");
  ct.Start(clutch_config);ct.Cancel();Check(!ct.Active(),"Clutch cancel failed");
  Check(!t.Next(missing) && t.error==CalibrationTour::Disconnected,"Disconnect not rejected");
  missing.connected=true;missing.key="other";missing.axes.assign(8,0);
  Check(!t.Next(missing),"Another device accepted");
  t.Cancel();Check(!t.Active() && original.calibration==0,"Cancel changed calibration");
  std::puts("Guided calibration, inverted axes, ambiguous/no movement, invalid positions, disconnect and cancel passed.");return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"Calibration tour failed: %s\n",e.what());return 1;}
}
