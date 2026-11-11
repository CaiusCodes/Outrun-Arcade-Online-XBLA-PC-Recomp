#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace outrun::audio {
// Audio-thread-only state. Native ownership/lifetime and target volumes remain
// authoritative; this envelope applies only to the first80ms of a new voice.
class EngineAttack {
  struct Voice{bool valid=false;uint32_t object=0;uint64_t start=0;};
  std::array<Voice,315> voices_{};
 public:
  void Start(uint32_t group,uint32_t layer,uint32_t object,int32_t handle,uint64_t now){
    if(handle<0 || handle>=315)return;
    voices_[handle]={group==0 && layer>=2 && layer<16,object,now};
  }
  void Stop(int32_t handle){if(handle>=0 && handle<315)voices_[handle]={};}
  bool NeedsUpdate(int32_t handle) const {return handle>=0 && handle<315 && voices_[handle].valid;}
  bool Apply(uint32_t object,int32_t handle,uint64_t now,double& left,double& right){
    if(handle<0 || handle>=315 || !std::isfinite(left) || !std::isfinite(right))return false;
    auto& v=voices_[handle];
    if(!v.valid || v.object!=object || now<v.start)return false;
    if(now-v.start>=80){v.valid=false;return false;}
    const double t=double(now-v.start)/80;
    // Native centibels: gain=10^(value/2000). Squared amplitude ramp starts
    // quietly; no change to pitch or the eventual requested volume.
    if(t==0){left=std::min(left,-10000.0);right=std::min(right,-10000.0);}
    else {const double offset=4000*std::log10(t);left+=offset;right+=offset;}
    return true;
  }
};
void RecordEngineStart(uint32_t group,uint32_t layer,uint32_t object,int32_t handle);
}
