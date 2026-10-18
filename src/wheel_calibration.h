#pragma once
#include "wheel.h"

namespace fm4::wheel {
// A draft is committed only after all steps validate; cancelling preserves the
// working calibration. Axis numbers and pedal direction are discovered from
// movement, never guessed from a device name.
struct CalibrationTour {
  int step = -1;
  Config draft;
  std::vector<int> baseline;
  enum Error { None, Disconnected, NoMovement, Ambiguous, InvalidRange } error = None;
  bool Active() const { return step >= 0 && step < 8; }
  void Start(const Config& config) { draft=config;draft.calibration=0;baseline.clear();step=0;error=None; }
  void Cancel() { step=-1;error=None; }
  int Detect(const std::vector<int>& axes, int excluded1=-1, int excluded2=-1) {
    int best=-1, first=0, second=0;
    for(size_t i=0;i<axes.size();++i) {
      if(int(i)==excluded1 || int(i)==excluded2) continue;
      const int delta=std::abs(axes[i]-baseline[i]);
      if(delta>first) {second=first;first=delta;best=int(i);} else second=std::max(second,delta);
    }
    if(first<4096) {error=NoMovement;return -1;}
    if(second>first*0.7f) {error=Ambiguous;return -1;}
    return best;
  }
  bool Next(const Snapshot& state) {
    error=None;
    if(!Active()) return false;
    if(!state.connected || state.key!=draft.device || state.axes.empty() || state.axes.size()>64 ||
        (step>0 && state.axes.size()!=baseline.size())) {error=Disconnected;return false;}
    const auto& axes=state.axes;
    if(step==0) baseline=axes;
    if(step==1) {
      const int axis=Detect(axes);if(axis<0) return false;
      draft.throttle_axis=axis;draft.throttle_full=axes[axis];
    }
    if(step==2) {
      const int value=axes[draft.throttle_axis];
      if(std::abs(value-draft.throttle_full)<4096 || std::abs(value-baseline[draft.throttle_axis])>2048) {error=InvalidRange;return false;}
      draft.throttle_rest=value;draft.calibration|=24;
    }
    if(step==3) {
      const int axis=Detect(axes,draft.throttle_axis);if(axis<0) return false;
      draft.brake_axis=axis;draft.brake_full=axes[axis];
    }
    if(step==4) {
      const int value=axes[draft.brake_axis];
      if(std::abs(value-draft.brake_full)<4096 || std::abs(value-baseline[draft.brake_axis])>2048) {error=InvalidRange;return false;}
      draft.brake_rest=value;draft.calibration|=96;
    }
    if(step==5) {
      const int axis=Detect(axes,draft.throttle_axis,draft.brake_axis);if(axis<0) return false;
      draft.steering_axis=axis;draft.left=axes[axis];draft.calibration|=1;
    }
    if(step==6) {
      const int value=axes[draft.steering_axis], center=baseline[draft.steering_axis];
      if((value-center)*int64_t(draft.left-center)>=0 || std::abs(value-center)<4096) {error=InvalidRange;return false;}
      draft.right=value;draft.calibration|=4;
    }
    if(step==7) {
      const int value=axes[draft.steering_axis];
      if(std::abs(value-baseline[draft.steering_axis])>2048) {error=InvalidRange;return false;}
      draft.center=value;draft.calibration|=2;
      if(!Valid(draft)) {error=InvalidRange;return false;}
      draft.enabled=true;
    }
    ++step;return true;
  }
};
struct ClutchCalibrationTour {
  int step=-1;
  Config draft;
  std::vector<int> baseline;
  bool Active() const {return step>=0 && step<3;}
  void Start(const Config& c) {draft=c;draft.clutch_enabled=true;draft.clutch_calibration=0;step=0;baseline.clear();}
  void Cancel() {step=-1;}
  bool Next(const Snapshot& s) {
    if(!Active() || !s.connected || s.key!=draft.device || s.axes.empty() || s.axes.size()>64 ||
        (step>0 && s.axes.size()!=baseline.size())) return false;
    if(step==0) baseline=s.axes;
    if(step==1) {
      int best=-1,first=0,second=0;
      for(size_t i=0;i<s.axes.size();++i) {
        if(int(i)==draft.steering_axis || int(i)==draft.throttle_axis || int(i)==draft.brake_axis) continue;
        const int delta=std::abs(s.axes[i]-baseline[i]);
        if(delta>first) {second=first;first=delta;best=int(i);} else second=std::max(second,delta);
      }
      if(first<4096 || second>first*0.7f) return false;
      draft.clutch_axis=best;draft.clutch_full=s.axes[best];
    }
    if(step==2) {
      const int value=s.axes[draft.clutch_axis];
      if(std::abs(value-baseline[draft.clutch_axis])>2048 || std::abs(value-draft.clutch_full)<4096) return false;
      draft.clutch_rest=value;draft.clutch_calibration=3;
      if(!ValidClutch(draft)) return false;
    }
    ++step;return true;
  }
};
}
