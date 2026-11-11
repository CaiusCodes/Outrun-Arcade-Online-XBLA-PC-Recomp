#pragma once
#include <array>
#include <atomic>
#include <cstdint>
namespace outrun::diagnostics {
// Independent budgets ensure gain sampling cannot discard later key/lifecycle
// events. Diagnostics only: no sound, gain or guest state is changed here.
class TraceBudget {
 public:
  bool Event(){return events_.fetch_add(1)<10000;}
  bool TireGain(uint32_t layer,uint32_t object,int32_t handle,uint64_t now){
    if(layer>=16)return false;
    auto& last=last_[layer];
    if(last.valid && last.object==object && last.handle==handle && now-last.time<50)return false;
    last={true,object,handle,now};
    return gains_.fetch_add(1)<6000;
  }
 private:
  struct Sample {bool valid=false;uint32_t object=0;int32_t handle=-1;uint64_t time=0;};
  std::array<Sample,16> last_{}; // Used by the guest audio thread only.
  std::atomic<unsigned> events_{0},gains_{0};
};
}
