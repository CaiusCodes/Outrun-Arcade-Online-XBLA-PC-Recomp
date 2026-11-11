#pragma once
#include <cstdint>

namespace outrun::gpu_wait {
enum class Action { Yield, Sleep };
// Budget belongs to one native wait instance; a new native initialization
// timestamp starts a fresh budget even when its stack address is reused.
struct Budget {
  uint32_t address=0, stamp=0;
  uint64_t started=0, last=0;
  bool active=false;
  void Reset() { active=false; }
  Action Poll(uint32_t next_address,uint32_t next_stamp,uint64_t now) {
    if(!active || address!=next_address || stamp!=next_stamp || now<last || now-last>5000) {
      active=true;address=next_address;stamp=next_stamp;started=now;
    }
    last=now;
    return now-started<500 ? Action::Yield : Action::Sleep;
  }
};
struct WaitSample { Action action=Action::Yield; double elapsed_us=0; };
void Backoff(uint32_t address,uint32_t stamp,bool waiting,WaitSample* sample=nullptr,
             uint32_t sleep_us=500);
}
